/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#include "easycodec/easy_decode.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include "cnrt.h"
#include "cxxutil/log.h"
#include "decoder.h"
#include "device/mlu_context.h"

#define CALL_CNRT_FUNC(func, msg)                                                            \
  do {                                                                                       \
    int ret = (func);                                                                        \
    if (0 != ret) {                                                                          \
      LOGE(DECODE) << msg << " error code: " << ret;                                         \
      THROW_EXCEPTION(Exception::INTERNAL, msg " cnrt error code : " + std::to_string(ret)); \
    }                                                                                        \
  } while (0)

namespace edk {

extern Decoder* CreateMlu200Decoder(const EasyDecode::Attr& attr);

extern Decoder* CreateMlu300Decoder(const EasyDecode::Attr& attr);

extern Decoder* CreateProgressiveJpegDecoder(const EasyDecode::Attr& attr);

std::unique_ptr<EasyDecode> EasyDecode::New(const Attr& attr) {
  return std::unique_ptr<EasyDecode>(new EasyDecode(attr));
}

EasyDecode::EasyDecode(const Attr& attr) {
  MluContext ctx;
  CoreVersion core_version = ctx.GetCoreVersion();

  try {
    if (core_version == CoreVersion::MLU370) {
      handler_ = CreateMlu300Decoder(attr);
    } else if (core_version == CoreVersion::MLU270 || core_version == CoreVersion::MLU220) {
      handler_ = CreateMlu200Decoder(attr);
    } else {
      THROW_EXCEPTION(Exception::INIT_FAILED,
                      "Device not supported yet, core version: " + std::to_string(static_cast<int>(core_version)));
    }
    if (nullptr == handler_) {
      THROW_EXCEPTION(Exception::INIT_FAILED,
                      "New decoder failed, core version: " + std::to_string(static_cast<int>(core_version)));
    }
  } catch (...) {
    delete handler_;
    handler_ = nullptr;
    throw;
  }
}

EasyDecode::~EasyDecode() {
  delete handler_;
  handler_ = nullptr;
  if (progressive_jpeg_handler_) {
    delete progressive_jpeg_handler_;
    progressive_jpeg_handler_ = nullptr;
  }
}

bool EasyDecode::Pause() { return handler_->Pause(); }

bool EasyDecode::Resume() { return handler_->Resume(); }

void EasyDecode::AbortDecoder() { handler_->AbortDecoder(); }

EasyDecode::Status EasyDecode::GetStatus() const { return handler_->GetStatus(); }

bool EasyDecode::FeedData(const CnPacket& packet, bool integral_frame) {
  if (Status::RUNNING != handler_->GetStatus()) {
    return false;
  }
  if (packet.length == 0 || (!packet.data)) {
    LOGE(DECODE) << "Packet do not have data. The packet will not be sent.";
    return false;
  }
  int progressive_mode = detail::CheckProgressiveMode(reinterpret_cast<uint8_t*>(packet.data), packet.length);

  if (progressive_mode == 1) {
    if (!progressive_jpeg_handler_) {
      progressive_jpeg_handler_ = CreateProgressiveJpegDecoder(handler_->GetAttr());
    }
    if (progressive_jpeg_handler_) {
      return progressive_jpeg_handler_->FeedData(packet);
    }
  }
  return handler_->FeedData(packet);
}

bool EasyDecode::FeedEos() { return handler_->FeedEos(); }

void EasyDecode::ReleaseBuffer(uint64_t buf_id) {
  LOGD(DECODE) << "Release decode buffer reference " << buf_id;
  bool is_progressive =
      progressive_jpeg_handler_ && progressive_jpeg_handler_->ReleaseBuffer(buf_id);
  if (!is_progressive) {
    handler_->ReleaseBuffer(buf_id);
  }
}

bool EasyDecode::CopyFrameD2H(void* dst, const CnFrame& frame) {
  if (!dst) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "CopyFrameD2H: destination is nullptr");
    return false;
  }
  auto odata = reinterpret_cast<uint8_t*>(dst);
  LOGT(DECODE) << "Copy codec frame from device to host";
  LOGT(DECODE) << "device address: (plane 0) " << frame.ptrs[0] << ", (plane 1) " << frame.ptrs[1];
  LOGT(DECODE) << "host address: " << reinterpret_cast<int64_t>(odata);

  switch (frame.pformat) {
    case PixelFmt::NV21:
    case PixelFmt::NV12: {
      size_t len_y = frame.strides[0] * frame.height;
      size_t len_uv = frame.strides[1] * frame.height / 2;
      CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<void*>(odata), frame.ptrs[0], len_y, CNRT_MEM_TRANS_DIR_DEV2HOST),
                     "Decode copy frame plane luminance failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y), frame.ptrs[1], len_uv, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane chroma failed.");
      break;
    }
    case PixelFmt::I420: {
      size_t len_y = frame.strides[0] * frame.height;
      size_t len_u = frame.strides[1] * frame.height / 2;
      size_t len_v = frame.strides[2] * frame.height / 2;
      CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<void*>(odata), frame.ptrs[0], len_y, CNRT_MEM_TRANS_DIR_DEV2HOST),
                     "Decode copy frame plane y failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y), frame.ptrs[1], len_u, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane u failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y + len_u), frame.ptrs[2], len_v, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane v failed.");
      break;
    }
    default:
      LOGE(DECODE) << "don't support format: " << static_cast<int>(frame.pformat);
      break;
  }

  return true;
}

EasyDecode::Attr EasyDecode::GetAttr() const { return handler_->GetAttr(); }

int EasyDecode::GetMinimumOutputBufferCount() const { return handler_->GetMinimumOutputBufferCount(); }

}  // namespace edk
