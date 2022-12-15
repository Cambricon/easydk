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
// for select
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>

#include "glog/logging.h"

#include "cn_api.h"
#include "mps_service_impl_venc.hpp"

namespace cnedk {

cnS32_t cnsampleCommVencGetGopAttr(cnvencEnGopMode_t enGopMode, cnvencGopAttr_t *pstGopAttr) {
  switch (enGopMode) {
    case VENC_GOPMODE_NORMALP:
      pstGopAttr->enGopMode = VENC_GOPMODE_NORMALP;
      pstGopAttr->stNormalP.s32IPQpDelta = 3;
      break;

    case VENC_GOPMODE_SMARTP:
      pstGopAttr->enGopMode = VENC_GOPMODE_SMARTP;
      pstGopAttr->stSmartP.s32BgQpDelta = 7;
      pstGopAttr->stSmartP.s32ViQpDelta = 2;
      pstGopAttr->stSmartP.u32BgInterval = 1200;
      break;

    case VENC_GOPMODE_DUALP:
      pstGopAttr->enGopMode = VENC_GOPMODE_DUALP;
      pstGopAttr->stDualP.u32SPInterval = 4;
      pstGopAttr->stDualP.s32SPQpDelta = 2;
      pstGopAttr->stDualP.s32IPQpDelta = 3;
      break;

    case VENC_GOPMODE_BIPREDB:
      pstGopAttr->enGopMode = VENC_GOPMODE_BIPREDB;
      pstGopAttr->stBipredB.s32BQpDelta = -2;
      pstGopAttr->stBipredB.s32IPQpDelta = 3;
      pstGopAttr->stBipredB.u32BFrmNum = 2;
      break;

    default:
      CNSAMPLE_TRACE("not support the gop mode !\n");
      return CN_FAILURE;
      break;
  }
  return CN_SUCCESS;
}

int MpsVenc::Config(const MpsServiceConfig &config) {
  mps_config_ = config;
  std::unique_lock<std::mutex> lk(id_mutex_);
  for (int i = mps_config_.codec_id_start; i < kMaxMpsVecNum; i++) {
    id_q_.push(i);
  }
  return 0;
}

void *MpsVenc::Create(IVEncResult *result, VencCreateParam *params) {
  // check parameters, todo
  if (!result) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): Result handler is null";
    return nullptr;
  }

  int id = GetId();
  if (id <= 0) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): Get encoder id failed";
    return nullptr;
  }
  VEncCtx &ctx = venc_ctx[id - 1];
  if (ctx.created_) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): Duplicated";
    return reinterpret_cast<void *>(id);
  }

  ctx.result_ = result;
  ctx.venc_chn_ = id - 1;
  memset(&ctx.chn_attr_, 0, sizeof(ctx.chn_attr_));
  ctx.chn_attr_.stVencAttr.enType = params->type;
  ctx.chn_attr_.stVencAttr.u32MaxPicWidth = ALIGN_UP(params->width, 16);
  ctx.chn_attr_.stVencAttr.u32MaxPicHeight = ALIGN_UP(params->height, 16);
  ctx.chn_attr_.stVencAttr.u32PicWidth = params->width;
  ctx.chn_attr_.stVencAttr.u32PicHeight = params->height;

  ctx.chn_attr_.stVencAttr.u32BufSize =
      ALIGN_UP(ALIGN_UP(params->width, 16) * ALIGN_UP(params->height, 16), DEFAULT_ALIGN);

  ctx.chn_attr_.stVencAttr.bByFrame = CN_TRUE;

  cnvencGopAttr_t &stGopAttr = ctx.chn_attr_.stGopAttr;
  cnsampleCommVencGetGopAttr(VENC_GOPMODE_NORMALP, &stGopAttr);

  uint32_t u32FrameRate = static_cast<uint32_t>(params->frame_rate);
  u32FrameRate = u32FrameRate > 1 ? u32FrameRate : 30;
  u32FrameRate = u32FrameRate < 120 ? u32FrameRate : 120;

  if (params->type == PT_H264) {
    cnvencH264Vbr_t &stH264Vbr = ctx.chn_attr_.stRcAttr.stH264Vbr;
    stH264Vbr.u32Gop = 30;
    stH264Vbr.u32StatTime = 1;  // u32StatTime;
    stH264Vbr.u32SrcFrameRate = u32FrameRate;
    stH264Vbr.u32DstFrameRate = (1 << 16) | (u32FrameRate & 0x00FF);

    if (params->bitrate > 0) {
      stH264Vbr.u32MaxBitrate = params->bitrate;
    } else {
      stH264Vbr.u32MaxBitrate = 1024 * 2 + 2048 * u32FrameRate / 30;
    }

    ctx.chn_attr_.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    ctx.chn_attr_.stVencAttr.u32Profile = 1;
  } else if (params->type == PT_H265) {
    cnvencH265Vbr_t &stH265Vbr = ctx.chn_attr_.stRcAttr.stH265Vbr;
    stH265Vbr.u32Gop = 30;
    stH265Vbr.u32StatTime = 1;  // u32StatTime;
    stH265Vbr.u32SrcFrameRate = u32FrameRate;
    stH265Vbr.u32DstFrameRate = (1 << 16) | (u32FrameRate & 0x00FF);

    if (params->bitrate > 0) {
      stH265Vbr.u32MaxBitrate = params->bitrate;
    } else {
      stH265Vbr.u32MaxBitrate = 1024 * 2 + 2048 * u32FrameRate / 30;
    }

    ctx.chn_attr_.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
    ctx.chn_attr_.stVencAttr.u32Profile = 0;
  } else if (params->type == PT_JPEG) {
    ctx.chn_attr_.stVencAttr.u32Profile = 0;
    ctx.chn_attr_.stVencAttr.u32BufSize =
      ALIGN_UP(ALIGN_UP(params->width, 16) * ALIGN_UP(params->height, 16), 4096);
    u32FrameRate = 1;
  } else {
    ReturnId(id);
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): Unsupported codec type: " << params->type;
    return nullptr;
  }
  if (params->type == PT_JPEG) {
    cnvencModParam_t mod_param;
    int ret = cnvencGetModParam(&mod_param);
    if (CN_SUCCESS != ret) {
      ReturnId(id);
      LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencGetModParam failed, ret = " << ret;
      return nullptr;
    }
    mod_param.u32MiniBufMode = 1;

    ret = cnvencSetModParam(&mod_param);
    if (CN_SUCCESS != ret) {
      ReturnId(id);
      LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencSetModParam failed, ret = " << ret;
      return nullptr;
    }
  }

  int ret = cnvencCreateChn(ctx.venc_chn_, &ctx.chn_attr_);
  if (CN_SUCCESS != ret) {
    ReturnId(id);
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencCreateChn failed, ret = " << ret;
    return nullptr;
  }

  if (params->type == PT_JPEG) {
    cnvencJpegParam_t pstJpegParam;
    ret = cnvencGetJpegParam(ctx.venc_chn_, &pstJpegParam);
    if (CN_SUCCESS != ret) {
      ReturnId(id);
      LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencGetJpegParam failed, ret = " << ret;
      return nullptr;
    }

    pstJpegParam.u32Qfactor = 90;
    cnvencSetJpegParam(ctx.venc_chn_, &pstJpegParam);
    if (CN_SUCCESS != ret) {
      ReturnId(id);
      LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencSetJpegParam failed, ret = " << ret;
      return nullptr;
    }
  }

  memset(&ctx.chn_param_, 0, sizeof(ctx.chn_param_));
  ctx.chn_param_.s32RecvPicNum = -1;
  ret = cnvencStartRecvFrame(ctx.venc_chn_, &ctx.chn_param_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Create(): cnvencStartRecvFrame failed, ret = " << ret;
    goto err_exit;
  }

  ctx.fd_ = cnvencGetFd(ctx.venc_chn_);
  ctx.eos_sent_ = false;
  ctx.error_flag_ = false;
  ctx.created_ = true;
  VLOG(2) << "[EasyDK] [MpsVenc] Create(): Done";
  return reinterpret_cast<void *>(id);

