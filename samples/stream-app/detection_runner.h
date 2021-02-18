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

#ifndef EDK_SAMPLES_STREAM_APP_DETECTION_RUNNER_H_
#define EDK_SAMPLES_STREAM_APP_DETECTION_RUNNER_H_

#include <memory>
#include <string>

#include "cnosd.h"
#include "cnpostproc.h"
#include "device/mlu_context.h"
#include "easybang/resize_and_colorcvt.h"
#include "easycodec/easy_decode.h"
#include "easyinfer/easy_infer.h"
#include "easyinfer/mlu_memory_op.h"
#include "easyinfer/model_loader.h"
#include "easytrack/easy_track.h"
#include "feature_extractor.h"
#include "runner.h"

class DetectionRunner : public StreamRunner {
 public:
  DetectionRunner(const std::string& model_path, const std::string& func_name, const std::string& label_path,
                  const std::string& track_model_path, const std::string& track_func_name,
                  const std::string& data_path, const std::string& net_type, bool show, bool save_video);
  ~DetectionRunner();

  void Process(edk::CnFrame frame) override;

 private:
  edk::MluMemoryOp mem_op_;
  edk::EasyInfer infer_;
  edk::MluResizeConvertOp rc_op_;
  CnOsd osd_;
  std::shared_ptr<edk::ModelLoader> model_{nullptr};
  std::unique_ptr<FeatureExtractor> feature_extractor_{nullptr};
  std::unique_ptr<edk::CnPostproc> postproc_{nullptr};
  std::unique_ptr<edk::FeatureMatchTrack> tracker_{nullptr};
  std::unique_ptr<cv::VideoWriter> video_writer_{nullptr};
  void **mlu_output_{nullptr}, **cpu_output_{nullptr}, **mlu_input_{nullptr};

  bool show_;
  bool save_video_;
};

#endif  // EDK_SAMPLES_STREAM_APP_DETECTION_RUNNER_H_

