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
#include "mps_service_impl_vin.hpp"

#include <mutex>

#include "glog/logging.h"


// #define CN_PQ_COMMON_CFG_PATH "/mps/bin/cnisp-tool/"
// extern cnS32_t cnTuningServerInit(const cnChar_t *ps8CfgPath);
// extern cnS32_t cnTuningServerExit();

namespace cnedk {

int MpsVin::Config(const MpsServiceConfig &config) {
  // check config, TODO(gaoyujia)
  mps_config_ = config;

  // generate VB info for vins
  int i = 0;
  vin_num_ = 0;
  for (auto &it : mps_config_.vins) {
    int sensor_id = it.first;
    VinParam param = it.second;
    vin_[i].sensor_id = sensor_id;
    vin_[i].w = 0;
    vin_[i].h = 0;
    cnEnPicSize sensor_pic_size;
    if (cnsampleCommViGetSizeBySensor(static_cast<cnEnSampleSnsType_t>(param.sensor_type), &sensor_pic_size) ==
        CN_SUCCESS) {
      if (PIC_3840x2173 == sensor_pic_size) {
        sensor_pic_size = PIC_3840x2160;  // //dev crop to PIC_3840x2160
      }
      vin_[i].size = sensor_pic_size;
      cnSize_t vin_size;
      if (cnsampleCommSysGetPicSize(sensor_pic_size, &vin_size) == CN_SUCCESS) {
        vin_[i].w = static_cast<int>(vin_size.u32Width);
        vin_[i].h = static_cast<int>(vin_size.u32Height);
        cnU64_t blk_size = commGetRawBufferSize(vin_size.u32Width, vin_size.u32Height, PIXEL_FORMAT_RAW12_1F,
                                                COMPRESS_MODE_NONE, DEFAULT_ALIGN);
        if (vb_info_) {
          vb_info_->OnVBInfo(blk_size, 8);  // how many buffers needed ? FIXME
        }

        if (param.out_height <= 0 || param.out_width <= 0) {
          vin_[i].skip_vpps = true;
        } else {
          // vpps not skipped, pre-alloc vbs,
          //   width 64 bytes-aligned
          //   height 16 bytes-aligned
          vin_[i].w = param.out_width;
          vin_[i].h = param.out_height;
          vin_[i].skip_vpps = false;
          cnU64_t blk_size = ((vin_[i].w + 63) / 64 * 64) * ((vin_[i].h + 15) / 16 * 16) * 3 / 2;
          if (vb_info_) {
            vb_info_->OnVBInfo(blk_size, 10);
          }
        }
        vin_[i].enable = true;
        ++vin_num_;
      }
    }
    if (!vin_[i].w || !vin_[i].h) {
       LOG(ERROR) << "[EasyDK] [MpsVin] Config(): Invalid sensor type: " << param.sensor_type;
      continue;
    }
    ++i;
    if (i >= kMaxVinNum) {
       LOG(ERROR) << "[EasyDK] [MpsVin] Config(): The number of inputs cannot exceed " << kMaxVinNum;
      break;
    }
  }
  VLOG(2) << "[EasyDK] [MpsVin] Config(): Done, vin_num = " << vin_num_;
  return 0;
}

int MpsVin::Init() {
  cnS32_t ret = InitVin();
  if (ret != CN_SUCCESS) {
     LOG(ERROR) << "[EasyDK] [MpsVin] Init(): InitVin failed, ret = " << ret;
    return -1;
  }
  for (int i = 0; i < vin_num_; i++) {
    if (vin_[i].skip_vpps) continue;
    ret = InitVpps(i);
    if (ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsVin] Init(): InitVpps failed, ret = " << ret;
      return -1;
    }
  }
  for (int i = 0; i < vin_num_; i++) {
    if (vin_[i].skip_vpps) continue;
    vppsGrp_t vpps_grp = KVppsVinBase + i;
    cnsampleCommViBindVpps(i, 0, vpps_grp);
  }
  // cnTuningServerInit(CN_PQ_COMMON_CFG_PATH);
  VLOG(2) << "[EasyDK] [MpsVin] Init(): Done";
  return 0;
}

