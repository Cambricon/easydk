/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#ifndef MPS_SERVICE_IMPL_VIN_HPP_
#define MPS_SERVICE_IMPL_VIN_HPP_

#include <mutex>
#include "glog/logging.h"

#include "../mps_service.hpp"
#include "mps_internal/cnsample_comm.h"

namespace cnedk {

static const int kMaxVinNum = 8;
class MpsVin : private NonCopyable {
 public:
  explicit MpsVin(IVBInfo *vb_info) : vb_info_(vb_info) {}
  ~MpsVin() { Destroy(); }

  int Config(const MpsServiceConfig &config);
  int Init();
  void Destroy();

  int GetVinOutSize(int sensor_id, int *w, int *h);
  int VinCaptureFrame(int sensor_id, cnVideoFrameInfo_t *info, int timeout_ms);
  int VinCaptureFrameRelease(int sensor, cnVideoFrameInfo_t *info);

 private:
  cnS32_t InitVin();
  cnS32_t InitVpps(int i);
  int GetVinId(int sensor_id) {
    for (int i = 0; i < kMaxVinNum; i++) {
      if (vin_[i].sensor_id == sensor_id) {
        return i;
      }
    }
    LOG(ERROR) << "[EasyDK] [MpsVin] GetVinId(): No available sensor id";
    return -1;
  }

 private:
  IVBInfo *vb_info_ = nullptr;
  MpsServiceConfig mps_config_;
  struct {
    bool enable = false;
    int sensor_id;
    int w;
    int h;
    cnEnPicSize size;
    bool skip_vpps = false;
    std::mutex mutex;
  } vin_[kMaxVinNum];
  int vin_num_ = 0;
  cnsampleViConfig_t vi_config_;
};

}  // namespace cnedk

#endif  // MPS_SERVICE_IMPL_VIN_HPP_
