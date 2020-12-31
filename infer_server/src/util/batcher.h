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

#ifndef INFER_SERVER_UTIL_BATCHER_H_
#define INFER_SERVER_UTIL_BATCHER_H_

#include <glog/logging.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "util/timer.h"

namespace infer_server {

template <class item_type>
class Batcher {
 public:
  using notifier_type = std::function<void(std::vector<item_type>&&)>;

  // timeout == 0 means no timeout
  Batcher(notifier_type notifier, uint32_t timeout, uint32_t batch_size)
      : notifier_(notifier), timeout_(timeout), batch_size_(batch_size) {
    CHECK(batch_size) << "batch size is 0!";
    VLOG(4) << "batcher] -------batch timeout " << timeout_ << " ms";
    VLOG(4) << "batcher] -------batch size " << batch_size_;
    cache_.reserve(batch_size_);
    first_item_.store(true);
  }

  void AddItem(const item_type& item) {
    VLOG(8) << "batcher add one item";
    std::unique_lock<std::mutex> lk(cache_mutex_);
    if (timeout_ && first_item_.load()) {
      timer_.Cancel();
      timer_.NotifyAfter(timeout_, &Batcher<item_type>::Emit, this);
      first_item_.store(false);
    }
    cache_.emplace_back(item);
    if (cache_.size() > batch_size_ - 1) {
      Notify(std::move(lk));
    }
  }

  void AddItem(item_type&& item) {
    VLOG(8) << "batcher add one item";
    std::unique_lock<std::mutex> lk(cache_mutex_);
    if (timeout_ && first_item_.load()) {
      timer_.Cancel();
      timer_.NotifyAfter(timeout_, &Batcher<item_type>::Emit, this);
      first_item_.store(false);
    }
    cache_.emplace_back(std::forward<item_type>(item));
    if (cache_.size() > batch_size_ - 1) {
      Notify(std::move(lk));
    }
  }

  size_t Size() noexcept {
    std::unique_lock<std::mutex> lk(cache_mutex_);
    return cache_.size();
  }

  void Emit() {
    Notify(std::unique_lock<std::mutex>(cache_mutex_));
  }

 private:
  void Notify(std::unique_lock<std::mutex> lk) {
    if (cache_.empty()) {
      return;
    }
    std::vector<item_type> tmp_cache;
    tmp_cache.swap(cache_);
    first_item_.store(true);
    cache_.reserve(batch_size_);
    lk.unlock();

    VLOG(5) << "emit a batch, batch_size: " << tmp_cache.size();
    if (notifier_) {
      notifier_(std::move(tmp_cache));
    } else {
      LOG(WARNING) << "Batcher donot have notifier, do nothing";
    }
  }

  Batcher() = delete;
  Batcher(const Batcher&) = delete;
  Batcher& operator=(const Batcher&) = delete;

  std::vector<item_type> cache_;
  std::mutex cache_mutex_;
  notifier_type notifier_;
  Timer timer_;
  uint32_t timeout_;
  uint32_t batch_size_;
  std::atomic<bool> first_item_;
};  // class Batcher

}  // namespace infer_server

#endif  // INFER_SERVER_UTIL_BATCHER_H_
