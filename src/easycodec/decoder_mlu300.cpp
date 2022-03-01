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
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <iostream>

#include "cxxutil/log.h"
#include "decoder.h"

#ifdef ENABLE_MLU300_CODEC
#include <cncodec_v3_common.h>
#include <cncodec_v3_dec.h>
#include <cnrt.h>

namespace edk {

static void PrintCreateAttr(cncodecDecCreateInfo_t* create_info, cncodecDecParams_t* codec_params) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", create_info->codec);
  printf("%-32s%u\n", "DeviceID", create_info->device_id);
  printf("%-32s%u\n", "PixelFormat", codec_params->pixel_format);
  printf("%-32s%u\n", "Width", codec_params->max_width);
  printf("%-32s%u\n", "Height", codec_params->max_height);
  printf("%-32s%u\n", "StrideAlign", codec_params->stride_align);
  printf("%-32s%u\n", "InputStreamBufSize", create_info->stream_buf_size);
  printf("%-32s%u\n", "OutputBufferNum", codec_params->output_buf_num);
  printf("-------------------------------------\n");
}

class Mlu300Decoder : public Decoder {
 public:
  explicit Mlu300Decoder(const EasyDecode::Attr& attr);
  ~Mlu300Decoder();
  bool FeedData(const CnPacket& packet) override;
  bool FeedEos() override;
  void AbortDecoder() override;
  bool ReleaseBuffer(uint64_t buf_id) override;

  void ReceiveFrame(cncodecFrame_t* out);
  int ReceiveSequence(cncodecDecSequenceInfo_t* info);
  void ReceiveEvent(cncodecEventType_t type);
  void ReceiveEOS();

 private:
  void InitDecode(const EasyDecode::Attr& attr);
  void SetDecParams();

  // cncodec handle
  cncodecHandle_t handle_ = 0;

  cncodecDecCreateInfo_t create_info_;
  cncodecDecParams_t codec_params_;

  uint32_t packets_count_ = 0;
  uint32_t frames_count_ = 0;

  int receive_seq_time_ = 0;

  /// eos workround
  std::mutex eos_mtx_;
  std::condition_variable eos_cond_;
  std::atomic<bool> send_eos_{false};
  std::atomic<bool> got_eos_{false};
};  // class Mlu300Decoder

static i32_t Mlu300EventHandler(cncodecEventType_t type, void* user_data, void* package) {
  auto handler = reinterpret_cast<Mlu300Decoder*>(user_data);
  if (handler == nullptr) {
    LOGE(DECODE) << "Mlu300Decoder handler is nullptr";
    return 0;
  }
  switch (type) {
    case CNCODEC_EVENT_NEW_FRAME:
      handler->ReceiveFrame(reinterpret_cast<cncodecFrame_t*>(package));
      break;
    case CNCODEC_EVENT_SEQUENCE:
      handler->ReceiveSequence(reinterpret_cast<cncodecDecSequenceInfo_t*>(package));
      break;
    default:
      handler->ReceiveEvent(type);
      break;
  }
  return 0;
}

Mlu300Decoder::Mlu300Decoder(const EasyDecode::Attr& attr) : Decoder(attr) {
  struct __ShowCodecVersion {
    __ShowCodecVersion() {
      uint32_t major, minor, patch;
      int ret = cncodecGetLibVersion(&major, &minor, &patch);
      if (CNCODEC_SUCCESS == ret) {
        LOGI(DECODE) << "CNCodec Version: " << major << "." << minor << "." << patch;
      } else {
        LOGW(DECODE) << "Get CNCodec version failed.";
      }
    }
  };
  static __ShowCodecVersion show_version;

  try {
    InitDecode(attr);
  }catch (...) {
    throw;
  }
}

Mlu300Decoder::~Mlu300Decoder() {
  if (status_.load() == EasyDecode::Status::ERROR && handle_) {
    Mlu300Decoder::AbortDecoder();
    return;
  }
  /**
   * Decode destroyed. status set to STOP.
   */
  status_.store(EasyDecode::Status::STOP);
  /**
   * Release resources.
   */
  if (!handle_) {
    send_eos_.store(true);
    got_eos_.store(true);
  }
  try {
    if (!got_eos_.load() && !send_eos_.load()) {
      LOGI(DECODE) << "Send EOS in destruct";
      Mlu300Decoder::FeedEos();
    }

    if (!got_eos_.load()) {
      LOGI(DECODE) << "Wait EOS in destruct";
      std::unique_lock<std::mutex> eos_lk(eos_mtx_);
      eos_cond_.wait(eos_lk, [this]() -> bool { return got_eos_; });
    }
  } catch (Exception& e) {
    LOGE(DECODE) << e.what();
  }

  if (handle_) {
    int codec_ret = cncodecDecDestroy(handle_);
    if (CNCODEC_SUCCESS != codec_ret) {
      LOGE(DECODE) << "Call cncodecDecDestroy failed, ret = " << codec_ret;
    }
    handle_ = 0;
  }
}

