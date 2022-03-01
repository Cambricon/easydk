/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include <libyuv.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnis/contrib/opencv_frame.h"
#include "cnis/contrib/video_helper.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"

#include "device/mlu_context.h"

#include "postproc.h"

#define USE_CNCV_PREPROC
// uncomment to use OpenCV preproc; comment to use CNCV preproc;
// #undef USE_CNCV_PREPROC

constexpr const char* g_model_path_mlu370 =
    "http://video.cambricon.com/models/MLU370/yolov3_nhwc_tfu_0.8.2_uint8_int8_fp16.model";
constexpr const char* g_model_path_mlu270 =
    "http://video.cambricon.com/models/MLU270/yolov5/yolov5_b4c4_rgb_mlu270.cambricon";
#ifdef CNIS_HAVE_CURL
constexpr const char* g_model_path_mlu220 =
    "http://video.cambricon.com/models/MLU220/yolov5/yolov5_b4c4_rgb_mlu220.cambricon";
#else
// Please download from "http://video.cambricon.com/models/MLU220/yolov5/yolov5_b4c4_rgb_mlu220.cambricon";
constexpr const char* g_model_path_mlu220 = "yolov5_b4c4_rgb_mlu220.cambricon";
#endif

static int g_device_id = 0;

std::mutex g_mtx_for_video_capture_open;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

class PrintResult : public infer_server::Observer {
  void Response(infer_server::Status status, infer_server::PackagePtr out, infer_server::any user_data) noexcept {
    int frame_index = infer_server::any_cast<int>(user_data);
    if (status != infer_server::Status::SUCCESS) {
      std::cerr << "Infer failed for frame index " << frame_index << std::endl;
      return;
    }
    for (auto& it : out->data) {
      const std::vector<DetectObject>& objs = it->GetLref<std::vector<DetectObject>>();
      if (objs.empty()) {
        std::cout << "@@@@@@@@@@@ stream_id: [" << out->tag << "], No objects detected in frame "
                  << frame_index << std::endl;
        continue;
      }
      std::cout << "----- [" << out->tag << "]: Detected objects in frame "
                << frame_index << std::endl;
      for (auto& obj : objs) {
        std::cout << "label: " << obj.label
                  << "\t score: " << obj.score
                  << "\t bbox: " << obj.bbox.x << ", " << obj.bbox.y << ", " << obj.bbox.w << ", " << obj.bbox.h
                  << std::endl;
      }
      std::cout << "------------ Detected objects end -----------" << std::endl;
    }
  }
};

int Process(infer_server::InferServer server, infer_server::Session_t session,
            std::string video_path, std::string stream_id) {
  std::unique_lock<std::mutex> lk(g_mtx_for_video_capture_open);
  cv::VideoCapture source(video_path.c_str());
  lk.unlock();
  if (!source.isOpened()) {
    std::cerr << "!!!!!!!! cannot open video file: " << video_path << ", stream_id: " << stream_id << std::endl;
    return -1;
  }
  cv::Mat frame;
  int frame_index = 0;
  int frame_width = 0;
  int frame_height = 0;
  std::unique_ptr<uint8_t[]> cpu_yuv_data = nullptr;

  // read frame to infer
  while (source.read(frame)) {
    frame_width = static_cast<int>(frame.cols);
    frame_height = static_cast<int>(frame.rows);

    infer_server::PackagePtr input = std::make_shared<infer_server::Package>();
    input->data.emplace_back(new infer_server::InferData);
#ifdef USE_CNCV_PREPROC
    // First of all convert bgr to yuv nv12 by libyuv and copy image data from host to device
    int frame_stride = static_cast<int>(frame.step);
    int y_plane_bytes = frame_width * frame_height;
    if (!cpu_yuv_data) cpu_yuv_data.reset(new uint8_t[y_plane_bytes * 3 / 2]);
    libyuv::RGB24ToNV12(frame.data, frame_stride, cpu_yuv_data.get(), frame_width,
                        cpu_yuv_data.get() + y_plane_bytes, frame_width,
                        frame_width, frame_height);
    infer_server::video::VideoFrame video_frame;
    video_frame.plane_num = 2;
    video_frame.format = infer_server::video::PixelFmt::NV12;
    video_frame.width = frame_width;
    video_frame.height = frame_height;
    video_frame.stride[0] = frame_width;
    video_frame.stride[1] = frame_width;
    video_frame.plane[0] = infer_server::Buffer(y_plane_bytes, g_device_id);
    video_frame.plane[1] = infer_server::Buffer(y_plane_bytes / 2, g_device_id);
    video_frame.plane[0].CopyFrom(cpu_yuv_data.get(), y_plane_bytes);
    video_frame.plane[1].CopyFrom(cpu_yuv_data.get() + y_plane_bytes, y_plane_bytes / 2);

    input->data[0]->Set(std::move(video_frame));
#else
    infer_server::video::OpencvFrame cv_frame;
    cv_frame.img = frame;
    cv_frame.fmt = infer_server::video::PixelFmt::BGR24;
    input->data[0]->Set(std::move(cv_frame));
#endif
    input->data[0]->SetUserData(FrameSize{frame_width, frame_height});
    input->tag = stream_id;
    if (!server.Request(session, input, frame_index++)) {
      std::cerr << "stream_id [" << stream_id << "] request failed" << std::endl;
      server.DestroySession(session);
      return -2;
    }
  }

  server.WaitTaskDone(session, stream_id);
  return 0;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "USAGE: " << argv[0] << " path-to-video-file" << std::endl;
    return 0;
  }

  uint64_t start = duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  int stream_number = 2;
  std::string video_path = argv[1];

  // server by device id
  infer_server::InferServer server(g_device_id);

  infer_server::SessionDesc desc;
  desc.batch_timeout = 200;
  desc.strategy = infer_server::BatchStrategy::DYNAMIC;
  desc.name = "infer server demo";
  desc.show_perf = true;
  // We recommend engine_num = total core number / model core number. For example, on MLU270(16 ipu cores),
  // and load a 4 core model(core numebr is a given parameter when generate model), engine_num = 16 / 4 = 4.
  desc.engine_num = 4;
  // load model
  edk::MluContext ctx(g_device_id);
  edk::CoreVersion core_version = ctx.GetCoreVersion();
  if (core_version == edk::CoreVersion::MLU370) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu370);
  } else if (core_version == edk::CoreVersion::MLU270) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu270);
  } else if (core_version == edk::CoreVersion::MLU220) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu220);
  } else {
    std::cerr << "Core version is not supported, " << std::to_string(static_cast<int>(core_version)) << std::endl;
  }

