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

#ifndef CNEDK_ENCODE_IMPL_MLU590_HPP_
#define CNEDK_ENCODE_IMPL_MLU590_HPP_

#include <atomic>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

#include "cncodec_v3_common.h"
#include "cncodec_v3_enc.h"

#include "../cnedk_encode_impl.hpp"

namespace cnedk {

class EncoderMlu590 : public IEncoder {
 public:
  EncoderMlu590() = default;
  ~EncoderMlu590() = default;
  // IEncoder
  int Create(CnedkVencCreateParams* params) override;
  int Destroy() override;
  int SendFrame(CnedkBufSurface* surf, int timeout_ms) override;

  // // IVEncResult
  void OnFrameBits(void* _packet);
  void OnEos();
  void OnError(int errcode);

 private:
  int RequestFrame(cncodecFrame_t* frame);
  int Transform(const CnedkBufSurface& src, CnedkBufSurface* dst);
  bool FmtCast(cncodecPixelFormat_t* dst_fmt, CnedkBufSurfaceColorFormat src_fmt);

 private:
  CnedkVencCreateParams create_params_;
  void* venc_ = nullptr;

  std::queue<cncodecFrame_t> cnframe_queue_;
  std::mutex cnframe_queue_mutex_;

  std::atomic<bool> created_{false};
  std::atomic<bool> eos_sent_{false};  // flag for cndec-eos has been sent to decoder
  std::unique_ptr<std::promise<void>> eos_promise_;

  cncodecHandle_t instance_;

  uint64_t frame_count_ = 0;

  uint32_t width_, height_;
  uint32_t frame_rate_;

  uint32_t jpeg_quality_ = 50;
  CnedkBufSurfaceColorFormat color_format_ = CNEDK_BUF_COLOR_FORMAT_NV12;
  CnedkVencType codec_type_ = CNEDK_VENC_TYPE_JPEG;
  uint32_t input_buffer_count_ = 6;

  int mlu_device_id_ = -1;

  void* src_bgr_mlu_ = nullptr;
  void* src_yuv_mlu_ = nullptr;
  uint32_t src_bgr_size_ = 0;
  uint32_t src_yuv_size_ = 0;

  std::atomic<bool> bgr_mlu_alloc_{false};
  std::atomic<bool> yuv_mlu_alloc_{false};
};

}  // namespace cnedk

#endif  // CNEDK_ENCODE_IMPL_MLU590_HPP_
