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
#include <vector>

#include "cnis/processor.h"
#include "cnis/infer_server.h"
#include "cnosd.h"
#include "stream_runner.h"

struct DetectionFrame {
  CnedkBufSurface* surf;
  std::vector<DetectObject> objs;
};

class DetectionRunner : public StreamRunner, public infer_server::IPreproc, public infer_server::IPostproc {
 public:
  DetectionRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                  const std::string& model_path, const std::string& label_path,
                  const std::string& data_path, bool show, bool save_video);
  ~DetectionRunner();

  void Process(CnedkBufSurface* surf) override;

 private:
  int OnTensorParams(const infer_server::CnPreprocTensorParams *params) override;
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect>& src_rects) override;
  int OnPostproc(const std::vector<infer_server::InferData*>& data_vec, const infer_server::ModelIO& model_output,
                 const infer_server::ModelInfo* model_info) override;

 private:
  cv::Mat ConvertToMatAndReleaseBuf(CnedkBufSurface* surf);
  std::unique_ptr<infer_server::InferServer> infer_server_;
  infer_server::Session_t session_;
  CnOsd osd_;
  std::unique_ptr<cv::VideoWriter> video_writer_{nullptr};

  bool show_;
  bool save_video_;
  float threshold_ = 0.6;
  infer_server::CnPreprocTensorParams params_;
};

#endif  // EDK_SAMPLES_STREAM_APP_DETECTION_RUNNER_H_

