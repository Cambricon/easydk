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

#include "cnedk_decode_impl_mlu590.hpp"

#include <algorithm>
#include <cstring>  // for memset

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_transform.h"
#include "../common/utils.hpp"

static constexpr int DECODE_MAX_TRY_SEND_TIME = 3;

namespace cnedk {

static i32_t DecoderEventCallback(cncodecEventType_t type, void *ctx, void *output) {
  auto decoder = reinterpret_cast<DecoderMlu590 *>(ctx);
  switch (type) {
    case CNCODEC_EVENT_NEW_FRAME:
      decoder->ReceiveFrame(reinterpret_cast<cncodecFrame_t *>(output));
      break;
    case CNCODEC_EVENT_SEQUENCE:
      decoder->ReceiveSequence(reinterpret_cast<cncodecDecSequenceInfo_t *>(output));
      break;
    case CNCODEC_EVENT_EOS:
      decoder->OnEos();
      break;
    case CNCODEC_EVENT_STREAM_CORRUPT:
      decoder->HandleStreamCorrupt();
      break;
    case CNCODEC_EVENT_STREAM_NOT_SUPPORTED:
      decoder->HandleStreamNotSupport();
      break;
    default:
      decoder->HandleUnknownEvent(type);
      break;
  }
  return 0;
}

// IDecoder
int DecoderMlu590::Create(CnedkVdecCreateParams *params) {
  create_params_ = *params;

  memset(&create_info_, 0, sizeof(create_info_));
  memset(&codec_params_, 0, sizeof(codec_params_));

  create_info_.device_id = create_params_.device_id;
  create_info_.send_mode = CNCODEC_DEC_SEND_MODE_FRAME;
  create_info_.run_mode = CNCODEC_RUN_MODE_ASYNC;
  switch (create_params_.type) {
    case CNEDK_VDEC_TYPE_H264:
      create_info_.codec = CNCODEC_H264;
      break;
    case CNEDK_VDEC_TYPE_H265:
      create_info_.codec = CNCODEC_HEVC;
      break;
    case CNEDK_VDEC_TYPE_JPEG:
      create_info_.codec = CNCODEC_JPEG;
      break;
    default:
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] Create(): Unsupported codec type: " << create_info_.codec;
      return -1;
  }
  switch (params->color_format) {
    case CNEDK_BUF_COLOR_FORMAT_NV21:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_NV21;
      break;
    case CNEDK_BUF_COLOR_FORMAT_NV12:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_NV12;
      break;
    default:
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] Create(): Unsupported pixel format: " << codec_params_.pixel_format;
      return -1;
  }

  create_info_.stream_buf_size = 4 << 20;  // FIXME
  create_info_.user_context = this;

  ResetFlags();

  int codec_ret = cncodecDecCreate(&instance_, &DecoderEventCallback, &create_info_);
  if (CNCODEC_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] Create(): Create decoder failed, ret = " << codec_ret;
    return -1;
  }

  created_ = true;

  if (create_params_.type == CNEDK_VDEC_TYPE_JPEG) {
    codec_params_.output_buf_num = 2;
  }

  codec_params_.color_space = CNCODEC_COLOR_SPACE_BT_709;
  codec_params_.output_buf_source = CNCODEC_BUF_SOURCE_LIB;
  codec_params_.output_order = CNCODEC_DEC_OUTPUT_ORDER_DISPLAY;

  if (CNCODEC_JPEG == create_info_.codec) {
    codec_params_.max_width = create_params_.max_width ? create_params_.max_width : 7680;
    codec_params_.max_height = create_params_.max_height ? create_params_.max_height : 4320;
    codec_params_.stride_align = 64;  // must be multiple of 64 for jpeg
    return SetDecParams();
  } else {
    if (create_params_.max_width != 0 && create_params_.max_height != 0) {
      codec_params_.max_width = create_params_.max_width;
      codec_params_.max_height = create_params_.max_height;
    }
    codec_params_.stride_align = 1;
    codec_params_.dec_mode = CNCODEC_DEC_MODE_IPB;
  }

  return 0;
}

