/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#include "feature_extractor.h"

#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnis/contrib/video_helper.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cxxutil/log.h"
#include "easycodec/vformat.h"

bool FeatureExtractor::Init(const std::string &model_path, const std::string &func_name, int dev_id) {
  if (model_path.empty() || func_name.empty()) {
    LOGW(SAMPLES) << "[FeatureExtractor] Do not need to init if extract feature on CPU";
    LOGI(SAMPLES) << "[FeatureExtractor] Model not set, using opencv to extract feature on CPU";
    extract_feature_mlu_ = false;
    return true;
  }
  device_id_ = dev_id;

  infer_server_.reset(new infer_server::InferServer(device_id_));

  infer_server::SessionDesc desc;
  desc.strategy = infer_server::BatchStrategy::STATIC;
  desc.engine_num = 1;
  desc.priority = 0;
  desc.show_perf = true;
  desc.name = "Feature extract session";

  // load offline model
#ifdef CNIS_USE_MAGICMIND
  desc.model = infer_server::InferServer::LoadModel(model_path);
#else
  desc.model = infer_server::InferServer::LoadModel(model_path, func_name);
#endif
  // set preproc and postproc
  desc.preproc = infer_server::video::PreprocessorMLU::Create();
  desc.postproc = infer_server::Postprocessor::Create();

#ifdef CNIS_USE_MAGICMIND
  desc.preproc->SetParams("dst_format", infer_server::video::PixelFmt::RGB24,
                          "src_format", infer_server::video::PixelFmt::NV12,
                          "preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                          "keep_aspect_ratio", false,
                          "mean", std::vector<float>({0.485, 0.456, 0.406}),
                          "std", std::vector<float>({0.229, 0.224, 0.225}),
                          "normalize", true);
#else
  desc.preproc->SetParams("dst_format", infer_server::video::PixelFmt::ARGB,
                          "src_format", infer_server::video::PixelFmt::NV12,
                          "keep_aspect_ratio", false,
                          "preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC);
#endif
  auto postproc_func = [](infer_server::InferData* result, const infer_server::ModelIO& model_output,
                          const infer_server::ModelInfo* model) {
    const float* data = reinterpret_cast<const float*>(model_output.buffers[0].Data());
    std::vector<float> features;
    features.insert(features.end(), data, data + model_output.shapes[0].DataCount());
    result->Set(std::move(features));
    return true;
  };
  desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(postproc_func));

  session_ = infer_server_->CreateSyncSession(desc);

  // Check model I/O
  if (desc.model->InputNum() != 1) {
    LOGE(SAMPLES) << "[FeatureExtractor] model should have exactly one input";
    return false;
  }
  if (desc.model->OutputNum() != 1) {
    LOGE(SAMPLES) << "[FeatureExtractor] model should have exactly two output";
    return false;
  }

  LOGI(SAMPLES) << "[FeatureExtractor] to extract feature on MLU";
  extract_feature_mlu_ = true;
  return true;
}

FeatureExtractor::~FeatureExtractor() { Destroy(); }

void FeatureExtractor::Destroy() {
  if (extract_feature_mlu_ && infer_server_ && session_) {
    LOGI(SAMPLES) << "[FeatureExtractor] destroy session";
    infer_server_->DestroySession(session_);
  }
}

static float CalcFeatureOfRow(cv::Mat img, int n) {
  float result = 0;
  for (int i = 0; i < img.cols; i++) {
    int grey = img.ptr<uchar>(n)[i];
    result += grey > 127 ? static_cast<float>(grey) / 255 : -static_cast<float>(grey) / 255;
  }
  return result;
}

constexpr int kFeatureSizeCpu = 512;

bool FeatureExtractor::ExtractFeatureOnMlu(const edk::CnFrame &frame,
                                           std::vector<edk::DetectObject>* objs) {
  if (objs->size()) {
    infer_server::video::VideoFrame vframe;
    vframe.width = frame.width;
    vframe.height = frame.height;
    vframe.stride[0] = frame.strides[0];
    vframe.stride[1] = frame.strides[1];
    size_t plane_0_size = frame.strides[0] * frame.height;
    size_t plane_1_size = std::ceil(1.0 * frame.strides[1] * frame.height / 2);
    vframe.plane[0] = infer_server::Buffer(const_cast<void*>(frame.ptrs[0]), plane_0_size, nullptr, device_id_);
    vframe.plane[1] = infer_server::Buffer(const_cast<void*>(frame.ptrs[1]), plane_1_size, nullptr, device_id_);
    vframe.format = infer_server::video::PixelFmt::NV12;

    auto in = infer_server::Package::Create(objs->size());
    auto out = infer_server::Package::Create(objs->size());
    for (unsigned idx = 0; idx < objs->size(); ++idx) {
      edk::DetectObject obj = (*objs)[idx];
      infer_server::video::VideoFrame tmp = vframe;
      tmp.roi.x = obj.bbox.x;
      tmp.roi.y = obj.bbox.y;
      tmp.roi.w = obj.bbox.width;
      tmp.roi.h = obj.bbox.height;
      in->data[idx]->Set(std::move(tmp));
    }
    infer_server::Status status = infer_server::Status::SUCCESS;
    bool ret = infer_server_->RequestSync(session_, std::move(in), &status, out);
    if (!ret || status != infer_server::Status::SUCCESS) {
      LOGE(SAMPLE) << "Request sending data to infer server failed. Status: "
                   << std::to_string(static_cast<int>(status));
      return false;
    }

    for (unsigned idx = 0; idx < objs->size(); ++idx) {
      (*objs)[idx].feature = out->data[idx]->GetLref<std::vector<float>>();
    }
  }
  return true;
}

bool FeatureExtractor::ExtractFeatureOnCpu(const cv::Mat &frame,
                                           std::vector<edk::DetectObject>* objs) {
  if (objs->size()) {
    int frame_width = frame.cols;
    int frame_height = frame.rows;
    for (auto& obj : (*objs)) {
      cv::Mat obj_img(frame, cv::Rect(obj.bbox.x * frame_width, obj.bbox.y * frame_height, obj.bbox.width * frame_width,
                                      obj.bbox.height * frame_height));
  #if(CV_MAJOR_VERSION == 2)
      cv::Ptr<cv::ORB> processer = new cv::ORB(kFeatureSizeCpu);
  #elif(CV_MAJOR_VERSION >= 3)
      cv::Ptr<cv::ORB> processer = cv::ORB::create(kFeatureSizeCpu);
  #endif
      std::vector<cv::KeyPoint> keypoints;
      processer->detect(obj_img, keypoints);
      cv::Mat desc;
      processer->compute(obj_img, keypoints, desc);

      std::vector<float> features(kFeatureSizeCpu);
      for (int i = 0; i < kFeatureSizeCpu; i++) {
        features[i] = i < desc.rows ? CalcFeatureOfRow(desc, i) : 0;
      }
      obj.feature = features;
    }
  }
  return true;
}
