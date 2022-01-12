/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#ifndef EDK_SAMPLES_TRANSCODE_RUNNER_H_
#define EDK_SAMPLES_TRANSCODE_RUNNER_H_

#include <memory>
#include <string>

#include "device/mlu_context.h"
#include "easycodec/easy_encode.h"
#include "resize_yuv.h"
#include "runner.h"

class TranscodeRunner : public StreamRunner {
 public:
  TranscodeRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                  const std::string& data_path, const std::string& output_file_name,
                  int dst_width, int dst_height, double dst_frame_rate);
  ~TranscodeRunner();

  void Process(edk::CnFrame frame) override;

 private:
  void EosCallback();
  void PacketCallback(const edk::CnPacket &packet);

  std::unique_ptr<edk::EasyEncode> encode_{nullptr};
#ifdef HAVE_CNCV
  std::unique_ptr<CncvResizeYuv> resize_{nullptr};
#endif
  double dst_frame_rate_;
  int dst_width_;
  int dst_height_;
  std::string output_file_name_;
  bool jpeg_encode_;
  std::string file_name_;
  std::string file_extension_;
  size_t frame_count_ = 0;
  std::ofstream file_;
  std::mutex encode_eos_mut_;
  std::condition_variable encode_eos_cond_;
  std::atomic<bool> encode_received_eos_;
};

#endif  // EDK_SAMPLES_TRANSCODE_RUNNER_H_