bool Mlu300Decoder::FeedData(const CnPacket& packet) {
  if (!handle_) {
    LOGE(DECODE) << "Decoder has not been init";
    return false;
  }
  if (send_eos_) {
    LOGW(DECODE) << "EOS had been sent, won't feed data";
    return false;
  }

  cncodecStream_t codec_input;
  memset(&codec_input, 0, sizeof(codec_input));
  codec_input.mem_type = CNCODEC_MEM_TYPE_HOST;
  codec_input.mem_addr = reinterpret_cast<u64_t>(packet.data);
  codec_input.data_len = packet.length;
  codec_input.pts = packet.pts;
  codec_input.priv_data = reinterpret_cast<u64_t>(packet.user_data);
  LOGT(DECODE) << "Feed stream info, data: " << reinterpret_cast<void*>(codec_input.mem_addr)
              << ", length: " << codec_input.data_len << ", pts: " << codec_input.pts;
  int retry_time = 3;
  while (retry_time--) {
    int codec_ret = cncodecDecSendStream(handle_, &codec_input, 10000);
    if (CNCODEC_ERROR_TIMEOUT == codec_ret) {
      LOGW(DECODE) << "cncodecDecSendStream timeout, retry feed data, time: " << 3 - retry_time;
      if (!retry_time) {
        status_.store(EasyDecode::Status::ERROR);
        THROW_EXCEPTION(Exception::TIMEOUT, "easydecode timeout");
      }
      continue;
    } else if (CNCODEC_SUCCESS != codec_ret) {
      THROW_EXCEPTION(Exception::INTERNAL, "Call cncodecDecSendStream failed, ret = " + std::to_string(codec_ret));
    } else {
      break;
    }
  }
  packets_count_++;
  return true;
}

bool Mlu300Decoder::FeedEos() {
  if (status_.load() == EasyDecode::Status::ERROR) {
    LOGW(DECODE) << "Error had occured, EOS won't be sent";
    return false;
  }
  if (send_eos_.load()) {
    LOGW(DECODE) << "EOS had been feed, won't feed again";
    return false;
  }

  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ", Feed EOS data";

  int codec_ret = cncodecDecSetEos(handle_);
  if (CNCODEC_ERROR_INVALID_HANDLE == codec_ret) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::INVALID_ARG, "Feed EOS failed, invalid handle");
  } else if (CNCODEC_ERROR_TRANSMIT_FAILED == codec_ret) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::INTERNAL, "Feed EOS failed, communication with the device failed");
  } else if (CNCODEC_SUCCESS != codec_ret) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::INTERNAL, "Feed EOS failed. cncodec error code: " + std::to_string(codec_ret));
  }

  send_eos_ = true;
  return true;
}

void Mlu300Decoder::AbortDecoder() {
  LOGW(DECODE) << "Abort decoder";
  if (handle_) {
    int codec_ret = cncodecDecDestroy(handle_);
    if (CNCODEC_SUCCESS != codec_ret) {
      LOGE(DECODE) << "Call cncodecDecDestroy failed, ret = " << codec_ret;
    }
    handle_ = 0;
    status_.store(EasyDecode::Status::STOP);

    std::unique_lock<std::mutex> eos_lk(eos_mtx_);
    send_eos_.store(true);
    got_eos_.store(true);
    eos_cond_.notify_one();
  } else {
    LOGE(DECODE) << "Won't do abort, since cndecode handler has not been initialized";
  }
}

bool Mlu300Decoder::ReleaseBuffer(uint64_t buf_id) {
  if (handle_) {
    auto ret = cncodecDecFrameUnref(handle_, reinterpret_cast<cncodecFrame_t*>(buf_id));
    if (CNCODEC_SUCCESS == ret) {
      return true;
    } else {
      LOGE(DECODE) << "Release buffer failed. buf_id: " << buf_id;
    }
  }
  return false;
}

