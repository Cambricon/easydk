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

#ifndef SAMPLE_POST_PROCESS_CLASSIFICATON_HPP_
#define SAMPLE_POST_PROCESS_CLASSIFICATON_HPP_

#include <vector>
#include <memory>

#include "cnis/infer_server.h"
#include "cnis/processor.h"

#include "glog/logging.h"
#include "edk_frame.hpp"


class PostprocClassification : public infer_server::IPostproc {
 public:
  PostprocClassification() = default;
  ~PostprocClassification() = default;

  int OnPostproc(const std::vector<infer_server::InferData*>& data_vec,
                 const infer_server::ModelIO& model_output,
                 const infer_server::ModelInfo* model_info) override {
    cnedk::BufSurfWrapperPtr output = model_output.surfs[0];

    if (!output->GetHostData(0)) {
      LOG(ERROR) << "[EasyDK Samples] [PostprocClassification] Postprocess failed, copy data to host failed.";
      return -1;
    }
    CnedkBufSurfaceSyncForCpu(output->GetBufSurface(), -1, -1);

    auto len = model_info->OutputShape(0).DataCount();
    for (size_t batch_idx = 0; batch_idx < data_vec.size(); ++batch_idx) {
      float *res = static_cast<float*>(output->GetHostData(0, batch_idx));
      auto score_ptr = res;

      float max_score = 0;
      uint32_t label = 0;
      for (decltype(len) i = 0; i < len; ++i) {
        auto score = *(score_ptr + i);
        if (score > max_score) {
          max_score = score;
          label = i;
        }
      }

      std::shared_ptr<EdkFrame> frame = data_vec[batch_idx]->GetUserData<std::shared_ptr<EdkFrame>>();

      DetectObject obj;
      obj.label = label;
      obj.score = max_score;

      frame->objs.push_back(obj);
    }
    return 0;
  }
};

#endif
