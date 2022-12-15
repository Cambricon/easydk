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
#ifndef MPS_SERVICE_IMPL_VOUT_HPP_
#define MPS_SERVICE_IMPL_VOUT_HPP_

#include <mutex>
#include "../mps_service.hpp"
#include "mps_internal/cnsample_comm.h"

namespace cnedk {

// vout (vpps + vo)
class MpsVout : private NonCopyable {
 public:
  explicit MpsVout(IVBInfo *vb_info) : vb_info_(vb_info) {}
  ~MpsVout() {}

  int Config(const MpsServiceConfig &config);
  int Init();
  void Destroy();

  int GetVoutSize(int *w, int *h);
  int VoutSendFrame(cnVideoFrameInfo_t *info);

 private:
  cnS32_t VoutInit();
  cnS32_t VoutInitVo();
  cnS32_t VoutInitVpps();

 private:
  IVBInfo *vb_info_ = nullptr;
  MpsServiceConfig mps_config_;
  bool enable_ = false;
  struct {
    cnsampleVoConfig_t vo_config;
    cnEnPicSize_t en_vo_pic_size = PIC_720P;
    cnvoEnIntfSync_t en_intf_sync = VO_OUTPUT_720P60;
    cnEnPixelFormat_t en_pix_format = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
    cnEnVideoFormat_t en_video_format = VIDEO_FORMAT_LINEAR;
    cnEnCompressMode_t en_compress_mode = COMPRESS_MODE_NONE;
    bool init = false;
    vppsGrp_t vpps_grp = kVppsVoutBase;
    cnBool_t ab_chn_enable[VPPS_MAX_PHY_CHN_NUM] = {CN_TRUE, CN_FALSE, CN_FALSE, CN_FALSE};
    std::mutex mutex;
  } vout_;
};

}  // namespace cnedk

#endif  // MPS_SERVICE_IMPL_VOUT_HPP_
