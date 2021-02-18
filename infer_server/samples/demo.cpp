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

#include "infer_server.h"
#include "opencv_frame.h"
#include "processor.h"
#include "video_helper.h"

constexpr const char* g_model_path =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
constexpr const char* g_func_name = "subnet0";

struct DetectObject {
  int label;
  float score;
  infer_server::video::BoundingBox bbox;
};

struct PostprocSSD {
  float threshold;

  explicit PostprocSSD(float _threshold) : threshold(_threshold) {}

  inline float Clip(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo& model) {
    std::vector<DetectObject> objs;
    const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
    int box_num = data[0];
    data += 64;

    for (int bi = 0; bi < box_num; ++bi) {
      DetectObject obj;
      if (data[1] == 0) continue;
      obj.label = data[1] - 1;
      obj.score = data[2];
      if (threshold > 0 && obj.score < threshold) continue;
      obj.bbox.x = Clip(data[3]);
      obj.bbox.y = Clip(data[4]);
      obj.bbox.w = Clip(data[5]) - obj.bbox.x;
      obj.bbox.h = Clip(data[6]) - obj.bbox.y;
      objs.emplace_back(std::move(obj));
      data += 7;
    }

    result->Set(std::move(objs));
    return true;
  }
};

class PrintResult : public infer_server::Observer {
  void Response(infer_server::Status status, infer_server::PackagePtr out, infer_server::any user_data) noexcept {
    int frame_index = infer_server::any_cast<int>(user_data);
    if (status != infer_server::Status::SUCCESS) {
      std::cerr << "Infer SSD failed for frame index " << frame_index << std::endl;
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
  // load model
  infer_server::ModelPtr model = infer_server::InferServer::LoadModel(g_model_path, g_func_name);

  infer_server::SessionDesc desc;
  desc.batch_timeout = 200;
  desc.strategy = infer_server::BatchStrategy::DYNAMIC;
  desc.name = "infer server demo session";
  desc.show_perf = true;
  desc.preproc = std::make_shared<infer_server::PreprocessorHost>();
  desc.postproc = std::make_shared<infer_server::Postprocessor>();
  desc.model = model;

  // config process function
  desc.preproc->SetParams("process_function", infer_server::video::DefaultOpencvPreproc::GetFunction());
  desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocSSD(0.5)));

  infer_server::Session_t session = server.CreateSession(desc, std::make_shared<PrintResult>());

  cv::Mat frame;
  constexpr const char* tag = "opencv_stream0";
  int frame_index = 0;
  // read frame to infer
  while (source.read(frame)) {
    infer_server::video::OpencvFrame cv_frame;
    cv_frame.img = std::move(frame);
    cv_frame.fmt = infer_server::video::PixelFmt::BGR24;
    infer_server::PackagePtr input = std::make_shared<infer_server::Package>();
    input->data.emplace_back(new infer_server::InferData);
    input->data[0]->Set(std::move(cv_frame));
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

