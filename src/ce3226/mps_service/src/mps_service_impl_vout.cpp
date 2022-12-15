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
#include "mps_service_impl_vout.hpp"
#include <mutex>
#include "glog/logging.h"

namespace cnedk {

int MpsVout::Config(const MpsServiceConfig &config) {
  mps_config_ = config;
  enable_ = mps_config_.vout.enable;
  if (!enable_) {
    LOG(INFO) << "[EasyDK] [MpsVout] Config(): Vout is not enabled";
    return 0;
  }

  // generate VB info for Vout
  cnS32_t ret = CN_SUCCESS;
  cnSize_t vo_size;
  ret = cnsampleCommSysGetPicSize(vout_.en_vo_pic_size, &vo_size);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommSysGetPicSize failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsVout] Config(): cnsampleCommSysGetPicSize failed, ret = " << ret;
    return CN_FAILURE;
  }
  VLOG(2) << "[EasyDK] [MpsVout] Config(): vo_size(" << vo_size.u32Width << " x " << vo_size.u32Height << ")";
  cnU64_t blk_size = commGetPicBufferSize(vo_size.u32Width, vo_size.u32Height, PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
                                          DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
  if (vb_info_) {
    vb_info_->OnVBInfo(blk_size, 4);
  }

  // generate VB info for vpps
  if (vb_info_) {
    int max_width = (mps_config_.vout.max_input_width + 63) / 64 * 64;
    int max_height = (mps_config_.vout.max_input_height + 15) / 16 * 16;
    if (mps_config_.vout.input_fmt == PIXEL_FORMAT_YUV420_8BIT_SEMI_UV ||
        mps_config_.vout.input_fmt == PIXEL_FORMAT_YUV420_8BIT_SEMI_VU) {
      blk_size = max_width * max_height * 3 / 2;
    } else if (mps_config_.vout.input_fmt == PIXEL_FORMAT_BGR888_PACKED) {
      blk_size = max_width * max_height * 3;
    } else {
      LOG(ERROR) << "[EasyDK] [MpsVout] Config(): Unsupported input format: " << mps_config_.vout.input_fmt;
      return -1;
    }
    vb_info_->OnVBInfo(blk_size, 4);
  }
  VLOG(2) << "[EasyDK] [MpsVout] Config(): vb block size = " << blk_size;
  return 0;
}

int MpsVout::Init() {
  std::unique_lock<std::mutex> lock(vout_.mutex);
  if (!enable_) {
    LOG(INFO) << "[EasyDK] [MpsVout] Init(): Vout is not enabled";
    return 0;
  }
  cnS32_t ret = VoutInit();
  if (CN_SUCCESS == ret) {
    vout_.init = true;
    return 0;
  }
  LOG(ERROR) << "[EasyDK] [MpsVout] Init(): VoutInit failed";
  return -1;
}

void MpsVout::Destroy() {
  std::unique_lock<std::mutex> lock(vout_.mutex);
  if (!vout_.init) {
    VLOG(1) << "[EasyDK] [MpsVout] Destroy(): Uninitialized";
    return;
  }
  cnsampleCommVppsUnBindVo(vout_.vpps_grp, 0, 0, 0);
  cnsampleCommVoStopVo(&vout_.vo_config);
  cnsampleCommVppsStop(0, vout_.ab_chn_enable);
  VLOG(2) << "[EasyDK] [MpsVout] Destroy(): Done";
  vout_.init = false;
}

int MpsVout::VoutSendFrame(cnVideoFrameInfo_t *info) {
  std::unique_lock<std::mutex> lock(vout_.mutex);
  if (!enable_) {
    LOG(ERROR) << "[EasyDK] [MpsVout] VoutSendFrame(): Vout is not enabled";
    return -1;
  }
  cnS32_t milli_sec = -1;
  if (cnvppsSendFrame(vout_.vpps_grp, 0, info, milli_sec) != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsVout] VoutSendFrame(): cnvppsSendFrame failed";
    return -1;
  }
  return 0;
}

int MpsVout::GetVoutSize(int *w, int *h) {
  if (!enable_) {
    LOG(ERROR) << "[EasyDK] [MpsVout] GetVoutSize(): Vout is not enabled";
    return -1;
  }
  cnSize_t vo_size;
  cnS32_t ret = cnsampleCommSysGetPicSize(vout_.en_vo_pic_size, &vo_size);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommSysGetPicSize failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsVout] GetVoutSize(): cnsampleCommSysGetPicSize failed, ret = " << ret;
    return -1;
  }
  *w = static_cast<int>(vo_size.u32Width);
  *h = static_cast<int>(vo_size.u32Height);
  return 0;
}

