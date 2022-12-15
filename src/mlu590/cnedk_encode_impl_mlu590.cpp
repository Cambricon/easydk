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

#include "cnedk_encode_impl_mlu590.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>  // for memset
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_transform.h"
#include "../common/utils.hpp"

#define CNCODEC_PTS_MAX_VALUE (0xffffffffffffffffLL / 1000)
#define CNCODEC_STRIDE_ALIGNMENT 64

namespace cnedk {

static inline int64_t CurrentTick() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

static i32_t EncoderEventCallback(cncodecEventType_t event_type, void *user_context, void *data) {
  EncoderMlu590 *encoder = reinterpret_cast<EncoderMlu590 *>(user_context);
  switch (event_type) {
    case CNCODEC_EVENT_NEW_FRAME:
      encoder->OnFrameBits(data);
      break;
    case CNCODEC_EVENT_EOS:
      encoder->OnEos();
      break;
    default:
      break;
  }
  return 0;
}

static void PrintCreateAttr(cncodecEncParam_t *p_attr) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->coding_attr.codec_attr.codec);
  printf("%-32s%u\n", "PixelFmt", p_attr->pixel_format);
  printf("%-32s%u\n", "DeviceID", p_attr->device_id);
  printf("%-32s%u\n", "MemoryAllocType", p_attr->input_buf_source);
  printf("%-32s%u\n", "Width", p_attr->pic_width);
  printf("%-32s%u\n", "Height", p_attr->pic_height);
  printf("%-32s%u\n", "MaxWidth", p_attr->max_width);
  printf("%-32s%u\n", "MaxHeight", p_attr->max_height);
  printf("%-32s%u\n", "InputBufferNumber", p_attr->input_buf_num);
  printf("%-32s%u\n", "OutputStreamBufSize", p_attr->stream_buf_size);
}

bool EncoderMlu590::FmtCast(cncodecPixelFormat_t *dst_fmt, CnedkBufSurfaceColorFormat src_fmt) {
  switch (src_fmt) {
    case CNEDK_BUF_COLOR_FORMAT_NV12:
      *dst_fmt = CNCODEC_PIX_FMT_NV12;
      break;
    case CNEDK_BUF_COLOR_FORMAT_NV21:
      *dst_fmt = CNCODEC_PIX_FMT_NV21;
      break;
    // case CNEDK_BUF_COLOR_FORMAT_YUV420:
    //   *dst_fmt = CNCODEC_PIX_FMT_I420;
    //   break;
    default:
      return false;
  }
  return true;
}

// IEncoder
int EncoderMlu590::Create(CnedkVencCreateParams *params) {
  create_params_ = *params;

  mlu_device_id_ = params->device_id;

  if (params->type != CNEDK_VENC_TYPE_JPEG) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] Create(): Codec type must be JPEG";
    return -1;
  }

  input_buffer_count_ = params->input_buf_num;
  if (params->input_buf_num < 3) {
    LOG(WARNING) << "[EasyDK] [EncoderMlu590] Create(): Input buffer count must be bigger than 3. Set to 3";
    input_buffer_count_ = 3;
  }

  width_ = params->width % 2 ? params->width - 1 : params->width;
  height_ = params->height % 2 ? params->height - 1 : params->height;

  cncodecEncParam_t cn_param;

  memset(&cn_param, 0, sizeof(cn_param));
  cn_param.device_id = params->device_id;
  cn_param.run_mode = CNCODEC_RUN_MODE_ASYNC;
  cn_param.coding_attr.codec_attr.codec = CNCODEC_JPEG;
  codec_type_ = CNEDK_VENC_TYPE_JPEG;
  // if (!FmtCast(&cn_param.pixel_format, params->color_format)) {
  //   LOG(ERROR) << "[EasyDK] [EncoderMlu590] Create(): Unsupported pixel format: " << params->color_format;
  //   return -1;
  // }
  if (params->color_format != CNEDK_BUF_COLOR_FORMAT_NV12 &&
      params->color_format != CNEDK_BUF_COLOR_FORMAT_NV21) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] Create(): Unsupported pixel format: " << params->color_format;
    return -1;
  }

  color_format_ = params->color_format;

  if (params->jpeg_quality) {
    jpeg_quality_ = params->jpeg_quality > 100 ? 100 : params->jpeg_quality;
  }

  cn_param.color_space = CNCODEC_COLOR_SPACE_BT_709;
  cn_param.pic_width = width_;
  cn_param.pic_height = height_;
  cn_param.max_width = width_;
  cn_param.max_height = height_;
  cn_param.input_stride_align = 64;
  cn_param.input_buf_num = input_buffer_count_;
  cn_param.input_buf_source = CNCODEC_BUF_SOURCE_LIB;
  cn_param.stream_buf_size = 0;
  cn_param.user_context = reinterpret_cast<void *>(this);

  PrintCreateAttr(&cn_param);

  i32_t ret = cncodecEncCreate(&instance_, EncoderEventCallback, &cn_param);
  if (CNCODEC_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] Create(): Create encoder failed, ret = " << ret;
    return -1;
  }

  created_ = true;

  return 0;
}

