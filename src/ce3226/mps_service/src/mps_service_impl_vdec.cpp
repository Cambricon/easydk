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

#include "mps_service_impl_vdec.hpp"

namespace cnedk {

/*
 *  cnvdec has global mode params...hardcode at this moment!!!
 *  FIXME
 */
static void MpsVdecInit() {
  cnvdecModParam_t mod_param_;
  int ret = cnvdecGetModParam(&mod_param_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] MpsVdecInit(): cnvdecGetModParam failed, ret = " << ret;
    return;
  }
  mod_param_.enVdecVBSource = VB_SOURCE_PRIVATE;
  ret = cnvdecSetModParam(&mod_param_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] MpsVdecInit(): cnvdecSetModParam failed, ret = " << ret;
  }
}

int MpsVdec::Config(const MpsServiceConfig &config) {
  mps_config_ = config;

#if 0
  // generate VB info for vdec
  // ...
  if (vb_info_) {
    vb_info_->OnVBInfo(u64BlkSize, 4);
  }
#endif
  MpsVdecInit();
  std::unique_lock<std::mutex> lk(id_mutex_);
  for (int i = mps_config_.codec_id_start; i < kMaxMpsVdecNum; i++) {
    id_q_.push(i);
  }
  return 0;
}

void *MpsVdec::Create(IVDecResult *result, cnEnPayloadType_t type, int max_width, int max_height, int buf_num,
                      cnEnPixelFormat_t pix_fmt) {
  // check parameters, todo
  if (!result) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): Result handler is null";
    return nullptr;
  }
  switch (type) {
    case PT_H264:
    case PT_H265:
    case PT_JPEG:
      break;
    default: {
      LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): Unsupported codec type: " << type;
      return nullptr;
    }
  }
  //
  int id = GetId();
  if (id <= 0) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): Get decoder id failed";
    return nullptr;
  }
  VDecCtx &ctx = vdec_ctx_[id - 1];
  if (ctx.created_) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): Duplicated";
    return reinterpret_cast<void *>(id);
  }
  ctx.result_ = result;
  ctx.vdec_chn_ = id - 1;
  memset(&ctx.chn_attr_, 0, sizeof(ctx.chn_attr_));
  ctx.chn_attr_.enType = type;
  ctx.chn_attr_.enMode = VIDEO_MODE_FRAME;
  ctx.chn_attr_.u32PicWidth = max_width;
  ctx.chn_attr_.u32PicHeight = max_height;
  ctx.chn_attr_.u32StreamBufSize = (ctx.chn_attr_.u32PicWidth * ctx.chn_attr_.u32PicHeight + 1023) / 1024 * 1024;
  ctx.chn_attr_.stVdecVideoAttr.u32RefFrameNum = 0;  // not used
  ctx.chn_attr_.u32FrameBufCnt = buf_num;
  ctx.chn_attr_.u32FrameBufSize =
      cndecGetPictureBufferSize(ctx.chn_attr_.enType, ctx.chn_attr_.u32PicWidth, ctx.chn_attr_.u32PicHeight,
                                PIXEL_FORMAT_YUV420_8BIT_SEMI_UV, DATA_BITWIDTH_8, 16);

  VLOG(2) << "Type: " << ctx.chn_attr_.enType
          << " WxH: " << ctx.chn_attr_.u32PicWidth << "x" << ctx.chn_attr_.u32PicHeight
          << " Stream size: " << ctx.chn_attr_.u32StreamBufSize
          << " Framecnt: " << ctx.chn_attr_.u32FrameBufCnt
          << " RefNum: " << ctx.chn_attr_.stVdecVideoAttr.u32RefFrameNum
          << " FrameBuf Size: " << ctx.chn_attr_.u32FrameBufSize;

  int ret = cnvdecCreateChn(ctx.vdec_chn_, &ctx.chn_attr_);
  if (CN_SUCCESS != ret) {
    ReturnId(id);
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): cnvdecCreateChn failed, ret = " << ret;
    return nullptr;
  }

  ret = cnvdecGetChnParam(ctx.vdec_chn_, &ctx.chn_param_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): cnvdecGetChnParam failed, ret = " << ret;
    goto err_exit;
  }
  if (type == PT_JPEG) {
    ctx.chn_param_.stVdecPictureParam.enPixelFormat = pix_fmt;
  } else {
    ctx.chn_param_.stVdecVideoParam.enDecMode = VIDEO_DEC_MODE_IPB;
    ctx.chn_param_.stVdecVideoParam.enOutputOrder = VIDEO_OUTPUT_ORDER_DISP;
  }
  ctx.chn_param_.u32DisplayFrameNum = 2;
  ret = cnvdecSetChnParam(ctx.vdec_chn_, &ctx.chn_param_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): cnvdecSetChnParam failed, ret = " << ret;
    goto err_exit;
  }

  ret = cnvdecStartRecvStream(ctx.vdec_chn_);
  if (CN_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Create(): cnvdecStartRecvStream failed, ret = " << ret;
    goto err_exit;
  }

  ctx.fd_ = cnvdecGetFd(ctx.vdec_chn_);
  ctx.eos_sent_ = false;
  ctx.error_flag_ = false;
  ctx.created_ = true;
  VLOG(2) << "[EasyDK] [MpsVdec] Create(): Done";
  return reinterpret_cast<void *>(id);

