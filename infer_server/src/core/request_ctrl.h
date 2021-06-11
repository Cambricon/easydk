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

#ifndef INFER_SERVER_CORE_REQUEST_CTRL_H_
#define INFER_SERVER_CORE_REQUEST_CTRL_H_

#include <glog/logging.h>

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <utility>

#include "infer_server.h"
#include "util/spinlock.h"

namespace infer_server {

class RequestControl {
 public:
  using ResponseFunc = std::function<void(Status, PackagePtr)>;
  using NotifyFunc = std::function<void(const RequestControl*)>;

  RequestControl(ResponseFunc&& response, NotifyFunc&& done_notifier, const std::string& tag, int64_t request_id,
                 uint32_t data_num) noexcept
      : output_(new Package),
        response_(std::forward<ResponseFunc>(response)),
        done_notifier_(std::forward<NotifyFunc>(done_notifier)),
        tag_(tag),
        request_id_(request_id),
        data_num_(data_num),
        wait_num_(data_num),
        process_finished_(data_num ? false : true) {
    output_->data.resize(data_num);
    CHECK(response_) << "response cannot be null";
    CHECK(done_notifier_) << "notifier cannot be null";
  }

  ~RequestControl() {
    SpinLockGuard lk(done_mutex_);
    if (resp_done_cb_) {
      resp_done_cb_();
    }
    response_done_flag_.set_value();
  }

  /* ---------------------------- Observer --------------------------------*/
  const std::string& Tag() const noexcept { return tag_; }
  int64_t RequestId() const noexcept { return request_id_; }
  uint32_t DataNum() const noexcept { return data_num_; }

  bool IsSuccess() const noexcept { return status_.load() == Status::SUCCESS; }
  bool IsDiscarded() const noexcept { return is_discarded_.load(); }
  bool IsProcessFinished() const noexcept { return process_finished_.load(); }
  /* -------------------------- Observer END ------------------------------*/

#ifdef CNIS_RECORD_PERF
  void BeginRecord() noexcept { start_time_ = std::chrono::steady_clock::now(); }

  float EndRecord() noexcept {
    // record request latency
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<float, std::milli> dura = end - start_time_;
    float latency = dura.count();
    output_->perf["RequestLatency"] = latency;
    return latency;
  }

  // invoked only before response
  const std::map<std::string, float>& Performance() const noexcept { return output_->perf; }
#endif

  void SetResponseDoneCallback(std::function<void()>&& callback) {
    resp_done_cb_ = std::move(callback);
  }

  void Response() noexcept {
    response_(status_.load(), std::move(output_));
    VLOG(6) << "response end) request id: " << request_id_;
  }

  std::future<void> ResponseDonePromise() noexcept { return response_done_flag_.get_future(); }

  void Discard() noexcept { is_discarded_.store(true); }

  void ProcessFailed(Status status) noexcept { ProcessDone(status, nullptr, 0, {}); }

  // process on one piece of data done
  void ProcessDone(Status status, InferDataPtr output, uint32_t index, std::map<std::string, float> perf) noexcept;

 private:
  RequestControl() = delete;
  PackagePtr output_;
  ResponseFunc response_;
  NotifyFunc done_notifier_;
  std::function<void()> resp_done_cb_{nullptr};
  std::string tag_;
  SpinLock done_mutex_;
  std::promise<void> response_done_flag_;
  int64_t request_id_;
  uint32_t data_num_;
  uint32_t wait_num_;
  std::atomic<Status> status_{Status::SUCCESS};
  std::atomic<bool> is_discarded_{false};
  std::atomic<bool> process_finished_{false};
#ifdef CNIS_RECORD_PERF
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
#endif
};

}  // namespace infer_server

#endif  // INFER_SERVER_CORE_REQUEST_CTRL_H_
