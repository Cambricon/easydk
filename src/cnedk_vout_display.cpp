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

#include "cnedk_vout_display.h"

#include <cstring>  // for memset
#include <memory>  // for unique_ptr
#include <mutex>   // for call_once

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_platform.h"
#include "cnedk_vout_display_impl.hpp"
#include "common/utils.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_vout_display_impl_ce3226.hpp"
#endif
namespace cnedk {

IVoutDisplay *CreateVoutDisplay() {
  int dev_id = -1;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "CreateVoutDisplay(): failed", nullptr);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(dev_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateVoutDisplay(): Get platform information failed";
    return nullptr;
  }

// FIXME,
//  1. check prop_name ???
//  2. load so ???
#ifdef PLATFORM_CE3226
  if (info.support_unified_addr) {
    return new VoutDisplayCe3226();
  }
#endif
  return nullptr;
}

class VoutDisplayService {
 public:
  static VoutDisplayService &Instance() {
    static std::once_flag s_flag;
    std::call_once(s_flag, [&] { instance_.reset(new VoutDisplayService); });
    return *instance_;
  }

  int Render(CnedkBufSurface *surf) { return vout_->Render(surf); }

 private:
  VoutDisplayService(const VoutDisplayService &) = delete;
  VoutDisplayService(VoutDisplayService &&) = delete;
  VoutDisplayService &operator=(const VoutDisplayService &) = delete;
  VoutDisplayService &operator=(VoutDisplayService &&) = delete;
  VoutDisplayService() { vout_.reset(CreateVoutDisplay()); }

 private:
  std::unique_ptr<IVoutDisplay> vout_ = nullptr;
  static std::unique_ptr<VoutDisplayService> instance_;
};

std::unique_ptr<VoutDisplayService> VoutDisplayService::instance_;

}  // namespace cnedk

extern "C" {

int CnedkVoutRender(CnedkBufSurface *surf) { return cnedk::VoutDisplayService::Instance().Render(surf); }
};