err_exit:
  cnvencDestroyChn(ctx.venc_chn_);
  ReturnId(id);
  return nullptr;
}

int MpsVenc::Destroy(void *handle) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  if (id <= 0 || id > kMaxMpsVecNum) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Destroy(): Handle is invalid";
    return -1;
  }

  VEncCtx &ctx = venc_ctx[id - 1];
  if (!ctx.created_) {
    LOG(INFO) << "[EasyDK] [MpsVenc] Destroy(): Handle is not created";
    return 0;
  }

  // if error happened, destroy directly, eos maybe not be transmitted from the encoder
  if (!ctx.error_flag_ && !ctx.eos_sent_) {
    SendFrame(handle, nullptr, 0);
  }

  int codec_ret = cnvencStopRecvFrame(ctx.venc_chn_);
  if (CN_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Destroy(): cnvencStopRecvFrame failed, ret = " << codec_ret;
  }

  codec_ret = cnvencDestroyChn(ctx.venc_chn_);
  if (CN_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] Destroy(): cnvencDestroyChn failed, ret = " << codec_ret;
  }
  ctx.venc_chn_ = -1;
  ctx.fd_ = -1;
  ctx.eos_sent_ = false;
  ctx.error_flag_ = false;
  ctx.created_ = false;
  ReturnId(id);
  return 0;
}

