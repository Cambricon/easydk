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

#ifndef INFER_SERVER_CORE_SESSION_H_
#define INFER_SERVER_CORE_SESSION_H_

#include <glog/logging.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cache.h"
#include "engine.h"
#include "infer_server.h"
#include "priority.h"
#include "request_ctrl.h"
#include "util/thread_pool.h"

namespace infer_server {

class Executor;
using Executor_t = Executor*;

class Session {
 public:
  Session(const std::string& name, Executor_t executor, bool sync_link, bool show_perf) noexcept
      : name_(name), executor_(executor), running_(true), is_sync_link_(sync_link) {
#ifdef CNIS_RECORD_PERF
    if (show_perf) {
      // print performance information every 2 second
      perf_timer_.NotifyEvery(2000, &Session::PrintPerformance, this);
    }
#endif
  }

  ~Session() {
    if (running_.load()) {
      running_.store(false);
    }
    auto check = [this]() { return request_list_.empty() && !in_response_.load(); };
    std::unique_lock<std::mutex> lk_tail(response_mutex_);
    if (!check()) {
      VLOG(3) << "session " << name_ << " wait all task done in destructor";
      sync_cond_.wait(lk_tail, check);
    }
    lk_tail.unlock();

#ifdef CNIS_RECORD_PERF
    // stop print perf
    if (!perf_timer_.Idle()) {
      PrintPerformance();
      perf_timer_.Cancel();
    }
#endif
  }

  /* ---------------- Observer -------------------*/
  const std::string& GetName() const noexcept { return name_; }
  Executor_t GetExecutor() const noexcept { return executor_; }
  Observer* GetRawObserver() const noexcept { return observer_.get(); }
  bool IsSyncLink() const noexcept { return is_sync_link_; }
  /* -------------- Observer END -----------------*/

  void SetObserver(std::shared_ptr<Observer> observer) noexcept { observer_ = std::move(observer); }

  RequestControl* Send(PackagePtr&& data, std::function<void(Status, PackagePtr)>&& notifier) noexcept;

  void CheckAndResponse(const RequestControl* caller) noexcept;

  void WaitTaskDone(const std::string& tag) noexcept {
    VLOG(3) << "session " << name_ << " wait [" << tag << "] task done";
    std::vector<std::string> match = {tag};
    std::lock(request_mutex_, response_mutex_);
    std::unique_lock<std::mutex> lk_head(request_mutex_, std::adopt_lock);
    std::unique_lock<std::mutex> lk_tail(response_mutex_, std::adopt_lock);
    auto last = std::find_first_of(request_list_.rbegin(), request_list_.rend(), match.begin(), match.end(),
                                   [](RequestControl* c, const std::string& t) { return c->Tag() == t; });
    if (last == request_list_.rend()) return;
    std::future<void> flag = (*last)->ResponseDonePromise();
    lk_head.unlock();
    lk_tail.unlock();
    flag.get();
  }

  void DiscardTask(const std::string& tag) noexcept {
    std::lock(request_mutex_, response_mutex_);
    std::unique_lock<std::mutex> lk_head(request_mutex_, std::adopt_lock);
    std::unique_lock<std::mutex> lk_tail(response_mutex_, std::adopt_lock);
    std::for_each(request_list_.begin(), request_list_.end(), [&tag](RequestControl* it) {
      if (it->Tag() == tag) {
        it->Discard();
      }
    });
  }

#ifdef CNIS_RECORD_PERF
  void RecordPerformance(const std::string& perf_key, uint32_t unit_cnt, float time_ms) noexcept {
    std::unique_lock<std::mutex> lk(perf_mutex_);
    if (!perf_record_.count(perf_key)) {
      perf_record_[perf_key] = PerfStatistic();
    }

    auto& perf = perf_record_[perf_key];
    perf.unit_cnt += unit_cnt;
    perf.total += time_ms;
    float ave = time_ms / unit_cnt;
    if (ave > perf.max) perf.max = ave;
    if (ave < perf.min) perf.min = ave;
  }

