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

#include "cnedk_encode.h"

#include <cstring>  // for memset
#include <memory>  // for unique_ptr
#include <mutex>   // for call_once

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_encode_impl.hpp"
#include "cnedk_platform.h"
#include "common/utils.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_encode_impl_ce3226.hpp"
#endif

#ifdef PLATFORM_MLU370
#include "mlu370/cnedk_encode_impl_mlu370.hpp"
#endif

#ifdef PLATFORM_MLU590
#include "mlu590/cnedk_encode_impl_mlu590.hpp"
#endif

namespace cnedk {

IEncoder *CreateEncoder() {
  int dev_id = -1;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "CreateEncoder(): failed", nullptr);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(dev_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateEncoder(): Get platform information failed";
    return nullptr;
  }

// FIXME,
//  1. check prop_name ???
//  2. load so ???
#ifdef PLATFORM_CE3226
  if (info.support_unified_addr) {
    return new EncoderCe3226();
  }
#endif

#ifdef PLATFORM_MLU370
  return new EncoderMlu370();
#endif

#ifdef PLATFORM_MLU590
  return new EncoderMlu590();
#endif

  return nullptr;
}

class EncodeService {
 public:
  static EncodeService &Instance() {
    static std::once_flag s_flag;
    std::call_once(s_flag, [&] { instance_.reset(new EncodeService); });
    return *instance_;
  }

  int Create(void **venc, CnedkVencCreateParams *params) {
    if (!venc || !params) {
      LOG(ERROR) << "[EasyDK] [EncodeService] Create(): encoder or params pointer is invalid";
      return -1;
    }
    if (CheckParams(params) < 0) {
      LOG(ERROR) << "[EasyDK] [EncodeService] Create(): Parameters are invalid";
      return -1;
    }
    IEncoder *encoder_ = CreateEncoder();
    if (!encoder_) {
      LOG(ERROR) << "[EasyDK] [EncodeService] Create(): new encoder failed";
      return -1;
    }
    if (encoder_->Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] [EncodeService] Create(): Create encoder failed";
      delete encoder_;
      return -1;
    }
    *venc = encoder_;
    return 0;
  }

  int Destroy(void *venc) {
    if (!venc) {
      LOG(ERROR) << "[EasyDK] [EncodeService] Destroy(): Encoder pointer is invalid";
      return -1;
    }
    IEncoder *encoder_ = static_cast<IEncoder *>(venc);
    encoder_->Destroy();
    delete encoder_;
    return 0;
  }

  int SendFrame(void *venc, CnedkBufSurface *surf, int timeout_ms) {
    if (!venc) {
      LOG(ERROR) << "[EasyDK] [EncodeService] SendFrame(): Encoder pointer is invalid";
      return -1;
    }
    IEncoder *encoder_ = static_cast<IEncoder *>(venc);
    return encoder_->SendFrame(surf, timeout_ms);
  }

 private:
  int CheckParams(CnedkVencCreateParams *params) {
    if (params->type <= CNEDK_VENC_TYPE_INVALID || params->type >= CNEDK_VENC_TYPE_NUM) {
      LOG(ERROR) << "[EasyDK] [EncodeService] CheckParams(): Unsupported codec type: " << params->type;
      return -1;
    }

    if (params->OnEos == nullptr || params->OnFrameBits == nullptr || params->OnError == nullptr) {
      LOG(ERROR) << "[EasyDK] [EncodeService] CheckParams(): OnEos, OnFrameBits or OnError function pointer is invalid";
      return -1;
    }

    int dev_id = -1;
    CNRT_SAFECALL(cnrtGetDevice(&dev_id), "[EncodeService] CheckParams(): failed", -1);
    if (params->device_id != dev_id) {
      LOG(ERROR) << "[EasyDK] [EncodeService] CheckParams(): device id of current thread and device id in parameters"
                 << " are different";
      return -1;
    }

    // TODO(gaoyujia)
    return 0;
  }

 private:
  EncodeService(const EncodeService &) = delete;
  EncodeService(EncodeService &&) = delete;
  EncodeService &operator=(const EncodeService &) = delete;
  EncodeService &operator=(EncodeService &&) = delete;
  EncodeService() = default;

 private:
  static std::unique_ptr<EncodeService> instance_;
};

std::unique_ptr<EncodeService> EncodeService::instance_;

}  // namespace cnedk

extern "C" {

int CnedkVencCreate(void **venc, CnedkVencCreateParams *params) {
  return cnedk::EncodeService::Instance().Create(venc, params);
}
int CnedkVencDestroy(void *venc) { return cnedk::EncodeService::Instance().Destroy(venc); }
int CnedkVencSendFrame(void *venc, CnedkBufSurface *surf, int timeout_ms) {
  return cnedk::EncodeService::Instance().SendFrame(venc, surf, timeout_ms);
}
};