int DecoderMlu590::Destroy() {
  if (!created_) {
    LOG(WARNING) << "[EasyDK] [DecoderMlu590] Destroy(): Decoder is not created";
    return 0;
  }
  // if error happened, destroy directly, eos maybe not be transmitted from the decoder
  if (!error_flag_ && !eos_sent_) {
    SendStream(nullptr, 10000);
  }
  // wait eos
  if (eos_sent_) {
    eos_promise_->get_future().wait();
    eos_promise_.reset(nullptr);
  }
  /**
   * make sure all cndec buffers released before destorying cndecoder
   */
  while (cndec_buf_ref_count_) {
    std::this_thread::yield();
  }
  int codec_ret = cncodecDecDestroy(instance_);
  if (CNCODEC_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] Destroy(): Desctroy decoder failed, ret = " << codec_ret;
  }
  instance_ = 0;  // FIXME(lmx): INVALID HANDLE?
  ResetFlags();

  return 0;
}

int DecoderMlu590::SendStream(const CnedkVdecStream *stream, int timeout_ms) {
  if (!created_) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] SendStream(): Decoder is not created";
    return -1;
  }
  if (nullptr == stream || nullptr == stream->bits) {
    if (eos_sent_) {
      LOG(WARNING) << "[EasyDK] [DecoderMlu590] SendStream(): EOS packet has been send";
      return 0;
    }
    VLOG(2) << "[EasyDK] [DecoderMlu590] SendStream(): Send EOS packet to decoder";
    eos_sent_ = true;
    eos_promise_.reset(new std::promise<void>);
    int codec_ret = cncodecDecSetEos(instance_);
    if (CNCODEC_SUCCESS != codec_ret) {
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] SendStream(): Send EOS packet failed, ret = " << codec_ret;
    }
  } else {
    if (eos_sent_) {
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] SendStream(): EOS has been sent, process packet failed, pts:"
                 << stream->pts;
      return -1;
    }
    if (error_flag_) {
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] SendStream(): Error occurred in decoder, process packet failed, pts:"
                 << stream->pts;
      return -1;
    }
    cncodecStream_t codec_input;
    memset(&codec_input, 0, sizeof(codec_input));
    codec_input.mem_type = CNCODEC_MEM_TYPE_HOST;
    codec_input.mem_addr = reinterpret_cast<u64_t>(stream->bits);
    codec_input.data_len = stream->len;
    codec_input.pts = stream->pts;
    int max_try_send_time = DECODE_MAX_TRY_SEND_TIME;
    while (max_try_send_time--) {
      int codec_ret = cncodecDecSendStream(instance_, &codec_input, timeout_ms);
      switch (codec_ret) {
        case CNCODEC_SUCCESS:
          return 0;
        case CNCODEC_ERROR_BAD_STREAM:
        case CNCODEC_ERROR_NOT_SUPPORTED:
          return -3;
        case CNCODEC_ERROR_TIMEOUT:
          LOG(INFO) << "[EasyDK] [DecoderMlu590] SendStream(): cncodecDecSendStream timeout happened, retry feed data,"
                    << " remaining times: " << max_try_send_time;
          continue;
        default:
          LOG(ERROR) << "[EasyDK] [DecoderMlu590] SendStream(): cncodecDecSendStream failed, ret = " << codec_ret;
          return -1;
      }  // switch send stream ret
    }    // while timeout
    LOG(INFO) << "[EasyDK] [DecoderMlu590] SendStream(): cncodecDecSendStream timeout happened,"
              << " retry times: " << DECODE_MAX_TRY_SEND_TIME;
    return -2;
  }
  return 0;
}