  const std::map<std::string, PerfStatistic>& GetPerformance() const noexcept { return perf_record_; }

  void PrintPerformance() {
    std::unique_lock<std::mutex> lk(perf_mutex_);
    if (perf_record_.empty()) return;
    printf("\n-------------------------------- %s --------------------------------\n", name_.c_str());
    for (auto& p : perf_record_) {
      if (p.first == "Batch") {
        printf("  %-16s: total unit %d, unit count %-u, max %d, min %d, average %.3f\n", p.first.c_str(),
               static_cast<int>(p.second.total), p.second.unit_cnt, static_cast<int>(p.second.max),
               static_cast<int>(p.second.min), p.second.total / p.second.unit_cnt);
      } else {
        printf("  %-16s: total time %.3f ms, unit count %-u, max %.3f, min %.3f, average %.3f\n", p.first.c_str(),
               p.second.total, p.second.unit_cnt, p.second.max, p.second.min, p.second.total / p.second.unit_cnt);
      }
    }
    printf("-------------------------------- %s END --------------------------------\n\n", name_.c_str());
  }
#endif

 private:
  std::string name_;
  Executor_t executor_;
  std::mutex request_mutex_;
  std::mutex response_mutex_;
  std::condition_variable sync_cond_;
  std::list<RequestControl*> request_list_;
  std::shared_ptr<Observer> observer_{nullptr};

#ifdef CNIS_RECORD_PERF
  // performance statistics
  std::map<std::string, PerfStatistic> perf_record_;
  std::mutex perf_mutex_;
  Timer perf_timer_;
#endif

  int64_t request_id_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> in_response_{false};
  bool is_sync_link_{false};
};  // class Session

class Executor {
 public:
  Executor(SessionDesc desc, PriorityThreadPool* tp, int device_id);

  ~Executor();

  void Link(Session_t session) noexcept {
    std::unique_lock<std::mutex> lk(link_mutex_);
    VLOG(3) << "executor " << desc_.name << "] link session " << session->GetName();
    link_set_.insert(session);
  }

  void Unlink(Session_t session) noexcept {
    std::unique_lock<std::mutex> lk(link_mutex_);
    if (link_set_.count(session)) {
      VLOG(3) << "executor " << desc_.name << "] unlink session " << session->GetName();
      link_set_.erase(session);
    } else {
      LOG(WARNING) << "no such session in this executor";
    }
  }

  bool WaitIfCacheFull(int timeout) noexcept { return cache_->WaitIfFull(timeout); }

  /* ------------------- Observer --------------------- */
  size_t GetSessionNum() noexcept {
    std::unique_lock<std::mutex> lk(link_mutex_);
    return link_set_.size();
  }
  const SessionDesc& GetDesc() const noexcept { return desc_; }
  const Priority& GetPriority() const noexcept { return cache_->GetPriority(); }
  std::string GetName() const noexcept { return desc_.name; }
  uint32_t GetEngineNum() const noexcept { return desc_.engine_num; }
  PriorityThreadPool* GetThreadPool() const noexcept { return tp_; }
  /* ----------------- Observer END ------------------- */

  bool Upload(PackagePtr&& pack) noexcept { return cache_->Push(std::move(pack)); }

  void DispatchLoop() noexcept;

 private:
  SessionDesc desc_;
  PriorityThreadPool* tp_;
  std::unique_ptr<CacheBase> cache_;

  // manage link
  std::set<Session_t> link_set_;
  std::mutex link_mutex_;

  // dispatch to engine
  std::vector<std::unique_ptr<Engine>> engines_;
  std::atomic<Engine*> idle_{nullptr};
  std::thread dispatch_thread_;
  std::mutex dispatch_mutex_;
  std::condition_variable dispatch_cond_;

  PerfStatistic batch_record_;
  std::atomic_bool running_{false};
  int device_id_;
};  // class Executor

}  // namespace infer_server

#endif  // INFER_SERVER_CORE_SESSION_H_