int MpsVenc::CheckHandleEos(void *handle) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  VEncCtx &ctx = venc_ctx[id - 1];
  cnvencChnStatus stStatus;
  memset(&stStatus, 0, sizeof(stStatus));
  if (cnvencQueryStatus(ctx.venc_chn_, &stStatus) != CN_SUCCESS) {
    ctx.error_flag_ = true;
    if (ctx.result_) {
      ctx.result_->OnError(CN_ERROR_UNKNOWN);
    }
    LOG(ERROR) << "[EasyDK] [MpsVenc] CheckHandleEos(): cnvencQueryStatus failed";
    return -1;
  }
  if (0 == stStatus.u32LeftPics && stStatus.u32LeftStreamFrames == 0 && stStatus.u32LeftStreamBytes == 0) {
    if (ctx.result_) {
      ctx.result_->OnEos();
    }
    return 0;
  }
  return 1;
}

void MpsVenc::OnFrameBits(void *handle, cnvencStream_t *pst_stream) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  VEncCtx &ctx = venc_ctx[id - 1];
  if (!ctx.result_) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] OnFrameBits(): Result handler is null";
    return;
  }
  VLOG(5) << "[EasyDK] [MpsVenc] OnFrameBits(): Packet count = " << pst_stream->u32PacketCount;
  for (unsigned i = 0; i < pst_stream->u32PacketCount; i++) {
    VEncFrameBits framebits;
    cnvencPack_t *pkt = &pst_stream->pstPacket[i];
    framebits.bits = pkt->pu8Addr + pkt->u32Offset;
    framebits.len = pkt->u32Len - pkt->u32Offset;
    // FIXME
    if (pkt->DataType.enH264EType == H264E_NALU_SPS) {
      framebits.pkt_type = CNEDK_VENC_PACKAGE_TYPE_SPS;
    } else if (pkt->DataType.enH264EType == H264E_NALU_PPS) {
      framebits.pkt_type = CNEDK_VENC_PACKAGE_TYPE_PPS;
    } else if (pkt->DataType.enH264EType == H264E_NALU_IDRSLICE) {
      framebits.pkt_type = CNEDK_VENC_PACKAGE_TYPE_KEY_FRAME;
    } else {
      framebits.pkt_type = CNEDK_VENC_PACKAGE_TYPE_FRAME;
    }
    framebits.pts = pkt->u64PTS;
    ctx.result_->OnFrameBits(&framebits);
  }
}

