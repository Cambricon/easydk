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
#include "postproc.h"

#ifdef CNIS_USE_MAGICMIND
constexpr const char* g_model_path =
    "http://video.cambricon.com/models/MLU370/yolov3_nhwc_tfu_0.8.2_uint8_int8_fp16.model";
#else
constexpr const char* g_model_path =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
#endif

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
        std::cout << "@@@@@@@@@@@ No objects detected in frame " << frame_index << std::endl;
        continue;
      }
      std::cout << "------------ Detected objects in frame " << frame_index << std::endl;
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


int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "USAGE: " << argv[0] << " path-to-video-file" << std::endl;
    return 0;
  }

  cv::VideoCapture source(argv[1]);
  if (!source.isOpened()) {
    std::cerr << "!!!!!!!! cannot open video file: " << argv[1] << std::endl;
    return -1;
  }

  // server by device id
  infer_server::InferServer server(0);

  infer_server::SessionDesc desc;
  desc.batch_timeout = 200;
  desc.strategy = infer_server::BatchStrategy::DYNAMIC;
  desc.name = "infer server demo";
  desc.show_perf = true;
  desc.preproc = infer_server::PreprocessorHost::Create();
  desc.postproc = infer_server::Postprocessor::Create();

  // config process function
  #ifdef CNIS_USE_MAGICMIND
  // load model
  desc.model = infer_server::InferServer::LoadModel(g_model_path);
  desc.host_input_layout = {infer_server::DataType::UINT8, infer_server::DimOrder::NHWC};
  desc.preproc->SetParams("process_function",
                          infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGB24,
                                                                          {}, {}, false, true));
  desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov3MM(0.5)));
  #else
  desc.model = infer_server::InferServer::LoadModel(g_model_path);
  desc.preproc->SetParams("process_function",
                          infer_server::video::OpencvPreproc::GetFunction(infer_server::video::PixelFmt::RGBA));
  desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocSSD(0.5)));
  #endif

  infer_server::Session_t session = server.CreateSession(desc, std::make_shared<PrintResult>());

  cv::Mat frame;
  constexpr const char* tag = "opencv_stream0";
  int frame_index = 0;
#ifdef CNIS_USE_MAGICMIND
  int frame_width = 0;
  int frame_height = 0;
#endif
  // read frame to infer
  while (source.read(frame)) {
#ifdef CNIS_USE_MAGICMIND
    frame_width = static_cast<int>(frame.cols);
    frame_height = static_cast<int>(frame.rows);
#endif
    infer_server::video::OpencvFrame cv_frame;
    cv_frame.img = std::move(frame);
    cv_frame.fmt = infer_server::video::PixelFmt::BGR24;
    infer_server::PackagePtr input = std::make_shared<infer_server::Package>();
    input->data.emplace_back(new infer_server::InferData);
    input->data[0]->Set(std::move(cv_frame));
#ifdef CNIS_USE_MAGICMIND
    input->data[0]->SetUserData(FrameSize{frame_width, frame_height});
#endif
    input->tag = tag;
    if (!server.Request(session, input, frame_index++)) {
      std::cerr << "request failed" << std::endl;
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