void MpsVin::Destroy() {
  if (vin_num_) {
    for (int i = 0; i < vin_num_; i++) {
      if (vin_[i].skip_vpps) continue;
      vppsGrp_t vpps_grp = KVppsVinBase + i;
      cnsampleCommViUnBindVpps(i, 0, vpps_grp);
    }
    for (int i = 0; i < vin_num_; i++) {
      if (vin_[i].skip_vpps) continue;
      vppsGrp_t vpps_grp = KVppsVinBase + i;
      vppsChn_t vpps_chn = 0;
      cnvppsDisableChn(vpps_grp, vpps_chn);
      cnvppsStopGrp(vpps_grp);
      cnvppsDestroyGrp(vpps_grp);
    }
    cnsampleCommViStopVi(&vi_config_);

    vin_num_ = 0;
  }
  // cnTuningServerExit();
  VLOG(2) << "[EasyDK] [MpsVin] Destroy(): Done";
}

// vins (vin + vpps)
int MpsVin::GetVinOutSize(int sensor_id, int *w, int *h) {
  int id = GetVinId(sensor_id);
  if (id < 0) {
    LOG(ERROR) << "[EasyDK] [MpsVin] GetVinOutSize(): Invalid vin id";
    return -1;
  }
  *w = vin_[id].w;
  *h = vin_[id].h;
  return 0;
}

int MpsVin::VinCaptureFrame(int sensor_id, cnVideoFrameInfo_t *info, int timeout_ms) {
  int id = GetVinId(sensor_id);
  if (id < 0) {
    LOG(ERROR) << "[EasyDK] [MpsVin] VinCaptureFrame(): Invalid vin id";
    return -1;
  }
  if (vin_[id].skip_vpps) {
    cnS32_t ret = cnviGetChnFrame(id, 0, info, timeout_ms);
    if (ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsVin] VinCaptureFrame(): cnviGetChnFrame failed, ret = " << ret
                 << ", vin id = " << id;
      return -1;
    }
  } else {
    vppsGrp_t vpps_grp = KVppsVinBase + id;
    cnS32_t ret = cnvppsGetChnFrame(vpps_grp, 0, info, timeout_ms);
    if (ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsVin] VinCaptureFrame(): cnvppsGetChnFrame failed, ret = " << ret;
      return -1;
    }
  }
  return 0;
}

int MpsVin::VinCaptureFrameRelease(int sensor_id, cnVideoFrameInfo_t *info) {
  int id = GetVinId(sensor_id);
  if (id < 0) {
    LOG(ERROR) << "[EasyDK] [MpsVin] VinCaptureFrameRelease(): Invalid vin id";
    return -1;
  }
  if (vin_[id].skip_vpps) {
    cnS32_t ret = cnviReleaseChnFrame(id, 0, info);
    if (ret != CN_SUCCESS) {
      LOG(ERROR) << "[EasyDK] [MpsVin] VinCaptureFrameRelease(): cnviReleaseChnFrame failed, ret = " << ret;
      return -1;
    }
  } else {
    vppsGrp_t vpps_grp = KVppsVinBase + id;
    cnvppsReleaseChnFrame(vpps_grp, 0, info);
  }
  return 0;
}

cnS32_t MpsVin::InitVin() {
  cnS32_t ret = CN_SUCCESS;
  vi_config_.s32WorkingViNum = vin_num_;

  for (int i = 0; i < vin_num_; i++) {
    cnU32_t sns_id = vin_[i].sensor_id;
    vi_config_.as32WorkingViId[i] = sns_id;
    vi_config_.astViInfo[sns_id].stSnsInfo.enSnsType =
        static_cast<cnEnSampleSnsType_t>(mps_config_.vins[sns_id].sensor_type);
    vi_config_.astViInfo[sns_id].stSnsInfo.MipiDev = mps_config_.vins[sns_id].mipi_dev;
    vi_config_.astViInfo[sns_id].stDevInfo.ViDev = vi_config_.astViInfo[sns_id].stSnsInfo.MipiDev;
    vi_config_.astViInfo[sns_id].stSnsInfo.s32SnsClkId = mps_config_.vins[sns_id].sns_clk_id;
    vi_config_.astViInfo[sns_id].stSnsInfo.s32BusId = mps_config_.vins[sns_id].bus_id;

    cnsampleCommViGetClockBySensor(vi_config_.astViInfo[sns_id].stSnsInfo.enSnsType,
                                   &vi_config_.astViInfo[sns_id].stSnsInfo.enSnsClkFreq);

    cnsampleCommViGetWdrModeBySensor(vi_config_.astViInfo[sns_id].stSnsInfo.enSnsType,
                                     &vi_config_.astViInfo[sns_id].stDevInfo.enWDRMode);

    vi_config_.astViInfo[sns_id].stPipeInfo.enMastPipeMode = VI_OFFLINE_VPPS_OFFLINE;
    vi_config_.astViInfo[sns_id].stPipeInfo.pipe = i;
    vi_config_.astViInfo[sns_id].stChnInfo.ViChn = 0;
    vi_config_.astViInfo[sns_id].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
    vi_config_.astViInfo[sns_id].stChnInfo.enVideoFormat = VIDEO_FORMAT_LINEAR;
    vi_config_.astViInfo[sns_id].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;

    /*
      if (DIS_MODE_DOF_BUTT != g_enDisMode) {
        ret = cnsampleVioSetDis();
        if (CN_SUCCESS != ret) {
          std::cout << "set dis failed!\n";
          goto err_exit;
        }
      }

      if (CN_TRUE == g_bLdcSpreadEnable) {
        ret = cnsampleVioSetLdc();
        if (CN_SUCCESS != ret) {
          std::cout << "set LDC failed!\n";
          goto err_exit;
        }
        ret = cnsampleVioSetSpread();
        if (CN_SUCCESS != ret) {
          std::cout << "set spread failed!\n";
          goto err_exit;
        }
      }
  */
  }

  ret = cnsampleCommViStartVi(&vi_config_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [MpsVin] InitVin(): start vi failed";
  }

  return ret;
}

