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
#ifndef MPS_SERVICE_IMPL_HPP_
#define MPS_SERVICE_IMPL_HPP_

#include <map>
#include <memory>
#include <mutex>
#include "../mps_service.hpp"
#include "mps_service_impl_vdec.hpp"
#include "mps_service_impl_venc.hpp"
#include "mps_service_impl_vin.hpp"
#include "mps_service_impl_vout.hpp"

namespace cnedk {
class MpsServiceImpl : private NonCopyable, public IVBInfo {
 public:
  MpsServiceImpl() {
    mps_vout_.reset(new MpsVout(this));
    mps_vin_.reset(new MpsVin(this));
    mps_vdec_.reset(new MpsVdec(this));
    mps_venc_.reset(new MpsVenc(this));
  }
  ~MpsServiceImpl() { Destroy(); }

  int Init(const MpsServiceConfig &config);
  void Destroy();
  void OnVBInfo(cnU64_t blkSize, int blkCount) override;

  // vout (vpps0 + vo)
  int GetVoutSize(int *w, int *h) {
    if (mps_vout_) {
      return mps_vout_->GetVoutSize(w, h);
    }
    return -1;
  }
  // send videoFrameInfo to vpps+vo
  int VoutSendFrame(cnVideoFrameInfo_t *info) {
    if (mps_vout_) {
      return mps_vout_->VoutSendFrame(info);
    }
    return -1;
  }

  // vins (vin + vpps)
  int GetVinOutSize(int sensor_id, int *w, int *h) {
    if (mps_vin_) {
      return mps_vin_->GetVinOutSize(sensor_id, w, h);
    }
    return -1;
  }
  int VinCaptureFrame(int sensor_id, cnVideoFrameInfo_t *info, int timeout_ms) {
    if (mps_vin_) {
      return mps_vin_->VinCaptureFrame(sensor_id, info, timeout_ms);
    }
    return -1;
  }
  int VinCaptureFrameRelease(int sensor_id, cnVideoFrameInfo_t *info) {
    if (mps_vin_) {
      return mps_vin_->VinCaptureFrameRelease(sensor_id, info);
    }
    return -1;
  }

  // vdecs (vdec + vpps or vdec only)
  void *CreateVDec(IVDecResult *result, cnEnPayloadType_t type, int max_width, int max_height, int buf_num = 12,
                   cnEnPixelFormat_t pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_VU) {
    if (mps_vdec_) {
      return mps_vdec_->Create(result, type, max_width, max_height, buf_num, pix_fmt);
    }
    return nullptr;
  }
  int DestroyVDec(void *handle) {
    if (mps_vdec_) {
      return mps_vdec_->Destroy(handle);
    }
    return -1;
  }
  int VDecSendStream(void *handle, const cnvdecStream_t *pst_stream, cnS32_t milli_sec) {
    if (mps_vdec_) {
      return mps_vdec_->SendStream(handle, pst_stream, milli_sec);
    }
    return -1;
  }

  int VDecReleaseFrame(void *handle, const cnVideoFrameInfo_t *info) {
    if (mps_vdec_) {
      return mps_vdec_->ReleaseFrame(handle, info);
    }
    return -1;
  }

  // vencs
  void *CreateVEnc(IVEncResult *result, VencCreateParam *params) {
    if (mps_venc_) {
      return mps_venc_->Create(result, params);
    }
    return nullptr;
  }
  int DestroyVEnc(void *handle) {
    if (mps_venc_) {
      return mps_venc_->Destroy(handle);
    }
    return -1;
  }
  int VEncSendFrame(void *handle, const cnVideoFrameInfo_t *pst_frame, cnS32_t milli_sec) {
    if (mps_venc_) {
      return mps_venc_->SendFrame(handle, pst_frame, milli_sec);
    }
    return -1;
  }

 private:
  cnS32_t InitSys();

 private:
  MpsServiceConfig mps_config_;
  std::map<cnU64_t, int> pool_cfgs_;
  std::unique_ptr<MpsVout> mps_vout_ = nullptr;
  std::unique_ptr<MpsVin> mps_vin_ = nullptr;
  std::unique_ptr<MpsVdec> mps_vdec_ = nullptr;
  std::unique_ptr<MpsVenc> mps_venc_ = nullptr;
};

}  // namespace cnedk

#endif  // MPS_SERVICE_IMPL_HPP_