int DecoderMlu590::SetDecParams() {
  int codec_ret = cncodecDecSetParams(instance_, &codec_params_);
  if (CNCODEC_SUCCESS != codec_ret) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] SetDecParams(): cncodecDecSetParams failed, ret = " << codec_ret;
    return false;
  }
  return true;
}

// IVDecResult
void DecoderMlu590::OnFrame(cncodecFrame_t *codec_frame) {
  // FIXME
  codec_frame->width += codec_frame->width & 1;
  codec_frame->height -= codec_frame->height & 1;

  CnedkBufSurface *surf = nullptr;
  if (create_params_.GetBufSurf(&surf, codec_frame->width, codec_frame->height, GetSurfFmt(codec_frame->pixel_format),
                                create_params_.surf_timeout_ms, create_params_.userdata) < 0) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] OnFrame(): Get BufSurface failed";
    OnError(-1);
    return;
  }

  if (surf->mem_type != CNEDK_BUF_MEM_DEVICE) {
    LOG(ERROR) << "[EasyDK] [DecoderMlu590] OnFrame(): BufSurface memory type must be CNEDK_BUF_MEM_DEVICE";
    return;
  }

  cnrtSetDevice(create_params_.device_id);

  switch (codec_frame->pixel_format) {
    case CNCODEC_PIX_FMT_NV12:
    case CNCODEC_PIX_FMT_NV21:
      if (surf->surface_list[0].width != codec_frame->width || surf->surface_list[0].height != codec_frame->height) {
        CnedkBufSurface transform_src;
        CnedkBufSurfaceParams src_param;
        memset(&transform_src, 0, sizeof(CnedkBufSurface));
        memset(&src_param, 0, sizeof(CnedkBufSurfaceParams));

        src_param.data_size = codec_frame->plane[0].stride * codec_frame->height * 3 / 2;
        if (codec_frame->pixel_format == CNCODEC_PIX_FMT_NV12)
          src_param.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
        else if (codec_frame->pixel_format == CNCODEC_PIX_FMT_NV21)
          src_param.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
        src_param.data_ptr = reinterpret_cast<void *>(codec_frame->plane[0].dev_addr);

        VLOG(5) << "[EasyDK] [DecoderMlu590] OnFrame(): codec_frame: "
                << " width = " << codec_frame->width
                << ", height = " << codec_frame->height
                << ", stride = " << codec_frame->plane[0].stride;
        VLOG(5) << "[EasyDK] [DecoderMlu590] OnFrame(): surf->surface_list[0]: "
                << " width = " << surf->surface_list[0].width
                << ", height = " << surf->surface_list[0].height
                << ", stride = " << surf->surface_list[0].plane_params.pitch[0];

        src_param.width = codec_frame->width;
        src_param.height = codec_frame->height;
        src_param.plane_params.num_planes = 2;
        src_param.plane_params.offset[0] = 0;
        src_param.plane_params.offset[1] = codec_frame->plane[1].dev_addr - codec_frame->plane[0].dev_addr;
        src_param.plane_params.pitch[0] = codec_frame->plane[0].stride;
        src_param.plane_params.pitch[1] = codec_frame->plane[1].stride;

        transform_src.batch_size = 1;
        transform_src.num_filled = 1;
        transform_src.device_id = create_params_.device_id;
        transform_src.mem_type = CNEDK_BUF_MEM_DEVICE;
        transform_src.surface_list = &src_param;

        CnedkTransformParams trans_params;
        memset(&trans_params, 0, sizeof(trans_params));

        if (CnedkTransform(&transform_src, surf, &trans_params) < 0) {
          LOG(ERROR) << "[EasyDK] [DecoderMlu590] OnFrame(): CnedkTransfrom failed";
          break;
        }

      } else {
        CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<char *>(surf->surface_list[0].data_ptr) +
                                  surf->surface_list[0].plane_params.offset[0],
                                  reinterpret_cast<void *>(codec_frame->plane[0].dev_addr),
                                  codec_frame->plane[0].stride * codec_frame->height, cnrtMemcpyDevToDev),
                       "[DecoderMlu590] OnFrame(): copy codec buffer y data to surf failed");
        CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<char *>(surf->surface_list[0].data_ptr) +
                                  surf->surface_list[0].plane_params.offset[1],
                                  reinterpret_cast<void *>(codec_frame->plane[1].dev_addr),
                                  codec_frame->plane[1].stride * codec_frame->height / 2, cnrtMemcpyDevToDev),
                       "[DecoderMlu590] OnFrame(): copy codec buffer uv data to surf failed");
      }

      break;
    default:
      break;
  }

  surf->pts = codec_frame->pts;

  create_params_.OnFrame(surf, create_params_.userdata);
}

