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

#ifndef EDK_SAMPLES_RUNNER_H_
#define EDK_SAMPLES_RUNNER_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <iostream>

#include "cnedk_buf_surface.h"
#include "cnedk_decode.h"

#include "video_decoder.h"
#include "video_parser.h"

class StreamRunner : public IDecodeEventHandle {
 public:
  explicit StreamRunner(const std::string& data_path,
                        const VideoDecoder::DecoderType decode_type = VideoDecoder::EASY_DECODE,
                        int dev_id = 0);
  virtual ~StreamRunner();
  void Start() { running_.store(true); }
  void Stop() {
    running_.store(false);
    cond_.notify_one();
  }

  bool RunLoop();
  virtual void Process(CnedkBufSurface* surf) = 0;
  void DemuxLoop(const uint32_t repeat_time);

  void OnDecodeEos() override {
    std::unique_lock<std::mutex> lk(eos_mut_);
    frames_.push(nullptr);
    cond_.notify_one();
    receive_eos_ = true;
    eos_cond_.notify_one();
  }

  void OnDecodeFrame(CnedkBufSurface* surf) override {
    std::unique_lock<std::mutex> lk(mut_);
    frames_.push(surf);
    cond_.notify_one();
  }
  int GetDeviceId() { return device_id_; }

  bool Running() const { return running_.load(); }

 protected:
  void WaitForRunLoopExit() {
    while (in_loop_.load()) {}
  }

  std::unique_ptr<VideoDecoder> decoder_;

 private:
  StreamRunner() = delete;

  int device_id_ {0};
  std::unique_ptr<VideoParser> parser_;
  std::queue<CnedkBufSurface*> frames_;
  std::mutex mut_;
  std::condition_variable cond_;
  std::string data_path_;
  std::mutex eos_mut_;
  std::condition_variable eos_cond_;
  std::atomic<bool> receive_eos_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> in_loop_{false};
};

#endif  // EDK_SAMPLES_RUNNER_H_