err_exit:
  cnvdecDestroyChn(ctx.vdec_chn_);
  ReturnId(id);
  return nullptr;
}

int MpsVdec::Destroy(void *handle) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  if (id <= 0 || id > kMaxMpsVdecNum) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Destroy(): Handle is invalid";
    return -1;
  }

  VDecCtx &ctx = vdec_ctx_[id - 1];
  if (!ctx.created_) {
    LOG(INFO) << "[EasyDK] [MpsVdec] Destroy(): Handle is not created";
    return 0;
  }

  // if error happened, destroy directly, eos maybe not be transmitted from the decoder
  if (!ctx.error_flag_ && !ctx.eos_sent_) {
    SendStream(handle, nullptr, 0);
  }

  int codec_ret = cnvdecStopRecvStream(ctx.vdec_chn_);
  if (CN_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Destroy(): cnvdecStopRecvStream failed, ret = " << codec_ret;
  }

  codec_ret = cnvdecDestroyChn(ctx.vdec_chn_);
  if (CN_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] Destroy(): cnvdecDestroyChn failed, ret = " << codec_ret;
  }
  ctx.vdec_chn_ = -1;
  ctx.fd_ = -1;
  ctx.eos_sent_ = false;
  ctx.error_flag_ = false;
  ctx.created_ = false;
  ReturnId(id);
  return 0;
}

int MpsVdec::CheckHandleEos(void *handle) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  VDecCtx &ctx = vdec_ctx_[id - 1];
  cnvdecChnStatus stStatus;
  memset(&stStatus, 0, sizeof(stStatus));
  if (cnvdecQueryStatus(ctx.vdec_chn_, &stStatus) != CN_SUCCESS) {
    ctx.error_flag_ = true;
    if (ctx.result_) {
      ctx.result_->OnError(CN_ERROR_UNKNOWN);
    }
    LOG(ERROR) << "[EasyDK] [MpsVdec] CheckHandleEos(): cnvdecQueryStatus failed";
    return -1;
  }
  if (0 == stStatus.u32LeftPics && stStatus.u32LeftStreamFrames == 0) {
    if (ctx.result_) {
      ctx.result_->OnEos();
    }
    return 0;
  }
  return 1;
}

int MpsVdec::ReleaseFrame(void *handle, const cnVideoFrameInfo_t *info) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  if (id <= 0 || id > kMaxMpsVdecNum) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] ReleaseFrame(): Handle is invalid";
    return -1;
  }
  VDecCtx &ctx = vdec_ctx_[id - 1];
  cnvdecReleaseFrame(ctx.vdec_chn_, info);
  return 0;
}

