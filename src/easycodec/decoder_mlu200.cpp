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

#include <cnrt.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include "cxxutil/log.h"
#include "decoder.h"

#ifdef ENABLE_MLU200_CODEC
#include <cn_jpeg_dec.h>
#include <cn_video_dec.h>
#include "vpu_turbo_table.h"

#define ALIGN(size, alignment) (((u32_t)(size) + (alignment)-1) & ~((alignment)-1))

// cncodec add version macro since v1.6.0
#ifndef CNCODEC_VERSION
#define CNCODEC_VERSION 0
#endif

namespace edk {

static void PrintCreateAttr(cnvideoDecCreateInfo* p_attr) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->codec);
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Progressive", p_attr->progressive);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "OutputBufferNum", p_attr->outputBufNum);
  printf("-------------------------------------\n");
}

static void PrintCreateAttr(cnjpegDecCreateInfo* p_attr) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "OutputBufferNum", p_attr->outputBufNum);
  printf("%-32s%u\n", "InputBufferSize", p_attr->suggestedLibAllocBitStrmBufSize);
  printf("-------------------------------------\n");
}

class Mlu200Decoder : public Decoder {
 public:
  explicit Mlu200Decoder(const EasyDecode::Attr& attr);
  ~Mlu200Decoder();
  bool FeedData(const CnPacket& packet) override;
  bool FeedEos() override;
  void AbortDecoder() override;
  bool ReleaseBuffer(uint64_t buf_id) override;

  void ReceiveFrame(void* out);
  int ReceiveSequence(cnvideoDecSequenceInfo* info);
  void ReceiveEvent(cncodecCbEventType type);
  void ReceiveEOS();

 private:
  void InitVideoDecode(const EasyDecode::Attr& attr);
  void InitJpegDecode(const EasyDecode::Attr& attr);
  void FeedVideoData(const CnPacket& packet);
  void FeedJpegData(const CnPacket& packet);

  uint32_t SetVpuTimestamp(uint64_t pts);
  bool GetVpuTimestamp(uint32_t key, uint64_t *pts);

  // cncodec handle
  void* handle_ = nullptr;

  cnvideoDecCreateInfo vparams_;
  cnjpegDecCreateInfo jparams_;

  uint32_t packets_count_ = 0;
  uint32_t frames_count_ = 0;

  /// eos workround
  std::mutex eos_mtx_;
  std::condition_variable eos_cond_;
  std::atomic<bool> send_eos_{false};
  std::atomic<bool> got_eos_{false};
  bool jpeg_decode_ = false;

  // For m200 vpu-decoder, m200 vpu-codec does not 64bits timestamp, we have to implement it.
  uint32_t pts_key_ = 0;
  std::unordered_map<uint32_t, uint64_t> vpu_pts_map_;
  std::mutex pts_map_mtx_;
};  // class Mlu200Decoder

static i32_t Mlu200EventHandler(cncodecCbEventType type, void* user_data, void* package) {
  auto handler = reinterpret_cast<Mlu200Decoder*>(user_data);
  if (handler == nullptr) {
    LOGE(DECODE) << "Mlu200Decoder handler is nullptr";
    return 0;
  }
  switch (type) {
    case CNCODEC_CB_EVENT_NEW_FRAME:
      handler->ReceiveFrame(package);
      break;
    case CNCODEC_CB_EVENT_SEQUENCE:
      handler->ReceiveSequence(reinterpret_cast<cnvideoDecSequenceInfo*>(package));
      break;
    default:
      handler->ReceiveEvent(type);
      break;
  }
  return 0;
}

Mlu200Decoder::Mlu200Decoder(const EasyDecode::Attr& attr) : Decoder(attr) {
  struct __ShowCodecVersion {
    __ShowCodecVersion() {
      u8_t* version = cncodecGetVersion();
      LOGI(DECODE) << "CNCodec Version: " << static_cast<const unsigned char*>(version);
    }
  };
  static __ShowCodecVersion show_version;

  jpeg_decode_ = attr.codec_type == CodecType::JPEG || attr.codec_type == CodecType::MJPEG;

  try {
    if (jpeg_decode_) {
      InitJpegDecode(attr);
    } else {
      InitVideoDecode(attr);
    }
  }catch (...) {
    throw;
  }
}