void DecoderMlu590::OnEos() {
  eos_promise_->set_value();
  create_params_.OnEos(create_params_.userdata);
}
void DecoderMlu590::OnError(int errcode) {
  // TODO(gaoyujia): convert the error code
  create_params_.OnError(static_cast<int>(errcode), create_params_.userdata);
}

void DecoderMlu590::ReceiveFrame(cncodecFrame_t *codec_frame) { OnFrame(codec_frame); }

void DecoderMlu590::ReceiveSequence(cncodecDecSequenceInfo_t *seq_info) {
  receive_seq_time_++;
  if (1 < receive_seq_time_) {
    // variable geometry stream. check output buffer number, width, height and reset codec params
    if (codec_params_.output_buf_num < seq_info->min_output_buf_num + 1 ||
        codec_params_.max_width < seq_info->coded_width || codec_params_.max_height < seq_info->coded_height) {
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] ReceiveSequence(): "
                 << "Variable video resolutions, the preset parameters do not meet requirements."
                 << "max width[" << codec_params_.max_width << "], "
                 << "max height[" << codec_params_.max_height << "], "
                 << "output buffer number[" << codec_params_.output_buf_num << "]. "
                 << "But required: "
                 << "coded width[" << seq_info->coded_width << "], "
                 << "coded height[" << seq_info->coded_height << "], "
                 << "min output buffer number[" << seq_info->min_output_buf_num << "].";
      error_flag_ = true;
      OnError(-1);
    }
  } else {
    if (codec_params_.max_width && codec_params_.max_height) {
      VLOG(2) << "[EasyDK] [DecoderMlu590] ReceiveSequence(): Variable video resolutions enabled,"
              << " max width x height: " << codec_params_.max_width << " x " << codec_params_.max_height;
    } else {
      codec_params_.max_width = seq_info->coded_width;
      codec_params_.max_height = seq_info->coded_height;
    }

    codec_params_.output_buf_num = seq_info->min_output_buf_num + 1;
    if (!SetDecParams()) {
      LOG(ERROR) << "[EasyDK] [DecoderMlu590] ReceiveSequence(): Set decoder params failed";
      error_flag_ = true;
      OnError(-1);
    }
  }
}

void DecoderMlu590::HandleStreamCorrupt() {
  LOG(ERROR) << "[EasyDK] [DecoderMlu590] HandleStreamCorrupt(): Stream corrupt...";
}

inline void DecoderMlu590::HandleStreamNotSupport() {
  LOG(ERROR) << "[EasyDK] [DecoderMlu590] HandleStreamNotSupport(): Received unsupported event";
  error_flag_ = true;
  OnError(-1);
}

void DecoderMlu590::HandleUnknownEvent(cncodecEventType_t type) {
  LOG(ERROR) << "[EasyDK] [DecoderMlu590] HandleStreamNotSupport(): Received unknown event, type: "
             << static_cast<int>(type);
}

void DecoderMlu590::ResetFlags() {
  eos_sent_ = false;
  // timeout_  = false;
  error_flag_ = false;
  created_ = false;
}

}  // namespace cnedk