cnS32_t MpsVout::VoutInitVo() {
  cnS32_t ret = CN_SUCCESS;
  cnSize_t stSize;

  ret = cnsampleCommSysGetPicSize(vout_.en_vo_pic_size, &stSize);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommSysGetPicSize failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsVout] VoutInitVo(): cnsampleCommSysGetPicSize failed, ret = " << ret;
    return CN_FAILURE;
  }

  memset(&vout_.vo_config, 0, sizeof(vout_.vo_config));
  vout_.vo_config.VoDev = 0;
  vout_.vo_config.enVoIntfType = VO_INTF_LCD_24BIT;
  vout_.vo_config.enIntfSync = vout_.en_intf_sync;
  vout_.vo_config.u32BgColor = 0x0000FF;
  vout_.vo_config.enPixFormat = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
  vout_.vo_config.u32DisBufLen = 3;
  vout_.vo_config.enVoMode = VO_MODE_1MUX;

  vout_.vo_config.stDispRect.s32X = 0;
  vout_.vo_config.stDispRect.s32Y = 0;
  vout_.vo_config.stDispRect.u32Width = stSize.u32Width;
  vout_.vo_config.stDispRect.u32Height = stSize.u32Height;
  return cnsampleCommVoStartVo(&vout_.vo_config);
}

cnS32_t MpsVout::VoutInitVpps() {
  cnS32_t ret = CN_SUCCESS;
  cnSize_t vo_size;

  ret = cnsampleCommSysGetPicSize(vout_.en_vo_pic_size, &vo_size);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommSysGetPicSize failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsVout] VoutInitVpps(): cnsampleCommSysGetPicSize failed, ret = " << ret;
    return CN_FAILURE;
  }

  cnvppsGrpAttr_t vpps_grp_attr;
  memset(&vpps_grp_attr, 0, sizeof(vpps_grp_attr));
  vpps_grp_attr.u32MaxW = mps_config_.vout.max_input_width;
  vpps_grp_attr.u32MaxH = mps_config_.vout.max_input_height;
  vpps_grp_attr.enPixelFormat = mps_config_.vout.input_fmt;
  vpps_grp_attr.stFrameRate.s32SrcFrameRate = -1;
  vpps_grp_attr.stFrameRate.s32DstFrameRate = -1;

  cnvppsChnAttr_t vpps_chn_attr[VPPS_MAX_PHY_CHN_NUM];
  for (int i = 0; i < 1; i++) {
    memset(&vpps_chn_attr[i], 0, sizeof(vpps_chn_attr[i]));
    vpps_chn_attr[i].u32Width = vo_size.u32Width;
    vpps_chn_attr[i].u32Height = vo_size.u32Height;
    vpps_chn_attr[i].enChnMode = VPPS_CHN_MODE_USER;
    vpps_chn_attr[i].enCompressMode = vout_.en_compress_mode;
    vpps_chn_attr[i].enPixelFormat = vout_.vo_config.enPixFormat;
    vpps_chn_attr[i].enVideoFormat = vout_.en_video_format;
    vpps_chn_attr[i].stFrameRate.s32SrcFrameRate = -1;
    vpps_chn_attr[i].stFrameRate.s32DstFrameRate = -1;
    vpps_chn_attr[i].u32Depth = 0;
    vpps_chn_attr[i].bMirror = CN_FALSE;
    vpps_chn_attr[i].bFlip = CN_FALSE;
    vpps_chn_attr[i].stAspectRatio.enMode = ASPECT_RATIO_NONE;
  }

  ret = cnsampleCommVppsStart(vout_.vpps_grp, vout_.ab_chn_enable, &vpps_grp_attr, vpps_chn_attr);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommVppsStart Grp0 failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsVout] VoutInitVpps(): cnsampleCommVppsStart failed, ret = " << ret;
    return CN_FAILURE;
  }
  return CN_SUCCESS;
}

cnS32_t MpsVout::VoutInit() {
  cnS32_t ret = CN_SUCCESS;

  ret = VoutInitVo();
  if (ret) goto EXIT2;

  ret = VoutInitVpps();
  if (ret) goto EXIT1;

  ret = cnsampleCommVppsBindVo(vout_.vpps_grp, 0, 0, 0);
  if (ret) goto EXIT1;

  return CN_SUCCESS;

EXIT1:
  cnsampleCommVppsStop(vout_.vpps_grp, vout_.ab_chn_enable);
EXIT2:
  cnsampleCommVoStopVo(&vout_.vo_config);
  return CN_FAILURE;
}
};  // namespace cnedk
