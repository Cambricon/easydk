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

#include "cnedk_decode_impl_ce3226.hpp"

#include <cstring>  // for memset
#include "glog/logging.h"
#include "cnrt.h"

#include "cn_api.h"

namespace cnedk {

// IDecoder
int DecoderCe3226::Create(CnedkVdecCreateParams *params) {
  create_params_ = *params;
  cnEnPayloadType_t type;
  switch (create_params_.type) {
    case CNEDK_VDEC_TYPE_H264:
      type = PT_H264;
      break;
    case CNEDK_VDEC_TYPE_H265:
      type = PT_H265;
      break;
    case CNEDK_VDEC_TYPE_JPEG:
      type = PT_JPEG;
      break;
    default:
      LOG(ERROR) << "[EasyDK] [DecoderCe3226] Create(): Unsupported codec type: " << create_params_.type;
      return -1;
  }
  cnEnPixelFormat_t pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_VU;
  if (create_params_.type == CNEDK_VDEC_TYPE_JPEG) {
    switch (params->color_format) {
      case CNEDK_BUF_COLOR_FORMAT_NV21:
        pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_VU;
        break;
      case CNEDK_BUF_COLOR_FORMAT_NV12:
        pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
        break;
      default:
        LOG(ERROR) << "[EasyDK] [DecoderCe3226] Create(): Unsupported color format: " << params->color_format;
        return -1;
    }
  } else {
    if (params->color_format != CNEDK_BUF_COLOR_FORMAT_NV21) {
      LOG(ERROR) << "[EasyDK] [DecoderCe3226] Create(): Unsupported color format: " << params->color_format;
      return -1;
    }
  }

  vdec_ = MpsService::Instance().CreateVDec(this, type, params->max_width, params->max_height,
                                            params->frame_buf_num, pix_fmt);
  if (vdec_ == nullptr) {
    LOG(ERROR) << "[EasyDK] [DecoderCe3226] Create(): Create decoder failed";
    return -1;
  }
  return 0;
}

int DecoderCe3226::Destroy() {
  MpsService::Instance().DestroyVDec(vdec_);
  return 0;
}

int DecoderCe3226::SendStream(const CnedkVdecStream *stream, int timeout_ms) {
  if (stream->bits == nullptr || stream->len == 0) {
    VLOG(2) << "[EasyDK] [DecoderCe3226] SendStream(): Sent EOS packet to decoder";
    if (MpsService::Instance().VDecSendStream(vdec_, nullptr, 0) < 0) {
      LOG(ERROR) << "[EasyDK] [DecoderCe3226] SendStream(): Sent EOS packet failed";
      return -1;
    }
    return 0;
  }

  cnvdecStream input_data;
  memset(&input_data, 0, sizeof(input_data));
  input_data.u32Len = stream->len;
  input_data.u64PTS = stream->pts;
  input_data.pu8Addr = stream->bits;
  input_data.bDisplay = CN_TRUE;
  input_data.bEndOfFrame = CN_TRUE;
  input_data.bEndOfStream = CN_FALSE;
  if (MpsService::Instance().VDecSendStream(vdec_, &input_data, timeout_ms) < 0) {
    LOG(ERROR) << "[EasyDK] [DecoderCe3226] SendStream(): Sent packet failed";
    return -1;
  }
  return 0;
}

// IVDecResult
void DecoderCe3226::OnFrame(void *handle, const cnVideoFrameInfo_t *info) {
  // FIXME
  cnVideoFrameInfo_t *rectify_info = const_cast<cnVideoFrameInfo_t *>(info);
  rectify_info->stVFrame.u32Width += rectify_info->stVFrame.u32Width & 1;
  rectify_info->stVFrame.u32Height -= rectify_info->stVFrame.u32Height & 1;

  CnedkBufSurface *surf = nullptr;
  if (create_params_.GetBufSurf(&surf, rectify_info->stVFrame.u32Width, rectify_info->stVFrame.u32Height,
                               GetSurfFmt(info->stVFrame.enPixelFormat), create_params_.surf_timeout_ms,
                               create_params_.userdata) < 0) {
    LOG(ERROR) << "[EasyDK] [DecoderCe3226] OnFrame(): Get BufSurface failed";
    create_params_.OnError(-1, create_params_.userdata);
    MpsService::Instance().VDecReleaseFrame(handle, info);
    return;
  }
  cnVideoFrameInfo_t output;
  BufSurfaceToVideoFrameInfo(surf, &output);
  if (info->stVFrame.enPixelFormat == output.stVFrame.enPixelFormat &&
      rectify_info->stVFrame.u32Width == output.stVFrame.u32Width &&
      rectify_info->stVFrame.u32Height == output.stVFrame.u32Height) {
    CnedkBufSurfacePlaneParams &params = surf->surface_list[0].plane_params;
    for (size_t i = 0; i < params.num_planes; i++) {
      cnrtMemcpy2D(reinterpret_cast<void *>(output.stVFrame.u64PhyAddr[i]), output.stVFrame.u32Stride[i],
                   reinterpret_cast<void *>(info->stVFrame.u64PhyAddr[i]), info->stVFrame.u32Stride[i],
                   params.width[i], params.height[i], cnrtMemcpyDevToDev);
    }
  } else {
    MpsService::Instance().VguScaleCsc(rectify_info, &output);
  }
  MpsService::Instance().VDecReleaseFrame(handle, info);

  surf->pts = rectify_info->stVFrame.u64PTS;
  create_params_.OnFrame(surf, create_params_.userdata);
  return;
}

void DecoderCe3226::OnEos() { create_params_.OnEos(create_params_.userdata); }

void DecoderCe3226::OnError(cnS32_t errcode) {
  // TODO(gaoyujia): convert the error code
  create_params_.OnError(static_cast<int>(errcode), create_params_.userdata);
}

}  // namespace cnedk
