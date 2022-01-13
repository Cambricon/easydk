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

#ifndef EDK_SAMPLES_FEATURE_EXTRACTOR_H_
#define EDK_SAMPLES_FEATURE_EXTRACTOR_H_

#include <opencv2/core/core.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "easytrack/easy_track.h"
#include "easycodec/vformat.h"

class FeatureExtractor {
 public:
  ~FeatureExtractor();
  bool Init(const std::string& model_path, const std::string& func_name, int dev_id = 0);
  void Destroy();

  /*************************************************************************
   * @brief inference and extract feature of an object on mlu
   * @param
   *   frame[in] frame on mlu
   *   objs[in] detected objects of the frame
   * @return returns true if extract successfully, otherwise returns false.
   * ***********************************************************************/
  bool ExtractFeatureOnMlu(const edk::CnFrame& frame, std::vector<edk::DetectObject>* objs);
  /*************************************************************************
   * @brief inference and extract feature of an object on cpu
   * @param
   *   frame[in] frame on cpu
   *   objs[in] detected objects of the frame
   * @return returns true if extract successfully, otherwise returns false.
   * ***********************************************************************/
  bool ExtractFeatureOnCpu(const cv::Mat& frame, std::vector<edk::DetectObject>* obj);
  bool OnMlu() { return extract_feature_mlu_; }

 private:
  void Preprocess(const cv::Mat& img);

  std::unique_ptr<infer_server::InferServer> infer_server_;
  infer_server::Session_t session_;
  int device_id_ = 0;
  bool extract_feature_mlu_ = false;
};  // class FeatureExtractor

#endif  //  EDK_SAMPLES_FEATURE_EXTRACTOR_H_
