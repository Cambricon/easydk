/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#include <opencv2/opencv.hpp>

#include <memory>
#include <utility>
#include <vector>

#include "cnis/contrib/opencv_frame.h"
#include "cnis/contrib/video_helper.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "device/mlu_context.h"

#include "postproc.h"

constexpr const char* g_model_path_mlu370 =
    "http://video.cambricon.com/models/MLU370/yolov3_nhwc_tfu_0.8.2_uint8_int8_fp16.model";
constexpr const char* g_model_path_mlu270 =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
#ifdef CNIS_HAVE_CURL
constexpr const char* g_model_path_mlu220 =
    "http://video.cambricon.com/models/MLU220/yolov5/yolov5_b4c4_rgb_mlu220.cambricon";
#else
// Please download from "http://video.cambricon.com/models/MLU220/yolov5/yolov5_b4c4_rgb_mlu220.cambricon";
constexpr const char* g_model_path_mlu220 = "yolov5_b4c4_rgb_mlu220.cambricon";
#endif

static int g_device_id = 0;

class PrintResult : public infer_server::Observer {
  void Response(infer_server::Status status, infer_server::PackagePtr out, infer_server::any user_data) noexcept {
    int frame_index = infer_server::any_cast<int>(user_data);
    if (status != infer_server::Status::SUCCESS) {
      LOG(ERROR) << "[EasyDK InferServerSamples] [Demo] Infer failed for frame index " << frame_index;
      return;
    }
    for (auto& it : out->data) {
      const std::vector<DetectObject>& objs = it->GetLref<std::vector<DetectObject>>();
      if (objs.empty()) {
        std::cout << "[EasyDK InferServerSamples] [Demo] @@@@@@@@@@@ No objects detected in frame " << frame_index
                  << std::endl;
        continue;
      }
      std::cout << "[EasyDK InferServerSamples] [Demo] ------------ Detected objects in frame " << frame_index
                << std::endl;
      for (auto& obj : objs) {
        std::cout <<  "[EasyDK InferServerSamples] [Demo] label: " << obj.label << "\t score: " << obj.score
                  << "\t bbox: " << obj.bbox.x << ", " << obj.bbox.y << ", " << obj.bbox.w << ", " << obj.bbox.h
                  << std::endl;
      }
      std::cout << "[EasyDK InferServerSamples] [Demo] ------------ Detected objects end -----------" << std::endl;
    }
  }
};


int main(int argc, char** argv) {
  if (argc != 2) {
    LOG(ERROR) << "[EasyDK InferServerSamples] [Demo] USAGE: " << argv[0] << " path-to-video-file";
    return 0;
  }

  cv::VideoCapture source(argv[1]);
  if (!source.isOpened()) {
    LOG(ERROR) << "[EasyDK InferServerSamples] [Demo] Cannot open video file: " << argv[1];
    return -1;
  }

  // Bind this thread to device
  edk::MluContext ctx(g_device_id);
  ctx.BindDevice();

  // create infer server
  infer_server::InferServer server(g_device_id);

  infer_server::SessionDesc desc;
  desc.batch_timeout = 200;
  desc.strategy = infer_server::BatchStrategy::DYNAMIC;
  desc.name = "infer server demo";
  desc.show_perf = true;
  desc.preproc = infer_server::PreprocessorHost::Create();
  desc.postproc = infer_server::Postprocessor::Create();

  edk::CoreVersion core_version = ctx.GetCoreVersion();
  if (core_version == edk::CoreVersion::MLU370) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu370);
  } else if (core_version == edk::CoreVersion::MLU270) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu270);
  } else if (core_version == edk::CoreVersion::MLU220) {
    desc.model = infer_server::InferServer::LoadModel(g_model_path_mlu220);
  } else {
    LOG(ERROR) << "[EasyDK InferServerSamples] [MultiStreamDemo] Core version is not supported, "
               << CoreVersionStr(core_version);
  }
  // config process function
  if (core_version == edk::CoreVersion::MLU370) {
    desc.host_input_layout = {infer_server::DataType::UINT8, infer_server::DimOrder::NHWC};
    desc.preproc->SetParams("process_function",
                            infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGB24,
                                                                            {}, {}, false, true));
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov3MM(0.5)));
  } else if (core_version == edk::CoreVersion::MLU270) {
    desc.host_input_layout = {infer_server::DataType::UINT8, infer_server::DimOrder::NHWC};
    desc.preproc->SetParams("process_function",
                            infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGBA,
                                                                            {}, {}, false, false));
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocSSD(0.3)));
  } else if (core_version == edk::CoreVersion::MLU220) {
    desc.host_input_layout = {infer_server::DataType::FLOAT32, infer_server::DimOrder::NHWC};
    desc.preproc->SetParams("process_function",
                            infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGB24,
                                                                            {}, {}, true, true));
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov5(0.5)));
  }

  infer_server::Session_t session = server.CreateSession(desc, std::make_shared<PrintResult>());

  cv::Mat frame;
  constexpr const char* tag = "opencv_stream0";
  int frame_index = 0;
  int frame_width = 0;
  int frame_height = 0;
  // read frame to infer
  while (source.read(frame)) {
    frame_width = static_cast<int>(frame.cols);
    frame_height = static_cast<int>(frame.rows);
    infer_server::video::OpencvFrame cv_frame;
    cv_frame.img = std::move(frame);
    cv_frame.fmt = infer_server::video::PixelFmt::BGR24;
    infer_server::PackagePtr input = std::make_shared<infer_server::Package>();
    input->data.emplace_back(new infer_server::InferData);
    input->data[0]->Set(std::move(cv_frame));
    // Set frame size to user data, when postproc needs frame size, for example when keep aspect ratio is true.
    input->data[0]->SetUserData(FrameSize{frame_width, frame_height});
    input->tag = tag;
    if (!server.Request(session, input, frame_index++)) {
      LOG(ERROR) << "[EasyDK InferServerSamples] [Demo] Request failed";
      server.DestroySession(session);
      return -2;
    }

    // since old is moved, we create a new mat
    frame = cv::Mat();
  }

  server.WaitTaskDone(session, tag);
  server.DestroySession(session);

  return 0;
}