Mlu200Decoder::~Mlu200Decoder() {
  if (status_.load() == EasyDecode::Status::ERROR && handle_) {
    Mlu200Decoder::AbortDecoder();
  }
  /**
   * Decode destroied. status set to STOP.
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
      Mlu200Decoder::FeedEos();
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
    if (jpeg_decode_) {
      // Destroy jpu decoder
      LOGI(DECODE) << "Destroy jpeg decoder channel";
      auto ecode = cnjpegDecDestroy(handle_);
      if (CNCODEC_SUCCESS != ecode) {
        LOGE(DECODE) << "Decoder destroy failed Error Code: " << ecode;
      }
    } else {
      // destroy vpu decoder
      LOGI(DECODE) << "Stop video decoder channel";
      auto ecode = cnvideoDecStop(handle_);
      if (CNCODEC_SUCCESS != ecode) {
        LOGE(DECODE) << "Decoder stop failed Error Code: " << ecode;
      }

      LOGI(DECODE) << "Destroy video decoder channel";
      ecode = cnvideoDecDestroy(handle_);
      if (CNCODEC_SUCCESS != ecode) {
        LOGE(DECODE) << "Decoder destroy failed Error Code: " << ecode;
      }
    }
    handle_ = nullptr;
  }
}

bool Mlu200Decoder::FeedData(const CnPacket& packet) {
  if (!handle_) {
    LOGE(DECODE) << "Decoder has not been init";
    return false;
  }
  if (send_eos_) {
    LOGW(DECODE) << "EOS had been sent, won't feed data";
    return false;
  }

  if (jpeg_decode_) {
    FeedJpegData(packet);
  } else {
    FeedVideoData(packet);
  }
  return true;
}

bool Mlu200Decoder::FeedEos() {
  if (status_.load() == EasyDecode::Status::ERROR) {
    LOGW(DECODE) << "Error had occurred, EOS won't be sent";
    return false;
  }
  if (send_eos_.load()) {
    LOGW(DECODE) << "EOS had been feed, won't feed again";
    return false;
  }

  i32_t ecode = CNCODEC_SUCCESS;
  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ", Feed EOS data";
  if (jpeg_decode_) {
    cnjpegDecInput input;
    input.streamBuffer = nullptr;
    input.streamLength = 0;
    input.pts = 0;
    input.flags = CNJPEGDEC_FLAG_EOS;
    ecode = cnjpegDecFeedData(handle_, &input, 10000);
  } else {
    cnvideoDecInput input;
    input.streamBuf = nullptr;
    input.streamLength = 0;
    input.pts = 0;
    input.flags = CNVIDEODEC_FLAG_EOS;
    ecode = cnvideoDecFeedData(handle_, &input, 10000);
  }

  if (-CNCODEC_TIMEOUT == ecode) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::TIMEOUT, "EasyDecode feed EOS timeout");
  } else if (CNCODEC_SUCCESS != ecode) {
    status_.store(EasyDecode::Status::ERROR);
    THROW_EXCEPTION(Exception::INTERNAL, "Feed EOS failed. cncodec error code: " + std::to_string(ecode));
  }

  send_eos_ = true;
  return true;
}

void Mlu200Decoder::AbortDecoder() {
  LOGW(DECODE) << "Abort decoder";
  if (handle_) {
    if (jpeg_decode_) {
      cnjpegDecAbort(handle_);
    } else {
      cnvideoDecAbort(handle_);
    }
    handle_ = nullptr;
    status_.store(EasyDecode::Status::STOP);

    std::unique_lock<std::mutex> eos_lk(eos_mtx_);
    send_eos_.store(true);
    got_eos_.store(true);
    eos_cond_.notify_one();
  } else {
    LOGE(DECODE) << "Won't do abort, since cndecode handler has not been initialized";
  }
}

bool Mlu200Decoder::ReleaseBuffer(uint64_t buf_id) {
  int ret;
  if (jpeg_decode_) {
    ret = cnjpegDecReleaseReference(handle_, reinterpret_cast<cncodecFrame*>(buf_id));
  } else {
    ret = cnvideoDecReleaseReference(handle_, reinterpret_cast<cncodecFrame*>(buf_id));
  }
  if (CNCODEC_SUCCESS == ret) {
    return true;
  } else {
    LOGE(DECODE) << "Release buffer failed. buf_id: " << buf_id;
    return false;
  }
}

void Mlu200Decoder::InitVideoDecode(const EasyDecode::Attr& attr) {
  memset(&vparams_, 0, sizeof(cnvideoDecCreateInfo));
  vparams_.deviceId = attr.dev_id;
  if (const char* turbo_env_p = std::getenv("VPU_TURBO_MODE")) {
    LOGI(DECODE) << "VPU Turbo mode : " << turbo_env_p;
    static std::mutex vpu_instance_mutex;
    std::unique_lock<std::mutex> lk(vpu_instance_mutex);
    static int _vpu_inst_cnt = 0;
    vparams_.instance = g_vpudec_instances[_vpu_inst_cnt++ % 100];
  } else {
    vparams_.instance = CNVIDEODEC_INSTANCE_AUTO;
  }
  switch (attr.codec_type) {
    case CodecType::H264:
      vparams_.codec = CNCODEC_H264;
      break;
    case CodecType::H265:
      vparams_.codec = CNCODEC_HEVC;
      break;
    case CodecType::VP8:
      vparams_.codec = CNCODEC_VP8;
      break;
    case CodecType::VP9:
      vparams_.codec = CNCODEC_VP9;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec type not supported yet, codec_type:"
          + std::to_string(static_cast<int>(attr.codec_type)));
    }
  }
  switch (attr.pixel_format) {
    case PixelFmt::NV12:
      vparams_.pixelFmt = CNCODEC_PIX_FMT_NV12;
      break;
    case PixelFmt::NV21:
      vparams_.pixelFmt = CNCODEC_PIX_FMT_NV21;
      break;
    case PixelFmt::I420:
      vparams_.pixelFmt = CNCODEC_PIX_FMT_I420;
      break;
    case PixelFmt::P010:
      vparams_.pixelFmt = CNCODEC_PIX_FMT_P010;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec pixel format not supported yet, pixel format:"
          + std::to_string(static_cast<int>(attr.pixel_format)));
    }
  }
  vparams_.width = attr.frame_geometry.w;
  vparams_.height = attr.frame_geometry.h;
  vparams_.bitDepthMinus8 = attr.pixel_format == PixelFmt::P010 ? 2 : 0;
  vparams_.progressive = attr.interlaced ? 0 : 1;
  vparams_.inputBufNum = 2;
  vparams_.outputBufNum = attr.output_buffer_num;
  vparams_.allocType = CNCODEC_BUF_ALLOC_LIB;
  vparams_.userContext = reinterpret_cast<void*>(this);

  if (!attr.silent) {
    PrintCreateAttr(&vparams_);
  }

  int ecode = cnvideoDecCreate(&handle_, &Mlu200EventHandler, &vparams_);
  if (CNCODEC_SUCCESS != ecode || !handle_) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create video decode failed: " + std::to_string(ecode));
  }

  ecode = cnvideoDecSetAttributes(handle_, CNVIDEO_DEC_ATTR_OUT_BUF_ALIGNMENT, &(attr_.stride_align));
  if (CNCODEC_SUCCESS != ecode) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "cnvideo decode set attributes faild: " + std::to_string(ecode));
  }
}

void Mlu200Decoder::InitJpegDecode(const EasyDecode::Attr& attr) {
  memset(&jparams_, 0, sizeof(cnjpegDecCreateInfo));
  jparams_.deviceId = attr.dev_id;
  jparams_.instance = CNJPEGDEC_INSTANCE_AUTO;
  switch (attr.pixel_format) {
    case PixelFmt::NV12:
      jparams_.pixelFmt = CNCODEC_PIX_FMT_NV12;
      break;
    case PixelFmt::NV21:
      jparams_.pixelFmt = CNCODEC_PIX_FMT_NV21;
      break;
    case PixelFmt::YUYV:
      jparams_.pixelFmt = CNCODEC_PIX_FMT_YUYV;
      break;
    case PixelFmt::UYVY:
      jparams_.pixelFmt = CNCODEC_PIX_FMT_UYVY;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec pixel format not supported yet, pixel format:"
          + std::to_string(static_cast<int>(attr.pixel_format)));
    }
  }
  jparams_.width = attr.frame_geometry.w;
  jparams_.height = attr.frame_geometry.h;
  jparams_.inputBufNum = 2;
  jparams_.outputBufNum = attr.output_buffer_num;
  jparams_.bitDepthMinus8 = 0;
  jparams_.allocType = CNCODEC_BUF_ALLOC_LIB;
  jparams_.userContext = reinterpret_cast<void*>(this);
  jparams_.suggestedLibAllocBitStrmBufSize = (4U << 20);
  jparams_.enablePreparse = 0;
  if (!attr.silent) {
    PrintCreateAttr(&jparams_);
  }
  int ecode = cnjpegDecCreate(&handle_, CNJPEGDEC_RUN_MODE_ASYNC, &Mlu200EventHandler, &jparams_);
  if (0 != ecode) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create jpeg decode failed: " + std::to_string(ecode));
  }
}

void Mlu200Decoder::FeedVideoData(const CnPacket& packet) {
  cnvideoDecInput input;
  memset(&input, 0, sizeof(cnvideoDecInput));
  input.streamBuf = reinterpret_cast<u8_t*>(packet.data);
  input.streamLength = packet.length;
  input.pts = SetVpuTimestamp(packet.pts);
  input.flags = CNVIDEODEC_FLAG_TIMESTAMP;
  input.flags |= CNVIDEODEC_FLAG_END_OF_FRAME;
  LOGT(DECODE) << "Feed stream info, data: " << reinterpret_cast<void*>(input.streamBuf)
               << ", length: " << input.streamLength << ", pts: " << input.pts << ", flag: " << input.flags;

  int retry_time = 3;
  while (retry_time--) {
    auto ecode = cnvideoDecFeedData(handle_, &input, 10000);
    if (-CNCODEC_TIMEOUT == ecode) {
      LOGW(DECODE) << "cnvideoDecFeedData timeout, retry feed data, time: " << 3 - retry_time;
      if (!retry_time) {
        GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
        status_.store(EasyDecode::Status::ERROR);
        THROW_EXCEPTION(Exception::TIMEOUT, "easydecode timeout");
      }
      continue;
    } else if (CNCODEC_SUCCESS != ecode) {
      GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
      status_.store(EasyDecode::Status::ERROR);
      THROW_EXCEPTION(Exception::INTERNAL, "Feed data failed. cncodec error code: " + std::to_string(ecode));
    } else {
      break;
    }
  }

  packets_count_++;
}

void Mlu200Decoder::FeedJpegData(const CnPacket& packet) {
  cnjpegDecInput input;
  memset(&input, 0, sizeof(cnjpegDecInput));
  input.streamBuffer = reinterpret_cast<uint8_t*>(packet.data);
  input.streamLength = packet.length;
  input.pts = packet.pts;
  input.flags = CNJPEGDEC_FLAG_TIMESTAMP;
  LOGT(DECODE) << "Feed stream info, data: " << reinterpret_cast<void*>(input.streamBuffer)
               << " ,length: " << input.streamLength << " ,pts: " << input.pts;

  int retry_time = 3;
  while (retry_time--) {
    auto ecode = cnjpegDecFeedData(handle_, &input, 10000);
    if (-CNCODEC_TIMEOUT == ecode) {
      LOGW(DECODE) << "cnjpegDecFeedData timeout, retry feed data, time: " << 3 - retry_time;
      if (!retry_time) {
        GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
        status_.store(EasyDecode::Status::ERROR);
        THROW_EXCEPTION(Exception::TIMEOUT, "easydecode timeout");
      }
      continue;
    } else if (CNCODEC_SUCCESS != ecode) {
      GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
      status_.store(EasyDecode::Status::ERROR);
      THROW_EXCEPTION(Exception::INTERNAL, "Feedd data failed. cncodec error code: " + std::to_string(ecode));
    } else {
      break;
    }
  }
}

void Mlu200Decoder::ReceiveFrame(void* out) {
  // config CnFrame for user callback.
  CnFrame finfo;
  cncodecFrame* frame = nullptr;
  if (jpeg_decode_) {
    auto o = reinterpret_cast<cnjpegDecOutput*>(out);
    finfo.pts = o->pts;
    frame = &o->frame;
    LOGT(DECODE) << "Receive one jpeg frame, " << frame;
  } else {
    auto o = reinterpret_cast<cnvideoDecOutput*>(out);
    uint64_t usr_pts;
    if (GetVpuTimestamp(o->pts, &usr_pts)) {
      finfo.pts = usr_pts;
    } else {
      // need to return, if GetVpuTimestamp failed?
      LOGW(DECODE) << "Failed to query timetamp,"
                   << ", use timestamp from vpu-decoder:" << o->pts;
    }
    frame = &o->frame;
    LOGT(DECODE) << "Receive one video frame, " << frame;
  }
  if (frame->width == 0 || frame->height == 0 || frame->planeNum == 0) {
    LOGW(DECODE) << "Receive empty frame";
    return;
  }
  finfo.device_id = attr_.dev_id;
  finfo.channel_id = frame->channel;
  finfo.buf_id = reinterpret_cast<uint64_t>(frame);
  finfo.width = frame->width;
  finfo.height = frame->height;
  finfo.n_planes = frame->planeNum;
  finfo.frame_size = 0;
  finfo.pformat = attr_.pixel_format;
  for (uint32_t pi = 0; pi < frame->planeNum; ++pi) {
    finfo.strides[pi] = frame->stride[pi];
    finfo.ptrs[pi] = reinterpret_cast<void*>(frame->plane[pi].addr);
    finfo.frame_size += finfo.GetPlaneSize(pi);
  }

  LOGT(DECODE) << "Frame: width " << finfo.width << " height " << finfo.height << " planes " << finfo.n_planes
               << " frame size " << finfo.frame_size;

  if (NULL != attr_.frame_callback) {
    LOGD(DECODE) << "Add decode buffer Reference " << finfo.buf_id;
    if (jpeg_decode_) {
      cnjpegDecAddReference(handle_, frame);
    } else {
      cnvideoDecAddReference(handle_, frame);
    }
    attr_.frame_callback(finfo);
    frames_count_++;
  }
}

int Mlu200Decoder::ReceiveSequence(cnvideoDecSequenceInfo* info) {
  LOGI(DECODE) << "Receive sequence";

  vparams_.codec = info->codec;
  vparams_.width = info->width;
  vparams_.height = info->height;
  minimum_buf_cnt_ = info->minOutputBufNum;

  if (info->minInputBufNum > vparams_.inputBufNum) {
    vparams_.inputBufNum = info->minInputBufNum;
  }
  if (info->minOutputBufNum > vparams_.outputBufNum) {
    vparams_.outputBufNum = info->minOutputBufNum;
  }

  vparams_.userContext = reinterpret_cast<void*>(this);

  int ecode = cnvideoDecStart(handle_, &vparams_);
  if (ecode != CNCODEC_SUCCESS) {
    LOGE(DECODE) << "Start Decoder failed.";
    return -1;
  }
  return 0;
}

void Mlu200Decoder::ReceiveEOS() {
  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ",Received EOS from cncodec";

  status_.store(EasyDecode::Status::EOS);
  if (attr_.eos_callback) {
    attr_.eos_callback();
  }

  std::unique_lock<std::mutex> eos_lk(eos_mtx_);
  got_eos_.store(true);
  eos_cond_.notify_one();
}

void Mlu200Decoder::ReceiveEvent(cncodecCbEventType type) {
  switch (type) {
    case CNCODEC_CB_EVENT_EOS:
      ReceiveEOS();
      break;
    case CNCODEC_CB_EVENT_SW_RESET:
    case CNCODEC_CB_EVENT_HW_RESET:
      LOGE(DECODE) << "Decode firmware crash event: " << type;
      status_.store(EasyDecode::Status::ERROR);
      break;
    case CNCODEC_CB_EVENT_OUT_OF_MEMORY:
      LOGE(DECODE) << "Out of memory error thrown from cncodec";
      status_.store(EasyDecode::Status::ERROR);
      break;
    case CNCODEC_CB_EVENT_ABORT_ERROR:
      LOGE(DECODE) << "Abort error thrown from cncodec";
      status_.store(EasyDecode::Status::ERROR);
      break;
#if CNCODEC_VERSION >= 10600
    case CNCODEC_CB_EVENT_STREAM_CORRUPT:
      LOGW(DECODE) << "Stream corrupt, discard frame";
      break;
#endif
    default:
      LOGE(DECODE) << "Unknown event type";
      status_.store(EasyDecode::Status::ERROR);
      break;
  }
}

uint32_t Mlu200Decoder::SetVpuTimestamp(uint64_t pts) {
  std::lock_guard<std::mutex> lock(pts_map_mtx_);
  uint32_t key = pts_key_++;
  vpu_pts_map_[key] = pts;
  return key;
}

bool Mlu200Decoder::GetVpuTimestamp(uint32_t key, uint64_t *pts) {
  std::lock_guard<std::mutex> lock(pts_map_mtx_);
  auto iter = vpu_pts_map_.find(key);
  if (iter != vpu_pts_map_.end()) {
    if (pts) {
      *pts = iter->second;
      vpu_pts_map_.erase(iter);
      return true;
    }
    vpu_pts_map_.erase(iter);
    return false;
  }
  return false;
}

Decoder* CreateMlu200Decoder(const EasyDecode::Attr& attr) {
  return new Mlu200Decoder(attr);
}
}  // namespace edk

#else
namespace edk {
Decoder* CreateMlu200Decoder(const EasyDecode::Attr& attr) {
  LOGE(DECODE) << "Create mlu200 decoder failed, please install cncodec.";
  return nullptr;
}
}  // namespace edk
#endif  // ENABLE_MLU200_CODEC