int EncoderMlu590::Destroy() {
  if (!created_) {
    LOG(WARNING) << "[EasyDK] [EncoderMlu590] Destroy(): Encoder is not created";
    return 0;
  }

  if (!eos_sent_) {
    SendFrame(nullptr, 10000);
  }
  // wait eos
  if (eos_sent_) {
    eos_promise_->get_future().wait();
    eos_promise_.reset(nullptr);
  }

  // destroy cn encoder
  i32_t ret = cncodecEncDestroy(instance_);
  if (CNCODEC_SUCCESS != ret) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] Destroy(): Destroy encoder failed, ret = " << ret;
  }

  instance_ = 0;

  if (src_bgr_mlu_) {
    cnrtFree(src_bgr_mlu_);
    src_bgr_mlu_ = nullptr;
  }
  if (src_yuv_mlu_) {
    cnrtFree(src_yuv_mlu_);
    src_yuv_mlu_ = nullptr;
  }

  created_ = false;

  return 0;
}

int EncoderMlu590::RequestFrame(cncodecFrame_t *frame) {
  std::unique_lock<std::mutex> lk(cnframe_queue_mutex_);
  if (cnframe_queue_.size()) {
    *frame = cnframe_queue_.front();
    cnframe_queue_.pop();
    return 0;
  }
  lk.unlock();


  memset(frame, 0, sizeof(cncodecFrame_t));
  frame->width = width_;
  frame->height = height_;
  FmtCast(&frame->pixel_format, color_format_);

  int ecode = cncodecEncWaitAvailInputBuf(instance_, frame, 10000);
  if (CNCODEC_ERROR_TIMEOUT == ecode) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] RequestFrame(): cncodecEncWaitAvailInputBuf timeout";
    return -1;
  } else if (CNCODEC_SUCCESS != ecode) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] RequestFrame(): cncodecEncWaitAvailInputBuf failed, ret = " << ecode;
    return -1;
  }

  return 0;
}

