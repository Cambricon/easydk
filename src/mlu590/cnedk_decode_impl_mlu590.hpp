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

#ifndef CNEDK_DECODE_IMPL_MLU590_HPP_
#define CNEDK_DECODE_IMPL_MLU590_HPP_

#include <atomic>
#include <future>
#include <map>
#include <memory>

#include "cncodec_v3_common.h"
#include "cncodec_v3_dec.h"

#include "../cnedk_decode_impl.hpp"

namespace cnedk {

class DecoderMlu590 : public IDecoder {
 public:
  DecoderMlu590() = default;
  ~DecoderMlu590() = default;
  // IDecoder
  int Create(CnedkVdecCreateParams *params) override;
  int Destroy() override;
  int SendStream(const CnedkVdecStream *stream, int timeout_ms) override;
  CnedkBufSurfaceColorFormat GetSurfFmt(cncodecPixelFormat_t format) {
    static std::map<cncodecPixelFormat_t, CnedkBufSurfaceColorFormat> color_map{
        {CNCODEC_PIX_FMT_NV12, CNEDK_BUF_COLOR_FORMAT_NV12},
        {CNCODEC_PIX_FMT_NV21, CNEDK_BUF_COLOR_FORMAT_NV21},
    };
    return color_map[format];
  }

  // IVDecResult
  void OnFrame(cncodecFrame_t *codec_frame);
  void OnEos();
  void OnError(int errcode);

 public:
  void ReceiveFrame(cncodecFrame_t *codec_frame);
  void ReceiveSequence(cncodecDecSequenceInfo_t *seq_info);
  void ReceiveEOS();
  void HandleStreamCorrupt();
  void HandleStreamNotSupport();
  void HandleUnknownEvent(cncodecEventType_t type);

 private:
  int SetDecParams();
  void ResetFlags();

 private:
  std::atomic<int> cndec_buf_ref_count_{0};
  std::atomic<bool> eos_sent_{false};  // flag for cndec-eos has been sent to decoder
  std::atomic<bool> error_flag_{false};
  std::atomic<bool> created_{false};
  std::unique_ptr<std::promise<void>> eos_promise_;

  CnedkVdecCreateParams create_params_;
  cncodecDecCreateInfo_t create_info_;
  cncodecDecParams_t codec_params_;
  cncodecHandle_t instance_ = 0;
  int receive_seq_time_ = 0;
  void *vdec_ = nullptr;
};

}  // namespace cnedk

#endif  // CNEDK_DECODE_IMPL_MLU590_HPP_
