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

#include "cnedk_decode.h"

#include <cstring>  // for memset
#include <memory>  // for unique_ptr
#include <mutex>   // for call_once

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_decode_impl.hpp"
#include "cnedk_platform.h"
#include "common/utils.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_decode_impl_ce3226.hpp"
#endif

#ifdef PLATFORM_MLU370
#include "mlu370/cnedk_decode_impl_mlu370.hpp"
#endif

#ifdef PLATFORM_MLU590
#include "mlu590/cnedk_decode_impl_mlu590.hpp"
#endif


namespace cnedk {

IDecoder *CreateDecoder() {
  int dev_id = -1;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "CreateDecoder(): failed", nullptr);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(dev_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateDecoder(): Get platform information failed";
    return nullptr;
  }

// FIXME,
//  1. check prop_name ???
//  2. load so ???
#ifdef PLATFORM_CE3226
  if (info.support_unified_addr) {
    return new DecoderCe3226();
  }
#endif

#ifdef PLATFORM_MLU370
  return new DecoderMlu370();
#endif

#ifdef PLATFORM_MLU590
  return new DecoderMlu590();
#endif
  return nullptr;
}

class DecodeService {
 public:
  static DecodeService &Instance() {
    static std::once_flag s_flag;
    std::call_once(s_flag, [&] { instance_.reset(new DecodeService); });
    return *instance_;
  }

  int Create(void **vdec, CnedkVdecCreateParams *params) {
    if (!vdec || !params) {
      LOG(ERROR) << "[EasyDK] [DecodeService] Create(): decoder or params pointer is invalid";
      return -1;
    }
    if (CheckParams(params) < 0) {
      LOG(ERROR) << "[EasyDK] [DecodeService] Create(): Parameters are invalid";
      return -1;
    }
    IDecoder *decoder_ = CreateDecoder();
    if (!decoder_) {
      LOG(ERROR) << "[EasyDK] [DecodeService] Create(): new decoder failed";
      return -1;
    }
    if (decoder_->Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] [DecodeService] Create(): Create decoder failed";
      delete decoder_;
      return -1;
    }
    *vdec = decoder_;
    return 0;
  }

  int Destroy(void *vdec) {
    if (!vdec) {
      LOG(ERROR) << "[EasyDK] [DecodeService] Destroy(): Decoder pointer is invalid";
      return -1;
    }
    IDecoder *decoder_ = static_cast<IDecoder *>(vdec);
    decoder_->Destroy();
    delete decoder_;
    return 0;
  }

  int SendStream(void *vdec, const CnedkVdecStream *stream, int timeout_ms) {
    if (!vdec || !stream) {
      LOG(ERROR) << "[EasyDK] [DecodeService] SendStream(): Decoder or stream pointer is invalid";
      return -1;
    }
    IDecoder *decoder_ = static_cast<IDecoder *>(vdec);
    return decoder_->SendStream(stream, timeout_ms);
  }

 private:
  int CheckParams(CnedkVdecCreateParams *params) {
    if (params->type <= CNEDK_VDEC_TYPE_INVALID || params->type >= CNEDK_VDEC_TYPE_NUM) {
      LOG(ERROR) << "[EasyDK] [DecodeService] CheckParams(): Unsupported codec type: " << params->type;
      return -1;
    }

    if (params->color_format != CNEDK_BUF_COLOR_FORMAT_NV12 && params->color_format != CNEDK_BUF_COLOR_FORMAT_NV21) {
      LOG(ERROR) << "[EasyDK] [DecodeService] CheckParams(): Unsupported color format: " << params->color_format;
      return -1;
    }

    if (params->OnEos == nullptr || params->OnFrame == nullptr || params->OnError == nullptr ||
        params->GetBufSurf == nullptr) {
      LOG(ERROR) << "[EasyDK] [DecodeService] CheckParams(): OnEos, OnFrame, OnError or GetBufSurf function pointer"
                 << " is invalid";
      return -1;
    }

    int dev_id = -1;
    CNRT_SAFECALL(cnrtGetDevice(&dev_id), "[DecodeService] CheckParams(): failed", -1);
    if (params->device_id != dev_id) {
      LOG(ERROR) << "[EasyDK] [DecodeService] CheckParams(): device id of current thread and device id in parameters"
                 << " are different";
      return -1;
    }

    // TODO(gaoyujia)
    return 0;
  }

 private:
  DecodeService(const DecodeService &) = delete;
  DecodeService(DecodeService &&) = delete;
  DecodeService &operator=(const DecodeService &) = delete;
  DecodeService &operator=(DecodeService &&) = delete;
  DecodeService() = default;

 private:
  static std::unique_ptr<DecodeService> instance_;
};

std::unique_ptr<DecodeService> DecodeService::instance_;

}  // namespace cnedk

extern "C" {

int CnedkVdecCreate(void **vdec, CnedkVdecCreateParams *params) {
  return cnedk::DecodeService::Instance().Create(vdec, params);
}
int CnedkVdecDestroy(void *vdec) { return cnedk::DecodeService::Instance().Destroy(vdec); }
int CnedkVdecSendStream(void *vdec, const CnedkVdecStream *stream, int timeout_ms) {
  return cnedk::DecodeService::Instance().SendStream(vdec, stream, timeout_ms);
}
};