cnS32_t MpsVin::InitVpps(int i) {
  cnSize_t size;
  cnS32_t ret = cnsampleCommSysGetPicSize(vin_[i].size, &size);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("cnsampleCommSysGetPicSize failed with %d!\n", ret);
    return CN_FAILURE;
  }

  vppsGrp_t vpps_grp = KVppsVinBase + i;
  vppsChn_t vpps_chn = 0;

  cnvppsGrpAttr_t stVppsGrpAttr;
  memset(&stVppsGrpAttr, 0, sizeof(stVppsGrpAttr));
  stVppsGrpAttr.u32MaxW = size.u32Width;
  stVppsGrpAttr.u32MaxH = size.u32Height;
  stVppsGrpAttr.enPixelFormat = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
  stVppsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
  stVppsGrpAttr.stFrameRate.s32DstFrameRate = -1;
  ret = cnvppsCreateGrp(vpps_grp, &stVppsGrpAttr);
  if (ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsVin] InitVpps(): cnvppsCreateGrp failed, ret = "  << ret;
    return CN_FAILURE;
  }

  cnvppsChnAttr_t stVppsChnAttr;
  memset(&stVppsChnAttr, 0, sizeof(stVppsChnAttr));
  stVppsChnAttr.u32Width = 1920;
  stVppsChnAttr.u32Height = 1080;
  stVppsChnAttr.enChnMode = VPPS_CHN_MODE_USER;
  stVppsChnAttr.enCompressMode = COMPRESS_MODE_NONE;
  stVppsChnAttr.enPixelFormat = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
  stVppsChnAttr.enVideoFormat = VIDEO_FORMAT_LINEAR;
  stVppsChnAttr.stFrameRate.s32SrcFrameRate = -1;
  stVppsChnAttr.stFrameRate.s32DstFrameRate = -1;
  stVppsChnAttr.u32Depth = 4;  // (0,8)
  stVppsChnAttr.bMirror = CN_FALSE;
  stVppsChnAttr.bFlip = CN_FALSE;
  stVppsChnAttr.stAspectRatio.enMode = ASPECT_RATIO_NONE;

  ret = cnvppsSetChnAttr(vpps_grp, vpps_chn, &stVppsChnAttr);
  if (ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsVin] InitVpps(): cnvppsSetChnAttr failed, ret = " << ret;
    goto err_exit1;
  }

  ret = cnvppsEnableChn(vpps_grp, vpps_chn);
  if (ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsVin] InitVpps(): cnvppsEnableChn failed, ret = " << ret;
    goto err_exit2;
  }

  ret = cnvppsStartGrp(vpps_grp);
  if (ret != CN_SUCCESS) {
    LOG(ERROR) << "[EasyDK] [MpsVin] InitVpps(): cnvppsStartGrp failed, ret = " << ret;
    goto err_exit1;
  }
  return CN_SUCCESS;

err_exit1:
  cnvppsStopGrp(vpps_grp);
  cnvppsDisableChn(vpps_grp, vpps_chn);
err_exit2:
  cnvppsDestroyGrp(vpps_grp);
  return CN_FAILURE;
}
};  // namespace cnedk