void Mlu300Decoder::InitDecode(const EasyDecode::Attr& attr) {
  memset(&create_info_, 0, sizeof(create_info_));
  memset(&codec_params_, 0, sizeof(codec_params_));
  uint32_t width = attr.frame_geometry.w;
  uint32_t height = attr.frame_geometry.h;
  create_info_.device_id       = attr.dev_id;
  create_info_.send_mode       = CNCODEC_DEC_SEND_MODE_FRAME;
  create_info_.run_mode        = CNCODEC_RUN_MODE_ASYNC;
  create_info_.stream_buf_size = width * height * 5 / 4;
  create_info_.user_context    = this;

  codec_params_.output_buf_num    = attr.output_buffer_num;
  switch (attr.codec_type) {
    case CodecType::H264:
      create_info_.codec = CNCODEC_H264;
      break;
    case CodecType::H265:
      create_info_.codec = CNCODEC_HEVC;
      break;
    case CodecType::VP8:
      create_info_.codec = CNCODEC_VP8;
      break;
    case CodecType::VP9:
      create_info_.codec = CNCODEC_VP9;
      break;
    case CodecType::JPEG:
    case CodecType::MJPEG:
      create_info_.codec = CNCODEC_JPEG;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec type not supported yet, codec_type:"
          + std::to_string(static_cast<int>(attr.codec_type)));
    }
  }
  if (create_info_.codec != CNCODEC_JPEG) {
    switch (attr.pixel_format) {
      case PixelFmt::NV12:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_NV12;
        break;
      case PixelFmt::NV21:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_NV21;
        break;
      case PixelFmt::I420:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_I420;
        break;
      case PixelFmt::P010:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_P010;
        break;
      case PixelFmt::I010:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_I010;
        break;
      case PixelFmt::MONOCHROME:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_MONOCHROME;
        break;
      default: {
        THROW_EXCEPTION(Exception::INIT_FAILED, "codec pixel format not supported yet, pixel format:"
            + std::to_string(static_cast<int>(attr.pixel_format)));
      }
    }
  } else {
    switch (attr.pixel_format) {
      case PixelFmt::NV12:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_NV12;
        break;
      case PixelFmt::NV21:
        codec_params_.pixel_format = CNCODEC_PIX_FMT_NV21;
        break;
      default: {
        THROW_EXCEPTION(Exception::INIT_FAILED, "codec pixel format not supported yet, pixel format:"
            + std::to_string(static_cast<int>(attr.pixel_format)));
      }
    }
  }

  int codec_ret = cncodecDecCreate(&handle_, &Mlu300EventHandler, &create_info_);
  if (CNCODEC_SUCCESS != codec_ret || !handle_) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create decode failed: " + std::to_string(codec_ret));
  }

  codec_params_.color_space       = CNCODEC_COLOR_SPACE_BT_709;
  codec_params_.output_buf_source = CNCODEC_BUF_SOURCE_LIB;
  codec_params_.output_order      = CNCODEC_DEC_OUTPUT_ORDER_DISPLAY;

  codec_params_.max_width    = width;
  codec_params_.max_height   = height;
  if (CNCODEC_JPEG == create_info_.codec) {
    codec_params_.stride_align = 64;  // must be multiple of 64 for jpeg
    SetDecParams();
  } else {
    codec_params_.stride_align = attr.stride_align;
    codec_params_.dec_mode = CNCODEC_DEC_MODE_IPB;
  }

  if (!attr.silent) {
    PrintCreateAttr(&create_info_, &codec_params_);
  }
}

inline void Mlu300Decoder::SetDecParams() {
  int codec_ret = cncodecDecSetParams(handle_, &codec_params_);
  if (CNCODEC_SUCCESS != codec_ret) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::INTERNAL, "Call cncodecDecSetParams failed, ret = " +std::to_string(codec_ret));
  }
}


void Mlu300Decoder::ReceiveFrame(cncodecFrame_t* codec_frame) {
  // config CnFrame for user callback.
  CnFrame finfo;
  if (codec_frame->width == 0 || codec_frame->height == 0 || codec_frame->plane_num == 0) {
    LOGW(DECODE) << "Receive empty frame";
    return;
  }
  finfo.width = codec_frame->width;
  finfo.height = codec_frame->height;
  finfo.pts = codec_frame->pts;
  finfo.device_id = codec_frame->device_id;
  finfo.channel_id = codec_frame->mem_channel;
  finfo.buf_id = reinterpret_cast<uint64_t>(codec_frame);
  finfo.n_planes = codec_frame->plane_num;
  finfo.frame_size = 0;
  finfo.pformat =  attr_.pixel_format;
  for (uint32_t pi = 0; pi < codec_frame->plane_num; ++pi) {
    finfo.strides[pi] = codec_frame->plane[pi].stride;
    finfo.ptrs[pi] = reinterpret_cast<void*>(codec_frame->plane[pi].dev_addr);
    finfo.frame_size += finfo.GetPlaneSize(pi);
  }
  finfo.user_data = reinterpret_cast<void*>(codec_frame->priv_data);

  LOGT(DECODE) << "Frame: width " << finfo.width << " height " << finfo.height << " planes " << finfo.n_planes
               << " frame size " << finfo.frame_size;

  if (NULL != attr_.frame_callback) {
    LOGD(DECODE) << "Add decode buffer Reference " << finfo.buf_id;
    cncodecDecFrameRef(handle_, codec_frame);
    attr_.frame_callback(finfo);
    frames_count_++;
  }
}

