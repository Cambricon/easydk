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

#ifndef CNEDK_TRANSFORM_IMPL_HPP_
#define CNEDK_TRANSFORM_IMPL_HPP_

#include <algorithm>
#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_transform.h"
#include "common/utils.hpp"

namespace cnedk {

class ITransformer {
 public:
  virtual ~ITransformer() {}
  virtual int SetSessionParams(CnedkTransformConfigParams *config_params) {
    config_params_.compute_mode = config_params->compute_mode;
    config_params_.device_id = config_params->device_id;
    if (config_params->cnrt_queue) {
      if (config_params_.cnrt_queue == config_params->cnrt_queue) {
        return 0;
      }
      cnrtQueueDestroy(config_params_.cnrt_queue);
      config_params_.cnrt_queue = config_params->cnrt_queue;
    }
    return 0;
  }

  virtual int GetSessionParams(CnedkTransformConfigParams *config_params) {
    if (config_params_.cnrt_queue == nullptr) {
      CNRT_SAFECALL(cnrtQueueCreate(&config_params_.cnrt_queue), "[ITransformer] GetSessionParams(): failed", -1);
    }
    *config_params = config_params_;
    return 0;
  }

  virtual int Transform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) = 0;

 protected:
  static thread_local CnedkTransformConfigParams config_params_;
};

ITransformer *CreateTransformer();

}  // namespace cnedk

#endif  // CNEDK_TRANSFORM_IMPL_HPP_