int EncoderMlu590::SendFrame(CnedkBufSurface *surf, int timeout_ms) {
  int ret = 0;

  if (!created_) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): Encoder is not created";
    return -1;
  }

  if (nullptr == surf || nullptr == surf->surface_list[0].data_ptr) {
    if (eos_sent_) {
      LOG(WARNING) << "[EasyDK] [EncoderMlu590] SendFrame(): EOS Frame has been send";
      return 0;
    }

    while (cnframe_queue_.size()) {  // free cache codec buffer
      cncodecFrame_t cn_frame;
      cn_frame = cnframe_queue_.front();
      cnframe_queue_.pop();

      cncodecEncPicAttr_t frame_attr;
      memset(&frame_attr, 0, sizeof(cncodecEncPicAttr_t));
      if (codec_type_ == CNEDK_VENC_TYPE_JPEG) {
        frame_attr.jpg_pic_attr.jpeg_param.quality = jpeg_quality_;
      }
      int ret = 0;
      i32_t cnret = cncodecEncSendFrame(instance_, &cn_frame, &frame_attr, 3000);
      if (CNCODEC_ERROR_TIMEOUT == cnret) {
        LOG(ERROR) << "[EasyDK] [EncoderMlu370] SendFrame(): cncodecEncSendFrame timeout";
        ret = -2;
      } else if (CNCODEC_SUCCESS != cnret) {
        LOG(ERROR) << "[EasyDK] [EncoderMlu370] SendFrame(): cncodecEncSendFrame failed, ret = " << cnret;
        ret = -1;
      }

      if (ret == 0) {
        frame_count_++;
      }
    }

    VLOG(2) << "[EasyDK] [EncoderMlu590] SendFrame(): Send EOS Frame to encoder";
    eos_sent_ = true;
    eos_promise_.reset(new std::promise<void>);
    int codec_ret = cncodecEncSetEos(instance_);
    if (CNCODEC_SUCCESS != codec_ret) {
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): Send EOS Frame failed, ret = " << codec_ret;
    }
    return codec_ret;
  }

  int timeout = timeout_ms < 0 ? 0x7fffffff : timeout_ms;  // TODO(hqw): temp fix for cncodec bug

  // 1. get cn_frame
  cncodecFrame_t cn_frame;
  ret = RequestFrame(&cn_frame);
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): Request frame failed, ret = " << ret;
    return ret;
  }

  {
    CnedkBufSurface transform_dst;
    CnedkBufSurfaceParams dst_param;
    memset(&transform_dst, 0, sizeof(CnedkBufSurface));
    memset(&dst_param, 0, sizeof(CnedkBufSurfaceParams));

    {  // prepare transform output
      transform_dst.batch_size = 1;
      transform_dst.num_filled = 1;

      dst_param.data_ptr = reinterpret_cast<void *>(cn_frame.plane[0].dev_addr);
      dst_param.plane_params.num_planes = 2;
      dst_param.plane_params.offset[0] = 0;
      dst_param.plane_params.offset[1] = cn_frame.plane[1].dev_addr - cn_frame.plane[0].dev_addr;

      dst_param.pitch =
          (cn_frame.width + CNCODEC_STRIDE_ALIGNMENT - 1) / CNCODEC_STRIDE_ALIGNMENT * CNCODEC_STRIDE_ALIGNMENT;
      dst_param.width = cn_frame.width;
      dst_param.height = cn_frame.height;
      dst_param.plane_params.num_planes = 2;
      dst_param.plane_params.pitch[0] = dst_param.pitch;
      dst_param.plane_params.pitch[1] = dst_param.pitch;
      dst_param.color_format = color_format_;

      transform_dst.surface_list = &dst_param;
      transform_dst.device_id = mlu_device_id_;
      transform_dst.mem_type = CNEDK_BUF_MEM_DEVICE;
    }
    ret = Transform(*surf, &transform_dst);
    if (ret < 0) {
      std::unique_lock<std::mutex> lk(cnframe_queue_mutex_);
      cnframe_queue_.push(cn_frame);
      lk.unlock();
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): Transform frame failed, ret = " << ret;
    }
  }

  // 3. send en_frame to encode
  if (surf->surface_list[0].data_ptr != nullptr) {
    cn_frame.pts = surf->pts;
    cncodecEncPicAttr_t frame_attr;
    memset(&frame_attr, 0, sizeof(cncodecEncPicAttr_t));
    if (codec_type_ == CNEDK_VENC_TYPE_JPEG) {
      frame_attr.jpg_pic_attr.jpeg_param.quality = jpeg_quality_;
    }
    ret = 0;
    i32_t cnret = cncodecEncSendFrame(instance_, &cn_frame, &frame_attr, timeout);
    if (CNCODEC_ERROR_TIMEOUT == cnret) {
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): cncodecEncSendFrame timeout";
      ret = -2;
    } else if (CNCODEC_SUCCESS != cnret) {
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] SendFrame(): cncodecEncSendFrame failed, ret = " << cnret;
      ret = -1;
    }

    if (ret == 0) {
      frame_count_++;
    } else {
      std::unique_lock<std::mutex> lk(cnframe_queue_mutex_);
      cnframe_queue_.push(cn_frame);
      lk.unlock();
    }
  }
  return 0;
}

