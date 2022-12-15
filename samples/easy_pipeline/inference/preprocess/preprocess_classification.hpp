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

#ifndef SAMPLE_PRE_PROCESS_CLASSIFICATON_HPP_
#define SAMPLE_PRE_PROCESS_CLASSIFICATON_HPP_
#include <vector>

#include "cnis/processor.h"

#include "glog/logging.h"

class PreprocClassification : public infer_server::IPreproc {
 public:
  PreprocClassification() = default;
  ~PreprocClassification() = default;

 private:
  int OnTensorParams(const infer_server::CnPreprocTensorParams *params) override {
    params_ = *params;
    return 0;
  }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    CnedkTransformParams params;
    memset(&params, 0, sizeof(params));

    CnedkTransformConfigParams config;
    memset(&config, 0, sizeof(config));
    config.compute_mode = CNEDK_TRANSFORM_COMPUTE_MLU;
    CnedkTransformSetSessionParams(&config);

    if (CnedkTransform(src->GetBufSurface(), dst->GetBufSurface(), &params) < 0) {
      LOG(ERROR) << "[EasyDK Samples] [PreprocClassification] OnPreproc(): CnTransform failed";
      return -1;
    }

    return 0;
  }

 private:
  infer_server::CnPreprocTensorParams params_;
};

#endif