int MpsVdec::SendStream(void *handle, const cnvdecStream_t *pst_stream, cnS32_t milli_sec) {
  int64_t id = reinterpret_cast<int64_t>(handle);
  if (id <= 0 || id > kMaxMpsVdecNum) {
    LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): Handle is invalid";
    return -1;
  }

  VDecCtx &ctx = vdec_ctx_[id - 1];
  cnvdecStream_t input_data;
  bool send_done = false;
  memset(&input_data, 0, sizeof(input_data));
  if (nullptr == pst_stream) {
    VLOG(2) << "[EasyDK] [MpsVdec] SendStream(): Send EOS";
    if (ctx.eos_sent_) send_done = true;
    ctx.eos_sent_ = true;
    input_data.u32Len = 0;
    input_data.u64PTS = 0;
    input_data.bDisplay = CN_FALSE;
    input_data.bEndOfFrame = CN_TRUE;
    input_data.bEndOfStream = CN_TRUE;
  } else {
    if (ctx.eos_sent_) {
      LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): EOS has been sent, process packet failed, pts:"
                 << pst_stream->u64PTS;
      return -1;
    }
    if (ctx.error_flag_) {
      LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): Error occurred in decoder, process packet failed, pts:"
                 << pst_stream->u64PTS;
      return -1;
    }
    input_data = *pst_stream;
    input_data.bDisplay = CN_TRUE;
    input_data.bEndOfFrame = CN_TRUE;
    input_data.bEndOfStream = CN_FALSE;
  }

  int count = 4;  // FIXME
  while (!ctx.error_flag_) {
    // try SendStream
    if (!send_done) {
      // JPEG dec didn't handle null packet
      if (ctx.eos_sent_ && (PT_JPEG == ctx.chn_attr_.enType)) {
        send_done = true;
        continue;
      }
      int ret = cnvdecSendStream(ctx.vdec_chn_, &input_data, 1000);
      if (ret == CN_SUCCESS) {
        send_done = true;
        continue;
      } else if (ret != CN_ERR_VDEC_BUF_FULL) {
        LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): Send stream failed, ret = " << ret;
        ctx.error_flag_ = true;
        if (ctx.result_) {
          ctx.result_->OnError(ret);
        }
        return -1;
      }

      struct timeval timeout;
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(ctx.fd_, &read_fds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 1000;
      int value = select(ctx.fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
      if (value > 0 && FD_ISSET(ctx.fd_, &read_fds)) {
        // get picture and process it
        cnVideoFrameInfo_t frame_info;
        memset(&frame_info, 0, sizeof(frame_info));
        int ret = cnvdecGetFrame(ctx.vdec_chn_, &frame_info, 0);
        if (ret == CN_SUCCESS) {
          if (ctx.result_) {
            ctx.result_->OnFrame(handle, &frame_info);
          } else {
            cnvdecReleaseFrame(ctx.vdec_chn_, &frame_info);
          }
        } else {
          ctx.error_flag_ = true;
          LOG(ERROR) << "[EasyDK] [MpsVdec] SendFrame(): cnvdecGetFrame failed, ret = " << ret;
          if (ctx.result_) {
            ctx.result_->OnError(ret);
          }
          return -1;
        }
      }
      if (--count < 0) {
        ctx.error_flag_ = true;
        if (ctx.result_) {
          ctx.result_->OnError(CN_ERROR_UNKNOWN);
        }
        LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): Send stream timeout";
        return -1;
      }
    }

    // try getFrame:
    //  1. polling
    //  2. get picture and handle it
    while (1) {
      struct timeval timeout;
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(ctx.fd_, &read_fds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 1000;
      int value = select(ctx.fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
      if (value < 0) {
        LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): select failed";
        return -1;
      }
      if (value == 0) {
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
        LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): FD_ISSET unexpected";
        return -1;
      }

      // get picture and process it
      cnVideoFrameInfo_t frame_info;
      memset(&frame_info, 0, sizeof(frame_info));
      int ret = cnvdecGetFrame(ctx.vdec_chn_, &frame_info, 0);
      if (ret == CN_SUCCESS) {
        if (ctx.result_) {
          ctx.result_->OnFrame(handle, &frame_info);
        } else {
          cnvdecReleaseFrame(ctx.vdec_chn_, &frame_info);
        }
        if (ctx.eos_sent_ && send_done) {
          int value = CheckHandleEos(handle);
          if (value < 0) {
            LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): CheckHandleEos failed, ret = " << value;
            return -1;
          } else if (value == 0) {
            return 0;
          }
          // going on ...
        }
      } else {
        ctx.error_flag_ = true;
        LOG(ERROR) << "[EasyDK] [MpsVdec] SendStream(): cnvdecGetFrame failed, ret = " << ret;
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
