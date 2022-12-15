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

#include "cnedk_transform.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

#include "cnrt.h"

#include "cnedk_platform.h"
#include "cnedk_transform_impl.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_transform_impl_ce3226.hpp"
#endif

#ifdef PLATFORM_MLU370
#include "mlu370/cnedk_transform_impl_mlu370.hpp"
#endif

#ifdef PLATFORM_MLU590
#include "mlu590/cnedk_transform_impl_mlu590.hpp"
#endif

#include "common/utils.hpp"

namespace cnedk {

thread_local CnedkTransformConfigParams ITransformer::config_params_;

ITransformer *CreateTransformer() {
  int dev_id = -1;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "CreateTransformer(): failed", nullptr);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(dev_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateTransformer(): Get platform information failed";
    return nullptr;
  }

// FIXME,
//  1. check prop_name ???
//  2. load so ???
#ifdef PLATFORM_CE3226
  if (info.support_unified_addr) {
    return new TransformerCe3226();
  }
#endif

#ifdef PLATFORM_MLU370
  return new TransformerMlu370();
#endif

#ifdef PLATFORM_MLU590
  return new TransformerMlu590();
#endif

  return nullptr;
}

class TransformService {
 public:
  static TransformService &Instance() {
    static std::once_flag s_flag;
    std::call_once(s_flag, [&] { instance_.reset(new TransformService); });
    return *instance_;
  }
  ~TransformService() = default;

  int SetSessionParams(CnedkTransformConfigParams *config_params) {
    if (!config_params) {
      LOG(ERROR) << "[EasyDK] [TransformService] SetSessionParams(): Parameters pointer is invalid";
      return -1;
    }
    return transformer_->SetSessionParams(config_params);
  }

  int GetSessionParams(CnedkTransformConfigParams *config_params) {
    if (!config_params) {
      LOG(ERROR) << "[EasyDK] [TransformService] GetSessionParams(): Parameters pointer is invalid";
      return -1;
    }
    return transformer_->GetSessionParams(config_params);
  }

  int Transform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
    if (!dst || !src || !transform_params) {
      LOG(ERROR) << "[EasyDK] [TransformService] Transform(): src, dst BufSurface or parameters pointer is invalid";
      return -1;
    }
    return transformer_->Transform(src, dst, transform_params);
  }

 private:
  TransformService(const TransformService &) = delete;
  TransformService(TransformService &&) = delete;
  TransformService &operator=(const TransformService &) = delete;
  TransformService &operator=(TransformService &&) = delete;
  TransformService() { transformer_.reset(CreateTransformer()); }

 private:
  std::unique_ptr<ITransformer> transformer_ = nullptr;
  static std::unique_ptr<TransformService> instance_;
};

std::unique_ptr<TransformService> TransformService::instance_;

}  // namespace cnedk

extern "C" {

int CnedkTransformSetSessionParams(CnedkTransformConfigParams *config_params) {
  return cnedk::TransformService::Instance().SetSessionParams(config_params);
}

int CnedkTransformGetSessionParams(CnedkTransformConfigParams *config_params) {
  return cnedk::TransformService::Instance().GetSessionParams(config_params);
}

int CnedkTransform(CnedkBufSurface *src, CnedkBufSurface *dst, CnedkTransformParams *transform_params) {
  return cnedk::TransformService::Instance().Transform(src, dst, transform_params);
}

};  // extern "C"