int Mlu300Decoder::ReceiveSequence(cncodecDecSequenceInfo_t* seq_info) {
  LOGI(DECODE) << "Receive sequence";
  receive_seq_time_++;
  if (receive_seq_time_ > 1) {
    // variable geometry stream. check output buffer number, width, height and reset codec params
    if (codec_params_.output_buf_num < seq_info->min_output_buf_num + 1 ||
        codec_params_.max_width < seq_info->coded_width ||
        codec_params_.max_height < seq_info->coded_height) {
      LOGE(DECODE) << "Variable video resolutions, the preset parameters do not meet requirements."
                   << "max width[" << codec_params_.max_width << "], "
                   << "max height[" << codec_params_.max_height << "], "
                   << "output buffer number[" << codec_params_.output_buf_num << "]. "
                   << "But required: "
                   << "coded width[" << seq_info->coded_width << "], "
                   << "coded height[" << seq_info->coded_height << "], "
                   << "min output buffer number[" << seq_info->min_output_buf_num << "].";
      status_.store(EasyDecode::Status::ERROR);
      THROW_EXCEPTION(Exception::INTERNAL, "easydecode the preset parameters do not meet requirements");
    }
  } else {
    if (codec_params_.max_width && codec_params_.max_height) {
      LOGI(DECODE) << "Variable video resolutions enabled, max width x max height : "
                   << codec_params_.max_width << " x " << codec_params_.max_height;
    } else {
      codec_params_.max_width = seq_info->coded_width;
      codec_params_.max_height = seq_info->coded_height;
    }
    codec_params_.output_buf_num = std::max(seq_info->min_output_buf_num + 1, codec_params_.output_buf_num);
    SetDecParams();
  }
  minimum_buf_cnt_ = seq_info->min_output_buf_num;
  return 0;
}

void Mlu300Decoder::ReceiveEOS() {
  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ", Received EOS from cncodec";

  status_.store(EasyDecode::Status::EOS);
  if (attr_.eos_callback) {
    attr_.eos_callback();
  }

  std::unique_lock<std::mutex> eos_lk(eos_mtx_);
  got_eos_.store(true);
  eos_cond_.notify_one();
}

void Mlu300Decoder::ReceiveEvent(cncodecEventType_t type) {
  switch (type) {
    case CNCODEC_EVENT_EOS:
      ReceiveEOS();
      break;
    case CNCODEC_EVENT_OUT_OF_MEMORY:
      LOGE(DECODE) << "Out of memory error thrown from cncodec";
      status_.store(EasyDecode::Status::ERROR);
      break;
    case CNCODEC_EVENT_STREAM_CORRUPT:
      LOGW(DECODE) << "Stream corrupt, discard frame";
      break;
    case CNCODEC_EVENT_STREAM_NOT_SUPPORTED:
      LOGE(DECODE) << "Out of memory error thrown from cncodec";
      status_.store(EasyDecode::Status::ERROR);
      break;
    case CNCODEC_EVENT_BUFFER_OVERFLOW:
      LOGW(DECODE) << "buffer overflow thrown from cncodec, output buffer number is not enough";
      break;
    case CNCODEC_EVENT_FATAL_ERROR:
      LOGE(DECODE) << "fatal error throw from cncodec";
      status_.store(EasyDecode::Status::ERROR);
      break;
    default:
      LOGE(DECODE) << "Unknown event type";
      status_.store(EasyDecode::Status::ERROR);
      break;
  }
}

Decoder* CreateMlu300Decoder(const EasyDecode::Attr& attr) {
  return new Mlu300Decoder(attr);
}
}  // namespace edk

#else
namespace edk {

Decoder* CreateMlu300Decoder(const EasyDecode::Attr& attr) {
  LOGE(DECODE) << "Create mlu300 decoder failed, please install cncodec_v3.";
  return nullptr;
}
}  // namespace edk

#endif
