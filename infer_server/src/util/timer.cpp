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

#include "util/timer.h"

#include <glog/logging.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace infer_server {

namespace detail {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::duration<uint32_t, std::milli>;

struct TimeEvent {
  TimePoint alarm_time;
  Timer::Notifier notifier;
  Duration d;
  bool loop;
  std::atomic<bool> notifying;
};

struct EventCompare {
  bool operator()(const TimeEvent* lhs, const TimeEvent* rhs) { return lhs->alarm_time < rhs->alarm_time; }
};

class TimeCounter {
 public:
  static inline TimeCounter* Instance() {
    static TimeCounter t;
    return &t;
  }
  void Loop() {
    while (running_.load()) {
      std::unique_lock<std::mutex> lk(mutex_);
      if (events_.empty()) {
        VLOG(6) << "no time event...";
        cond_.wait(lk, [this]() { return !running_.load() || !events_.empty(); });
        continue;
      }

      TimeEvent* e = *events_.begin();
      if (Clock::now() < e->alarm_time) {
        VLOG(7) << "wait for next time event";
        cond_.wait_until(lk, e->alarm_time);
        continue;
      }

      events_.erase(events_.begin());
      if (e->loop) {
        // add loop timer event back
        e->alarm_time += e->d;
        events_.insert(e);
        // use notifying flag to avoid lock on notifier
        // lock on notifier may cause dead-lock under the following circumstances:
        //   { user lock -> add / remove -> timer lock }
        //   { timer lock -> notifier -> user lock }
        e->notifying.store(true);
        lk.unlock();
        e->notifier();
        e->notifying.store(false);
      } else {
        e->notifying.store(true);
        lk.unlock();
        e->notifier();
        e->notifying.store(false);
        delete e;
      }
    }
  }

  int64_t Add(uint32_t t_ms, Timer::Notifier&& notifier, bool loop) {
    VLOG(6) << "Add time event, timeout: " << t_ms;
    Duration d(t_ms);
    std::unique_lock<std::mutex> lk(mutex_);
    auto te = new TimeEvent;
    te->alarm_time = Clock::now() + d;
    te->notifier = std::forward<Timer::Notifier>(notifier);
    te->d = std::move(d);
    te->loop = loop;
    te->notifying.store(false);
    events_.insert(te);
    lk.unlock();
    cond_.notify_one();
    return reinterpret_cast<int64_t>(te);
  }

  void Remove(int64_t handle) {
    std::unique_lock<std::mutex> lk(mutex_);
    auto e = std::find_if(events_.begin(), events_.end(),
                          [handle](const TimeEvent* t) { return reinterpret_cast<int64_t>(t) == handle; });
    if (e != events_.end()) {
      VLOG(6) << "Remove time event";
      // wait until event is not in notifying
      while ((*e)->notifying.load()) {
      }
      delete *e;
      events_.erase(e);
    }
    lk.unlock();
    cond_.notify_one();
  }

  ~TimeCounter() {
    running_.store(false);
    cond_.notify_one();
    if (th_.joinable()) th_.join();
    for (auto& e : events_) {
      delete e;
    }
    events_.clear();
  }

 private:
  TimeCounter() {
    running_.store(true);
    th_ = std::thread(&TimeCounter::Loop, this);
  }
  std::multiset<TimeEvent*, EventCompare> events_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread th_;
  std::atomic<bool> running_{false};
};

}  // namespace detail

bool Timer::Start(uint32_t t_ms, Notifier&& notifier, bool loop) {
  if (Idle()) {
    auto task = [this, notifier, loop]() {
      notifier();
      if (!loop) timer_id_.store(0);
    };
    timer_id_.store(detail::TimeCounter::Instance()->Add(t_ms, std::move(task), loop));
    return true;
  }
  return false;
}

void Timer::Cancel() {
  if (!Idle()) {
    detail::TimeCounter::Instance()->Remove(timer_id_.load());
    timer_id_.store(0);
  }
}

}  // namespace infer_server