int EncoderMlu590::Transform(const CnedkBufSurface &src, CnedkBufSurface *dst) {
  // case 1: bgr(cpu) -> bgr(device) -> yuv(the same resolution with bgr) -> yuv (dst resolution)
  // case 2: bgr(cpu) -> bgr(device) -> yuv (dst resolution)
  // case 3: bgr(device) -> yuv(the same resolution with bgr) -> yuv(dst resolution)
  // case 4: bgr(device) -> yuv(dst resolution)
  // case 5: yuv(cpu) -> yuv(device) -> yuv(dst resolution)
  // case 6: yuv(cpu) -> yuv(dst resolution)
  // case 7: yuv(device, different resoultion with dst) -> yuv(dst resolution)
  // case 8: yuv(device, same resolution with dst) -> yuv(dst resolution)

  // malloc middle device memory
  if (src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_BGR ||
      src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_RGB) {
    if (src.mem_type == CNEDK_BUF_MEM_SYSTEM) {
      size_t bgr_size = src.surface_list[0].data_size;
      if (bgr_mlu_alloc_ == false || src_bgr_size_ < bgr_size) {
        if (src_bgr_mlu_) {  // realloc
          cnrtFree(src_bgr_mlu_);
        }

        CNRT_SAFECALL(cnrtMalloc(&src_bgr_mlu_, bgr_size),
                      "[EncoderMlu590] Transform(): malloc src bgr memory failed.", -1);
        src_bgr_size_ = bgr_size;
        bgr_mlu_alloc_ = true;
      }
    }
  } else if (src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV21 ||
             src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV12) {
    if (src.mem_type == CNEDK_BUF_MEM_SYSTEM) {
      size_t yuv_size = src.surface_list[0].data_size;
      if (yuv_mlu_alloc_ == false || src_yuv_size_ < yuv_size) {
        if (src_yuv_mlu_) {  // realloc
          cnrtFree(src_yuv_mlu_);
        }

        CNRT_SAFECALL(cnrtMalloc(&src_yuv_mlu_, yuv_size),
                      "[EncoderMlu590] Transform(): malloc src yuv memory failed.", -1);
        src_yuv_size_ = yuv_size;
        yuv_mlu_alloc_ = true;
      }
    }
  } else {
    LOG(ERROR) << "[EasyDK] [EncoderMlu590] Transform(): Unsupported color format";
    return -1;
  }

  CnedkTransformParams trans_params;
  memset(&trans_params, 0, sizeof(trans_params));

  // construct bufsurce
  if (src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_BGR ||
      src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_RGB) {
    CnedkBufSurface transform_src;
    CnedkBufSurfaceParams src_param;
    memset(&transform_src, 0, sizeof(CnedkBufSurface));
    memset(&src_param, 0, sizeof(CnedkBufSurfaceParams));

    if (src.mem_type == CNEDK_BUF_MEM_SYSTEM) {
      CNRT_SAFECALL(cnrtMemcpy(src_bgr_mlu_, src.surface_list[0].data_ptr, src.surface_list[0].data_size,
                               CNRT_MEM_TRANS_DIR_HOST2DEV),
                    "[EncoderMlu590] Transform(): src bgr H2D failed", -1);
      src_param.data_ptr = src_bgr_mlu_;
    } else {
      src_param.data_ptr = src.surface_list[0].data_ptr;
    }

    src_param.data_size = src.surface_list[0].data_size;
    src_param.color_format = src.surface_list[0].color_format;
    src_param.pitch = src.surface_list[0].pitch;
    src_param.width = src.surface_list[0].width;
    src_param.height = src.surface_list[0].height;
    src_param.plane_params.num_planes = 1;

    transform_src.batch_size = 1;
    transform_src.num_filled = 1;
    transform_src.device_id = mlu_device_id_;
    transform_src.mem_type = CNEDK_BUF_MEM_DEVICE;
    transform_src.surface_list = &src_param;

    CnedkTransform(&transform_src, dst, &trans_params);

  } else if (src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV21 ||
             src.surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV12) {
    if (src.surface_list[0].plane_params.pitch[0] == dst->surface_list[0].plane_params.pitch[0] &&
        src.surface_list[0].height == dst->surface_list[0].height) {
      if (src.mem_type == CNEDK_BUF_MEM_SYSTEM) {
        CNRT_SAFECALL(cnrtMemcpy(dst->surface_list[0].data_ptr, src.surface_list[0].data_ptr,
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[0],
                                 CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "[EncoderMlu590] Transform(): src y to dst H2D", -1);

        CNRT_SAFECALL(cnrtMemcpy(reinterpret_cast<char *>(dst->surface_list[0].data_ptr) +
                                 dst->surface_list[0].plane_params.offset[1],
                                 reinterpret_cast<char *>(src.surface_list[0].data_ptr) +
                                 src.surface_list[0].plane_params.offset[1],
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[1] / 2,
                                 CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "[EncoderMlu590] Transform(): src uv to dst H2D", -1);
      } else {
        CNRT_SAFECALL(cnrtMemcpy(dst->surface_list[0].data_ptr, src.surface_list[0].data_ptr,
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[0],
                                 CNRT_MEM_TRANS_DIR_DEV2DEV),
                      "[EncoderMlu590] Transform():src y to dst D2D failed", -1);

        CNRT_SAFECALL(cnrtMemcpy(reinterpret_cast<char *>(dst->surface_list[0].data_ptr) +
                                 dst->surface_list[0].plane_params.offset[1],
                                 reinterpret_cast<char *>(src.surface_list[0].data_ptr) +
                                 src.surface_list[0].plane_params.offset[1],
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[1] / 2,
                                 CNRT_MEM_TRANS_DIR_DEV2DEV),
                      "[EncoderMlu590] Transform(): src uv to dst D2D failed", -1);
      }
    } else {
      if (src.mem_type == CNEDK_BUF_MEM_DEVICE) {
        if (CnedkTransform(&(const_cast<CnedkBufSurface &>(src)), dst, &trans_params)) {
          return -1;
        }
      } else {
        CnedkBufSurface transform_src;
        CnedkBufSurfaceParams src_param;
        memset(&transform_src, 0, sizeof(CnedkBufSurface));
        memset(&src_param, 0, sizeof(CnedkBufSurfaceParams));

        CNRT_SAFECALL(cnrtMemcpy(src_yuv_mlu_, src.surface_list[0].data_ptr,
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[0],
                                 CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "[EncoderMlu590] Transform(): src y H2D failed", -1);

        CNRT_SAFECALL(cnrtMemcpy(reinterpret_cast<char *>(src_yuv_mlu_) + src.surface_list[0].plane_params.offset[1],
                                 reinterpret_cast<char *>(src.surface_list[0].data_ptr) +
                                 src.surface_list[0].plane_params.offset[1],
                                 src.surface_list[0].height * src.surface_list[0].plane_params.pitch[1] / 2,
                                 CNRT_MEM_TRANS_DIR_HOST2DEV),
                      "[EncoderMlu590] Transform(): src uv H2D failed", -1);

        src_param.pitch = src.surface_list[0].pitch;
        src_param.width = src.surface_list[0].width;
        src_param.height = src.surface_list[0].height;
        src_param.color_format = src.surface_list[0].color_format;
        src_param.data_size = src.surface_list[0].data_size;
        src_param.plane_params.num_planes = 2;
        src_param.plane_params.pitch[0] = src.surface_list[0].plane_params.pitch[0];
        src_param.plane_params.pitch[1] = src.surface_list[0].plane_params.pitch[1];
        src_param.plane_params.offset[0] = 0;
        src_param.plane_params.offset[1] = src.surface_list[0].height * src.surface_list[0].plane_params.pitch[0];
        src_param.data_ptr = src_yuv_mlu_;

        transform_src.batch_size = 1;
        transform_src.num_filled = 1;
        transform_src.device_id = mlu_device_id_;
        transform_src.mem_type = CNEDK_BUF_MEM_DEVICE;
        transform_src.surface_list = &src_param;
        if (CnedkTransform(&transform_src, dst, &trans_params)) {
          LOG(ERROR) << "[EasyDK] [EncoderMlu590] Transform(): CnedkTransform failed";
          return -1;
        }
      }
    }
  }
  return 0;
}

// IVEncResult
void EncoderMlu590::OnFrameBits(void *_packet) {
  if (create_params_.OnFrameBits != nullptr) {
    cnrtSetDevice(mlu_device_id_);

    CnedkVEncFrameBits cnFrameBits;
    memset(&cnFrameBits, 0, sizeof(CnedkVEncFrameBits));
    cncodecStream_t *stream = reinterpret_cast<cncodecStream_t *>(_packet);

    cnFrameBits.pkt_type = CNEDK_VENC_PACKAGE_TYPE_KEY_FRAME;

    // std::unique_ptr<uint8_t> packet_data {new (std::nothrow) uint8_t[stream->data_len]};
    uint8_t *packet_data = nullptr;
    try {
      packet_data = new uint8_t[stream->data_len];
    } catch (std::bad_alloc & exception) {
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] OnFrameBits(): bad_alloc " << exception.what()
                 << "; data_len " << stream->data_len;
      return;
    }

    auto ret = cnrtMemcpy(packet_data, reinterpret_cast<void *>(stream->mem_addr + stream->data_offset),
                          stream->data_len, CNRT_MEM_TRANS_DIR_DEV2HOST);

    if (ret != cnrtSuccess) {
      delete[] packet_data;
      LOG(ERROR) << "[EasyDK] [EncoderMlu590] OnFrameBits(): Copy bitstream failed, D2H";
      return;
    }

    cnFrameBits.bits = packet_data;
    cnFrameBits.len = stream->data_len;
    cnFrameBits.pts = stream->pts;

    create_params_.OnFrameBits(&cnFrameBits, create_params_.userdata);

    delete[] packet_data;
  }
}

void EncoderMlu590::OnEos() {
  eos_promise_->set_value();
  create_params_.OnEos(create_params_.userdata);
}

void EncoderMlu590::OnError(int errcode) {
  // TODO(gaoyujia): convert the error code
  create_params_.OnError(static_cast<int>(errcode), create_params_.userdata);
}

}  // namespace cnedk