int MpsVenc::SendFrame(void *handle, const cnVideoFrameInfo_t *pst_frame, cnS32_t milli_sec) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  if (id <= 0 || id > kMaxMpsVecNum) {
    LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): Handle is invalid";
    return -1;
  }
  VLOG(5) << "[EasyDK] [MpsVenc] SendFrame(): Frame width: "<< pst_frame->stVFrame.u32Width << ", height: "
          << pst_frame->stVFrame.u32Height << ", stride:" << pst_frame->stVFrame.u32Stride[0];

  VEncCtx &ctx = venc_ctx[id - 1];
  if (!ctx.created_) {
    LOG(INFO) << "[EasyDK] [MpsVenc] SendFrame(): Handle is not created";
    return -1;
  }

  bool send_done = false;
  if (nullptr == pst_frame) {
    VLOG(2) << "[EasyDK] [MpsVenc] SendFrame(): Send EOS";
    if (ctx.eos_sent_) send_done = true;
    ctx.eos_sent_ = true;
  } else {
    if (ctx.eos_sent_) {
      LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): EOS has been sent, process frameInfo failed, pts:"
                 << pst_frame->stVFrame.u64PTS;
      return -1;
    }
    if (ctx.error_flag_) {
      LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): Error occurred in encoder, process frameInfo failed, pts:"
                 << pst_frame->stVFrame.u64PTS;
      return -1;
    }
  }

  int count = 4;  // FIXME
  while (!ctx.error_flag_) {
    // try send frame_info
    if (!send_done) {
      if (!pst_frame) {
        send_done = true;
        continue;
      }
      int ret = cnvencSendFrame(ctx.venc_chn_, pst_frame, 1000);
      if (ret == CN_SUCCESS) {
        VLOG(5) << "[EasyDK] [MpsVenc] SendFrame(): cnvencSendFrame pts = " << pst_frame->stVFrame.u64PTS;
        send_done = true;
        continue;
      }
      if (--count < 0) {
        ctx.error_flag_ = true;
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): Send frame timeout";
        if (ctx.result_) {
          ctx.result_->OnError(CN_ERROR_UNKNOWN);
        }
        return -1;
      }
    }

    // try get bitstream:
    //  1. polling
    //  2. get bitstream and handle it
    while (1) {
      struct timeval timeout;
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(ctx.fd_, &read_fds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 1000;
      int value = select(ctx.fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
      if (value < 0) {
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): select failed";
        return -1;
      }
      if (value == 0) {
        // LOGE(SOURCE) << "[" << stream_id_ << "]: " << "select timeout\n";
        if (ctx.eos_sent_ && send_done) {
          int value = CheckHandleEos(handle);
          if (value < 0) {
            return -1;
          } else if (value == 0) {
            return 0;
          }
          // going on ..
          continue;
        }
        break;
      }
      if (!FD_ISSET(ctx.fd_, &read_fds)) {
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): FD_ISSET unexpected";
        return -1;
      }

      cnvencChnStatus_t stStat;
      // get picture and process it
      int ret = cnvencQueryStatus(ctx.venc_chn_, &stStat);
      if (CN_SUCCESS != ret) {
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): cnvencQueryStatus chn[" << ctx.venc_chn_ << " failed, ret = "
                   << ret;
        return -1;
      }
      if (0 == stStat.u32CurPacks) {
        continue;
      }
      cnvencStream_t stStream;
      memset(&stStream, 0, sizeof(stStream));
      bool pack_allocated_dynamically = false;
      if (stStat.u32CurPacks <= kMaxMpsVencPackNum) {
        stStream.pstPacket = &ctx.pack_[0];
      } else {
        stStream.pstPacket = reinterpret_cast<cnvencPack_t *>(malloc(sizeof(cnvencPack_t) * stStat.u32CurPacks));
        pack_allocated_dynamically = true;
      }
      if (NULL == stStream.pstPacket) {
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): Malloc stream pack failed";
        return -1;
      }
      stStream.u32PacketCount = stStat.u32CurPacks;
      ret = cnvencGetStream(ctx.venc_chn_, &stStream, 0);
      if (ret == CN_SUCCESS) {
        OnFrameBits(handle, &stStream);
        cnvencReleaseStream(ctx.venc_chn_, &stStream);
        if (pack_allocated_dynamically) free(stStream.pstPacket);
        if (ctx.eos_sent_ && send_done) {
          int value = CheckHandleEos(handle);
          if (value < 0) {
            LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): CheckHandleEos failed";
            return -1;
          } else if (value == 0) {
            return 0;
          }
          // going on ...
        }
      } else {
        if (pack_allocated_dynamically) free(stStream.pstPacket);
        ctx.error_flag_ = true;
        LOG(ERROR) << "[EasyDK] [MpsVenc] SendFrame(): cnvencGetStream failed, ret = " << ret;
        if (ctx.result_) {
          ctx.result_->OnError(ret);
        }
        return -1;
      }
    }

    if (!ctx.eos_sent_ && send_done) {
      break;
    }
  }
  return 0;
}

}  // namespace cnedk