#ifdef USE_CNCV_PREPROC
  // Use CNCV preproc
  desc.preproc = infer_server::video::PreprocessorMLU::Create();
  desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                          "src_format", infer_server::video::PixelFmt::NV12,
                          "dst_format", infer_server::video::PixelFmt::RGB24,
                          "normalize", false,
                          "keep_aspect_ratio", true);
#else
  // Use OpenCV preproc
  desc.preproc = infer_server::PreprocessorHost::Create();
  desc.host_input_layout = {infer_server::DataType::UINT8, infer_server::DimOrder::NHWC};
  desc.preproc->SetParams("process_function",
                          infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGB24,
                                                                          {}, {}, false, true));
#endif

  desc.postproc = infer_server::Postprocessor::Create();
  if (core_version == edk::CoreVersion::MLU370) {
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov3MM(0.5)));
  } else {
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov5(0.5)));
  }

  infer_server::Session_t session = server.CreateSession(desc, std::make_shared<PrintResult>());

  uint64_t process_start = duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  std::vector<std::pair<std::string, std::future<int>>> process_return_vec;
  for (int i = 0; i < stream_number; i++) {
    std::string stream_id = "stream_" + std::to_string(i);
    std::future<int> process_return = std::async(std::launch::async, &Process, server, session, video_path, stream_id);
    process_return_vec.push_back({stream_id, std::move(process_return)});
  }

  std::vector<std::pair<std::string, int>> failed_stream_vec;
  while (!process_return_vec.empty()) {
    for (auto iter = process_return_vec.begin(); iter != process_return_vec.end();) {
      if (iter->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        int ret = iter->second.get();
        if (ret == 0) {
          std::cout << "############## Stream id: [" << iter->first << "] PROCESS SUCCEED!!" << std::endl;
        } else {
          failed_stream_vec.push_back({iter->first, ret});
          std::cout << "############## Stream id: [" << iter->first << "] PROCESS FAILED, ret code = "
                    << ret << std::endl;
        }
        process_return_vec.erase(iter);
        iter = process_return_vec.begin();
      } else {
        iter++;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  server.DestroySession(session);

  uint64_t end = duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  if (failed_stream_vec.empty()) {
    std::cout << "All " << stream_number << " Streams Process Succeed!!!" << std::endl;
  } else {
    std::cout << failed_stream_vec.size() << "/" << stream_number << " Streams Process Failed!!!" << std::endl;
    for (auto &it : failed_stream_vec) {
      std::cout << "Stream id : [" << it.first << "], ret code = " << it.second << std::endl;
    }
  }

  std::cout << "Total time: " << end - start << " ms, Process time: " << end - process_start << " ms" << std::endl;

  return 0;
}

