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

#include "cnedk_encode_impl_ce3226.hpp"

#include <cstring>  // for memset
#include "glog/logging.h"

#include "cn_api.h"

namespace cnedk {

// IEncoder
int EncoderCe3226::Create(CnedkVencCreateParams *params) {
  create_params_ = *params;
  VencCreateParam create_params;
  memset(&create_params, 0, sizeof(create_params));

  if (params->type == CNEDK_VENC_TYPE_H264) {
    create_params.type = PT_H264;
  } else if (params->type == CNEDK_VENC_TYPE_H265) {
    create_params.type = PT_H265;
  } else if (params->type == CNEDK_VENC_TYPE_JPEG) {
    create_params.type = PT_JPEG;
  } else {
    LOG(ERROR) << "[EasyDK] [EncoderCe3226] Create(): Unsupported codec type: " << params->type;
    return -1;
  }

  create_params.bitrate = params->bitrate >> 10;
  if (create_params.bitrate < 10 || create_params.bitrate > 800000) {
    LOG(ERROR) << "[EasyDK] [EncoderCe3226] Create(): bitrate is out of range(10240, 819200000), value: "
               << params->bitrate;
    return -1;
  }
  create_params.width = params->width;
  create_params.height = params->height;
  create_params.frame_rate = params->frame_rate;
  create_params.gop_size = params->gop_size;

  venc_ = MpsService::Instance().CreateVEnc(this, &create_params);
  if (venc_ == nullptr) {
    LOG(ERROR) << "[EasyDK] [EncoderCe3226] Create(): Create encoder failed";
    return -1;
  }
  return 0;
}

int EncoderCe3226::Destroy() {
  MpsService::Instance().DestroyVEnc(venc_);
  return 0;
}

int EncoderCe3226::SendFrame(CnedkBufSurface *surf, int timeout_ms) {
  if (!surf || surf->surface_list[0].data_ptr == nullptr) {  // send eos
    VLOG(2) << "[EasyDK] [EncoderCe3226] SendFrame(): Sent EOS frame encoder";
    if (MpsService::Instance().VEncSendFrame(venc_, nullptr, timeout_ms) < 0) {
      LOG(ERROR) << "[EasyDK] [EncoderCe3226] SendFrame(): Sent EOS frame failed";
      return -1;
    }
    return 0;
  }

  if (surf->surface_list[0].width == create_params_.width &&
      surf->surface_list[0].height == create_params_.height) {  // no need scale
    cnVideoFrameInfo_t frame;
    BufSurfaceToVideoFrameInfo(surf, &frame);
    if (MpsService::Instance().VEncSendFrame(venc_, &frame, timeout_ms) < 0) {
      LOG(ERROR) << "[EasyDK] [EncoderCe3226] SendFrame(): Sent frame failed";
      return -1;
    }
  } else {
    if (!output_surf_) {
      CnedkBufSurfaceCreateParams create_params;
      memset(&create_params, 0, sizeof(create_params));
      create_params.batch_size = 1;
      create_params.width = create_params_.width;
      create_params.height = create_params_.height;
      create_params.color_format = surf->surface_list[0].color_format;
      create_params.device_id = 0;
      create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;

      if (CnedkBufPoolCreate(&surf_pool_, &create_params, 6) < 0) {
        LOG(ERROR) << "[EasyDK] [EncoderCe3226] SendFrame(): Create pool failed";
        return -1;
      }

      if (CnedkBufSurfaceCreateFromPool(&output_surf_, surf_pool_) < 0) {
        LOG(ERROR) << "[EasyDK] [EncoderCe3226] SendFrame(): Create BufSurface from pool failed";
        return -1;
      }
    }

    cnVideoFrameInfo_t frame;
    BufSurfaceToVideoFrameInfo(surf, &frame);

    cnVideoFrameInfo_t output;
    BufSurfaceToVideoFrameInfo(output_surf_, &output);

    MpsService::Instance().VguScaleCsc(&frame, &output);

    if (MpsService::Instance().VEncSendFrame(venc_, &output, timeout_ms) < 0) {
      LOG(ERROR) << "[EasyDK] [EncoderCe3226] SendStream(): Sent frame failed";
      return -1;
    }
  }
  return 0;
}

// IVEncResult
void EncoderCe3226::OnFrameBits(VEncFrameBits *frameBits) {
  CnedkVEncFrameBits cnFrameBits;
  memset(&cnFrameBits, 0, sizeof(CnedkVEncFrameBits));
  cnFrameBits.bits = frameBits->bits;
  cnFrameBits.len = frameBits->len;
  cnFrameBits.pts = frameBits->pts;
  cnFrameBits.pkt_type = frameBits->pkt_type;
  create_params_.OnFrameBits(&cnFrameBits, create_params_.userdata);
}
void EncoderCe3226::OnEos() { create_params_.OnEos(create_params_.userdata); }
void EncoderCe3226::OnError(cnS32_t errcode) {
  // TODO(gaoyujia): convert the error code
  create_params_.OnError(static_cast<int>(errcode), create_params_.userdata);
}

}  // namespace cnedk
