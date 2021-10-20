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

#include "cxxutil/log.h"
#include "device/mlu_context.h"
#include "easyinfer/easy_infer.h"

bool FeatureExtractor::Init(const std::string &model_path, const std::string &func_name, int dev_id) {
  if (model_path.empty() || func_name.empty()) {
    LOGW(SAMPLES) << "[FeatureExtractor] Do not need to init if extract feature on CPU";
    LOGI(SAMPLES) << "[FeatureExtractor] Model not set, using opencv to extract feature on CPU";
    extract_feature_mlu_ = false;
    return true;
  }
  model_ = std::make_shared<edk::ModelLoader>(model_path, func_name);
  device_id_ = dev_id;

  // 1. init runtime_lib and device
  edk::MluContext context;
  context.SetDeviceId(device_id_);
  context.BindDevice();

  // Check model I/O
  if (model_->InputNum() != 1) {
    LOGE(SAMPLES) << "[FeatureExtractor] model should have exactly one input";
    return false;
  }
  if (model_->OutputNum() != 2) {
    LOGE(SAMPLES) << "[FeatureExtractor] model should have exactly two output";
    return false;
  }
  if (model_->InputShape(0).C() != 3) {
    LOGE(SAMPLES) << "[FeatureExtractor] feature extractor model wrong input shape!";
    return false;
  }

  // prepare input and output memory
  mem_op_.SetModel(model_);
  input_cpu_ptr_ = mem_op_.AllocCpuInput();
  input_mlu_ptr_ = mem_op_.AllocMluInput();
  output_mlu_ptr_ = mem_op_.AllocMluOutput();
  output_cpu_ptr_ = mem_op_.AllocCpuOutput();

  // init Easyinfer
  infer_.Init(model_, device_id_);
  LOGI(SAMPLES) << "[FeatureExtractor] to extract feature on MLU";
  extract_feature_mlu_ = true;
  return true;
}

FeatureExtractor::~FeatureExtractor() { Destroy(); }

void FeatureExtractor::Destroy() {
  if (extract_feature_mlu_) {
    LOGI(SAMPLES) << "[FeatureExtractor] release resources";
    if (input_mlu_ptr_) mem_op_.FreeMluInput(input_mlu_ptr_);
    if (output_mlu_ptr_) mem_op_.FreeMluOutput(output_mlu_ptr_);
    if (input_cpu_ptr_) mem_op_.FreeCpuInput(input_cpu_ptr_);
    if (output_cpu_ptr_) mem_op_.FreeCpuOutput(output_cpu_ptr_);
    input_mlu_ptr_ = output_mlu_ptr_ = input_cpu_ptr_ = output_cpu_ptr_ = nullptr;
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

std::vector<float> FeatureExtractor::ExtractFeature(const edk::TrackFrame &frame, const edk::DetectObject &obj) {
  if (frame.format != edk::TrackFrame::ColorSpace::RGB24) {
    LOGE(SAMPLES) << "[FeatureExtractor] input image has non-support pixel format";
    return {};
  }

  cv::Mat image(frame.height, frame.width, CV_8UC3, frame.data);
  cv::Mat obj_img(image, cv::Rect(obj.bbox.x * frame.width, obj.bbox.y * frame.height, obj.bbox.width * frame.width,
                                  obj.bbox.height * frame.height));

  if (extract_feature_mlu_) {
    std::lock_guard<std::mutex> lk(mlu_proc_mutex_);
    Preprocess(obj_img);

    mem_op_.MemcpyInputH2D(input_mlu_ptr_, input_cpu_ptr_);

    infer_.Run(input_mlu_ptr_, output_mlu_ptr_);

    mem_op_.MemcpyOutputD2H(output_cpu_ptr_, output_mlu_ptr_);

    const float *begin = reinterpret_cast<float *>(output_cpu_ptr_[1]);
    const float *end = begin + model_->OutputShape(1).BatchDataCount();
    return std::vector<float>(begin, end);
  } else {
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
    return features;
  }
}

void FeatureExtractor::Preprocess(const cv::Mat &image) {
  // resize image
  const edk::ShapeEx& in_shape = model_->InputShape(0);
  cv::Mat image_resized;
  if (image.rows != static_cast<int>(in_shape.H()) || image.cols != static_cast<int>(in_shape.W())) {
    cv::resize(image, image_resized, cv::Size(in_shape.W(), in_shape.H()));
  } else {
    image_resized = image;
  }

  // convert data type to float 32
  cv::Mat image_float;
  image_resized.convertTo(image_float, CV_32FC3);

  cv::Mat image_normalized(in_shape.H(), in_shape.W(), CV_32FC3, reinterpret_cast<float *>(input_cpu_ptr_[0]));
  cv::divide(image_float, 255.0, image_normalized);
}
