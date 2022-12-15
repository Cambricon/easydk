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

#include "cnedk_vin_capture.h"

#include <cstring>  // for memset
#include <memory>  // for unique_ptr
#include <mutex>   // for call_once

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_platform.h"
#include "cnedk_vin_capture_impl.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_vin_capture_impl_ce3226.hpp"
#endif
#include "common/utils.hpp"

namespace cnedk {

IVinCapture *CreateVinCapture() {
  int dev_id = -1;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "CreateVinCapture(): failed", nullptr);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(dev_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateVinCapture(): Get platform information failed";
    return nullptr;
  }

// FIXME,
//  1. check prop_name ???
//  2. load so ???
#ifdef PLATFORM_CE3226
  if (info.support_unified_addr) {
    return new VinCaptureCe3226();
  }
#endif
  return nullptr;
}

class VinCaptureService {
 public:
  static VinCaptureService &Instance() {
    static std::once_flag s_flag;
    std::call_once(s_flag, [&] { instance_.reset(new VinCaptureService); });
    return *instance_;
  }

  int Create(void **vin_capture, CnedkVinCaptureCreateParams *params) {
    if (!vin_capture || !params) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Create(): vin capture or params pointer is invalid";
      return -1;
    }
    if (CheckParams(params) < 0) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Create(): Parameters are invalid";
      return -1;
    }
    IVinCapture *capture_ = CreateVinCapture();
    if (!capture_) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Create(): new vin capture failed";
      return -1;
    }
    if (capture_->Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Create(): Create vin capture failed";
      delete capture_;
      return -1;
    }
    *vin_capture = capture_;
    return 0;
  }

  int Destroy(void *vin_capture) {
    if (!vin_capture) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Destroy(): vin capture pointer is invalid";
      return -1;
    }
    IVinCapture *capture_ = static_cast<IVinCapture *>(vin_capture);
    capture_->Destroy();
    delete capture_;
    return 0;
  }

  int Capture(void *vin_capture, int timeout_ms) {
    if (!vin_capture) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] Capture(): vin capture pointer is invalid";
      return -1;
    }
    IVinCapture *capture_ = static_cast<IVinCapture *>(vin_capture);
    return capture_->Capture(timeout_ms);
  }

 private:
  int CheckParams(CnedkVinCaptureCreateParams *params) {
    if (params->OnFrame == nullptr || params->OnError == nullptr || params->GetBufSurf == nullptr) {
      LOG(ERROR) << "[EasyDK] [VinCaptureService] CheckParams(): OnFrame, OnError or GetBufSurf function pointer"
                 << " is invalid";
      return -1;
    }

    // TODO(gaoyujia)
    return 0;
  }

 private:
  VinCaptureService(const VinCaptureService &) = delete;
  VinCaptureService(VinCaptureService &&) = delete;
  VinCaptureService &operator=(const VinCaptureService &) = delete;
  VinCaptureService &operator=(VinCaptureService &&) = delete;
  VinCaptureService() = default;

 private:
  static std::unique_ptr<VinCaptureService> instance_;
};

std::unique_ptr<VinCaptureService> VinCaptureService::instance_;

}  // namespace cnedk

extern "C" {

int CnedkVinCaptureCreate(void **vin_capture, CnedkVinCaptureCreateParams *params) {
  return cnedk::VinCaptureService::Instance().Create(vin_capture, params);
}
int CnedkVinCaptureDestroy(void *vin_capture) { return cnedk::VinCaptureService::Instance().Destroy(vin_capture); }
int CnedkVinCapture(void *vin_capture, int timeout_ms) {
  return cnedk::VinCaptureService::Instance().Capture(vin_capture, timeout_ms);
}
};
