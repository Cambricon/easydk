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

#include "cnedk_transform_impl_mlu370.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>  // for memset
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "cnedk_transform_cncv.hpp"

namespace cnedk {

int TransformerMlu370::Transform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
  if (config_params_.compute_mode != CNEDK_TRANSFORM_COMPUTE_MLU &&
      config_params_.compute_mode != CNEDK_TRANSFORM_COMPUTE_DEFAULT) {
    LOG(ERROR) << "[EasyDK] [TransformerMlu370] Transform(): Unsupported compute mode: " << config_params_.compute_mode;
    return -1;
  }

  if (src->num_filled > dst->batch_size) {
    LOG(ERROR) << "[EasyDK] [TransformerMlu370] Transform(): The number of inputs exceeds batch size: "
               << src->num_filled << " v.s. " << dst->batch_size;
    return -1;
  }

  if (src->mem_type != CNEDK_BUF_MEM_DEVICE || dst->mem_type != CNEDK_BUF_MEM_DEVICE) {
    LOG(ERROR) << "[EasyDK] [TransformerMlu370] Transform(): The src and dst mem_type must be CNEDK_BUF_MEM_DEVICE";
    return -1;
  }

  if (src->surface_list[0].data_size == 0) {
    LOG(ERROR) << "[EasyDK] [TransformerMlu370] Transform(): Input data size is 0";
    return -1;
  }

  return CncvTransform(src, dst, transform_params);
}

}  // namespace cnedk
