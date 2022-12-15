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

#ifndef CNEDK_TRANSFORM_IMPL_CE3226_HPP_
#define CNEDK_TRANSFORM_IMPL_CE3226_HPP_

#include <algorithm>
#include <cstring>  // for memset
#include "../cnedk_transform_impl.hpp"
#include "cnedk_transform.h"

namespace cnedk {

class TransformerCe3226 : public ITransformer {
 public:
  TransformerCe3226() {
    if (config_params_.compute_mode == CNEDK_TRANSFORM_COMPUTE_DEFAULT) {
      params_.compute_mode = CNEDK_TRANSFORM_COMPUTE_VGU;  // for CE3226
    } else {
      params_.compute_mode = config_params_.compute_mode;
    }
  }
  ~TransformerCe3226() = default;
  int Transform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) override;

 private:
  int TransformHw(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params);

 private:
  CnedkTransformConfigParams params_;
};

}  // namespace cnedk

#endif  // CNEDK_TRANSFORM_IMPL_CE3226_HPP_
