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

#ifndef CNEDK_ENCODE_IMPL_CE3226_HPP_
#define CNEDK_ENCODE_IMPL_CE3226_HPP_

#include "../cnedk_encode_impl.hpp"
#include "ce3226_helper.hpp"
#include "mps_service/mps_service.hpp"

namespace cnedk {

class EncoderCe3226 : public IEncoder, public IVEncResult {
 public:
  EncoderCe3226() = default;
  ~EncoderCe3226() {
    if (output_surf_) {
      CnedkBufSurfaceDestroy(output_surf_);
      output_surf_ = nullptr;
    }
    if (surf_pool_) {
      CnedkBufPoolDestroy(surf_pool_);
      surf_pool_ = nullptr;
    }
  }
  // IEncoder
  int Create(CnedkVencCreateParams *params) override;
  int Destroy() override;
  int SendFrame(CnedkBufSurface *surf, int timeout_ms) override;

  // IVEncResult
  void OnFrameBits(VEncFrameBits *frameBits) override;
  void OnEos() override;
  void OnError(cnS32_t errcode) override;

 private:
  void *surf_pool_ = nullptr;
  CnedkVencCreateParams create_params_;
  void *venc_ = nullptr;
  CnedkBufSurface *output_surf_ = nullptr;
};

}  // namespace cnedk

#endif  // CNEDK_ENCODE_IMPL_CE3226_HPP_
