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

#include "cxxutil/log.h"
#include "device/mlu_context.h"
#include "easycodec/easy_decode.h"
#include "video_parser.h"

class StreamRunner {
 public:
  explicit StreamRunner(const std::string& data_path);
  virtual ~StreamRunner();
  void Start() { running_.store(true); }
  void Stop() {
    running_.store(false);
    cond_.notify_one();
  }

  bool RunLoop();
  virtual void Process(edk::CnFrame frame) = 0;
  void DemuxLoop(const uint32_t repeat_time);

  void ReceiveEos() { receive_eos_ = true; }
  void ReceiveFrame(const edk::CnFrame& info) {
    std::unique_lock<std::mutex> lk(mut_);
    frames_.push(info);
    cond_.notify_one();
  }

  bool Running() { return running_.load(); }

 protected:
  void WaitForRunLoopExit() {
    while (in_loop_.load()) {}
  }
  edk::MluContext env_;
  std::unique_ptr<edk::EasyDecode> decode_{nullptr};

 private:
  StreamRunner() = delete;
  class DemuxEventHandle : public IDemuxEventHandle {
   public:
    explicit DemuxEventHandle(StreamRunner* runner): runner_(runner) {}
    bool OnPacket(const edk::CnPacket& packet) override { return runner_->decode_->FeedData(packet, true); }
    void OnEos() override { LOGI(SAMPLES) << "capture EOS"; }
    void SendEos() {
      if (!send_eos_) {
        runner_->decode_->FeedEos();
        send_eos_ = true;
      }
    }
    bool Running() override {
      return runner_->Running();
    }
   private:
    StreamRunner* runner_;
    bool send_eos_{false};
  } demux_event_handle_;

  std::unique_ptr<VideoParser> parser_;
  std::queue<edk::CnFrame> frames_;
  std::mutex mut_;
  std::condition_variable cond_;
  std::string data_path_;
  std::atomic<bool> receive_eos_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> in_loop_{false};
};

#endif  // EDK_SAMPLES_RUNNER_H_

