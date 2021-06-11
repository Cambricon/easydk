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

#ifndef INFER_SERVER_CORE_CACHE_H_
#define INFER_SERVER_CORE_CACHE_H_

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <utility>

#include "infer_server.h"
#include "priority.h"
#include "request_ctrl.h"
#include "util/batcher.h"

namespace infer_server {

class CacheBase {
 public:
  CacheBase(uint32_t batch_size, const Priority& priority)
      : batch_size_(batch_size), priority_(priority) {}
  virtual ~CacheBase() = default;

  /* ---------------- Observer -------------------*/
  const Priority& GetPriority() const noexcept { return priority_; }
  bool Running() const noexcept { return running_.load(); }
  uint32_t BatchSize() const noexcept { return batch_size_; }
  /* -------------- Observer END -----------------*/

  virtual void Start() noexcept { running_.store(true); }

  virtual void Stop() noexcept {
    running_.store(false);
    cache_cond_.notify_all();
  }

  bool Push(PackagePtr&& pack) noexcept {
    CHECK(pack);
    if (!Running()) return false;
    Enqueue(std::forward<PackagePtr>(pack));
    return true;
  }

  PackagePtr Pop() noexcept {
    std::unique_lock<std::mutex> cache_lk(cache_mutex_);
    if (cache_.empty()) {
      cache_cond_.wait(cache_lk, [this]() { return !cache_.empty() || !running_.load(); });
      if (!running_.load() && cache_.empty()) {
        // end loop, exit thread
        return nullptr;
      }
    }
    PackagePtr pack = cache_.front();
    // check discard
    if (std::find_if(pack->data.begin(), pack->data.end(),
                     [](const InferDataPtr& it) { return it->ctrl->IsDiscarded(); }) != pack->data.end()) {
      ClearDiscard(pack);
      if (cache_.empty()) {
        cache_lk.unlock();
        return nullptr;
      }
      pack = cache_.front();
    }
    cache_.pop_front();
    cache_lk.unlock();
    cache_cond_.notify_one();

    return pack;
  }

 protected:
  virtual void Enqueue(PackagePtr&& pack) noexcept = 0;
  virtual void ClearDiscard(PackagePtr pack) noexcept = 0;

 protected:
  std::list<PackagePtr> cache_;
  std::mutex cache_mutex_;
  std::condition_variable cache_cond_;

 private:
  uint32_t batch_size_;
  Priority priority_;
  std::atomic<bool> running_{false};
};

class CacheDynamic : public CacheBase {
 public:
  CacheDynamic(uint32_t batch_size, const Priority& priority, uint32_t batch_timeout)
      : CacheBase(batch_size, priority) {
    batcher_.reset(new Batcher<InferDataPtr>(
        [this](BatchData&& data) {
          auto pack = std::make_shared<Package>();
          pack->priority = GetPriority().Get(-data.at(0)->ctrl->RequestId());
          pack->data = std::move(data);
          std::unique_lock<std::mutex> lk(cache_mutex_);
          cache_.push_back(pack);
          lk.unlock();
          cache_cond_.notify_all();
        },
        batch_timeout, BatchSize()));
  }

  ~CacheDynamic() {
    // batcher should be clear
    CHECK_EQ(batcher_->Size(), 0u) << "Executor Destruction] Batcher should not have any data";
  }

  virtual void Stop() noexcept override {
    CacheBase::Stop();
    batcher_->Emit();
    cache_cond_.notify_all();
  }

 protected:
  // rebatch
  void ClearDiscard(PackagePtr pack) noexcept override {
    std::list<InferDataPtr> cache;
    VLOG(4) << "Clear discarded cached data";
    do {
      cache_.pop_front();
      for (auto& it : pack->data) {
        RequestControl* ctrl = it->ctrl;
        if (!ctrl->IsDiscarded()) {
          cache.emplace_back(std::move(it));
        } else {
          ctrl->ProcessFailed(Status::SUCCESS);
        }
      }
      pack = cache_.empty() ? nullptr : cache_.front();
    } while (pack);
    pack.reset(new Package);
    while (!cache.empty()) {
      pack->data.emplace_back(cache.front());
      cache.pop_front();
      if (pack->data.size() >= BatchSize() || cache.empty()) {
        cache_.push_back(pack);
        if (!cache.empty()) pack.reset(new Package);
      }
    }
  }

  void Enqueue(PackagePtr&& pack) noexcept override {
    for (auto& it : pack->data) {
      CHECK(it->ctrl);
      batcher_->AddItem(std::move(it));
    }
  }

 private:
  std::unique_ptr<Batcher<InferDataPtr>> batcher_;
};

class CacheStatic : public CacheBase {
 public:
  CacheStatic(uint32_t batch_size, const Priority& priority)
      : CacheBase(batch_size, priority) {}

 protected:
  // won't rebatch
  void ClearDiscard(PackagePtr pack) noexcept override {
    std::list<PackagePtr> cache;
    do {
      cache_.pop_front();
      if (!pack->data[0]->ctrl->IsDiscarded()) {
        cache.emplace_back(std::move(pack));
      } else {
        for (auto& it : pack->data) {
          it->ctrl->ProcessFailed(Status::SUCCESS);
        }
      }
      pack = cache_.empty() ? nullptr : cache_.front();
    } while (pack);
    while (!cache.empty()) {
      cache_.emplace_back(cache.front());
      cache.pop_front();
    }
  }

  inline void ThreadsafePush(PackagePtr&& in) noexcept {
    std::unique_lock<std::mutex> lk(cache_mutex_);
    cache_.emplace_back(std::forward<PackagePtr>(in));
    lk.unlock();
    cache_cond_.notify_one();
  }

  void Enqueue(PackagePtr&& in) noexcept override {
    // input continuous data
    size_t data_size = in->data.size();
    if (in->predict_io && in->predict_io->HasValue()) {
      ThreadsafePush(std::move(in));
      return;
    }

    size_t batch_idx = 0;
    auto pack = std::make_shared<Package>();
    // Static strategy is unable to process a Package that contains more than batch_size
    for (size_t idx = 0; idx < data_size; ++idx) {
      CHECK(in->data[idx]->ctrl);
      pack->data.emplace_back(std::move(in->data[idx]));
      if (++batch_idx > BatchSize() - 1 || idx == data_size - 1) {
        pack->priority = GetPriority().Get(-pack->data.at(0)->ctrl->RequestId());
        ThreadsafePush(std::move(pack));
        batch_idx = 0;
        if (idx != data_size - 1) pack = std::make_shared<Package>();
      }
    }
  }
};

}  // namespace infer_server
#endif  // INFER_SERVER_CORE_CACHE_H_
