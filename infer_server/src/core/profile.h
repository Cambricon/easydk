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

#ifndef INFER_SERVER_CORE_PROFILE_H_
#define INFER_SERVER_CORE_PROFILE_H_

#include <chrono>
#include <unordered_map>

#include "infer_server.h"
#include "util/spinlock.h"
#include "util/timer.h"

namespace infer_server {

namespace detail {
template <typename T>
struct is_ratio : std::false_type {};

template <std::intmax_t Num, std::intmax_t Denom>
struct is_ratio<std::ratio<Num, Denom>> : std::true_type {};
}  // namespace detail

/// helper class for std::chrono::steady_clock
class Clock {
 public:
  using clock = std::chrono::steady_clock;
  using time_point = decltype(clock::now());

  static inline time_point Now() { return clock::now(); }

  template <typename Ratio = std::milli, typename = std::enable_if<detail::is_ratio<Ratio>::value>>
  static inline float Duration(const time_point& start, const time_point& end) {
    return std::chrono::duration<float, Ratio>(end - start).count();
  }

  template <typename Ratio = std::milli, typename = std::enable_if<detail::is_ratio<Ratio>::value>>
  static inline float DurationSince(const time_point& before) {
    return std::chrono::duration<float, Ratio>(Now() - before).count();
  }
};

class LatencyRecorder {
 public:
  void RecordPerformance(const std::string& perf_key, uint32_t unit_cnt, float time_ms) noexcept {
    std::unique_lock<std::mutex> lk(latency_mutex_);

    auto& perf = latency_record_[perf_key];
    perf.unit_cnt += unit_cnt;
    perf.total += time_ms;
    float ave = time_ms / unit_cnt;
    if (ave > perf.max) perf.max = ave;
    if (ave < perf.min) perf.min = ave;
  }

  void PrintPerformance(const std::string& name) {
    std::unique_lock<std::mutex> lk(latency_mutex_);
    if (latency_record_.empty()) return;
    printf("\n-------------------------------- %s --------------------------------\n", name.c_str());
    for (auto& p : latency_record_) {
      printf("  %-16s: total time %.3f ms, unit count %-u, max %.3f, min %.3f, average %.3f\n", p.first.c_str(),
             p.second.total, p.second.unit_cnt, p.second.max, p.second.min, p.second.total / p.second.unit_cnt);
    }
    printf("-------------------------------- %s END --------------------------------\n\n", name.c_str());
  }

  const std::map<std::string, LatencyStatistic>& GetPerformance() const noexcept { return latency_record_; }

 private:
  std::map<std::string, LatencyStatistic> latency_record_;
  std::mutex latency_mutex_;
};

class Profiler : public Clock {
 public:
  Profiler() : last_update_(Now()) {}

  void Init(bool self_update = true) {
    if (self_update) {
      measure_timer_.NotifyEvery(period_interval_, &Profiler::Update, this);
    }
  }

  struct Throughout {
    float latest{0};
    float max{0};
    float min{std::numeric_limits<float>::max()};
  };

  void RequestStart() noexcept {
    SpinLockGuard lk(mutex_);
    if (processing_cnt_ == 0u) {
      start_time_ = Now();
    }

    ++processing_cnt_;
  }

  void Update() {
    SpinLockGuard lk(mutex_);
    Refresh();
  }

  void RequestEnd(uint32_t unit_cnt) noexcept {
    CHECK_NE(processing_cnt_, 0u);
    SpinLockGuard lk(mutex_);
    ++request_cnt_, ++period_request_cnt_;
    unit_cnt_ += unit_cnt;
    period_unit_cnt_ += unit_cnt;
    if (--processing_cnt_ == 0u) {
      total_ += DurationSince<std::milli>(start_time_);
    }
  }

  float RequestPerSecond() const noexcept {
    // if some request in processing, total_time = total_time + (now - last_start)
    return request_cnt_ * 1e3 / (total_ + (processing_cnt_ == 0u ? 0 : DurationSince(start_time_)));
  }

  float UnitPerSecond() const noexcept {
    // if some request in processing, total_time = total_time + (now - last_start)
    return unit_cnt_ * 1e3 / (total_ + (processing_cnt_ == 0u ? 0 : DurationSince(start_time_)));
  }

  float RequestThroughoutRealtime() const noexcept { return rps_rt_.latest; }

  float UnitThroughoutRealtime() const noexcept { return ups_rt_.latest; }

  ThroughoutStatistic Summary() noexcept {
    ThroughoutStatistic ret;
    ret.request_cnt = request_cnt_;
    ret.unit_cnt = unit_cnt_;
    ret.rps = RequestPerSecond();
    ret.ups = UnitPerSecond();
    ret.rps_rt = rps_rt_.latest;
    ret.ups_rt = ups_rt_.latest;
    return ret;
  }

 protected:
  void Refresh() {
    auto now = Now();
    float period = Duration(last_update_, now);
    last_update_ = now;
    rps_rt_.latest = period_request_cnt_ * 1e3 / period;
    ups_rt_.latest = period_unit_cnt_ * 1e3 / period;
    rps_rt_.max = rps_rt_.max < rps_rt_.latest ? rps_rt_.latest : rps_rt_.max;
    rps_rt_.min = rps_rt_.min > rps_rt_.latest ? rps_rt_.latest : rps_rt_.min;
    ups_rt_.max = ups_rt_.max < ups_rt_.latest ? ups_rt_.latest : ups_rt_.max;
    ups_rt_.min = ups_rt_.min > ups_rt_.latest ? ups_rt_.latest : ups_rt_.min;
    period_request_cnt_ = 0;
    period_unit_cnt_ = 0;
  }

  SpinLock mutex_;
  Timer measure_timer_;
  time_point start_time_{time_point::min()};
  time_point last_update_;
  double total_{0};
  // total request num
  uint32_t request_cnt_{0};
  uint32_t processing_cnt_{0};
  uint32_t unit_cnt_{0};

  static constexpr uint32_t period_interval_{2000};
  uint32_t period_request_cnt_{0};
  Throughout rps_rt_;

  uint32_t period_unit_cnt_{0};
  Throughout ups_rt_;
};

class TagSetProfiler {
 public:
  TagSetProfiler() = default;

  void SetSelfUpdate(bool self_update) noexcept { self_update_ = self_update; }

  void RequestStart(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (!profilers_.count(tag)) {
      profilers_[tag].Init(false);
    }
    profilers_[tag].RequestStart();
  }

  void RequestEnd(const std::string& tag, uint32_t unit_cnt) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    // cannot find tag at request end means tag has been discarded
    if (profilers_.count(tag)) {
      profilers_[tag].RequestEnd(unit_cnt);
    }
  }

  void RemoveTag(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    profilers_.erase(tag);
  }

  float RequestPerSecond(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (profilers_.count(tag)) {
      return profilers_[tag].RequestPerSecond();
    }
    LOG(WARNING) << "Tag [" << tag << "] not exist";
    return 0;
  }

  float UnitPerSecond(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (profilers_.count(tag)) {
      return profilers_[tag].UnitPerSecond();
    }
    LOG(WARNING) << "Tag [" << tag << "] not exist";
    return 0;
  }

  float RequestThroughoutRealtime(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (profilers_.count(tag)) {
      return profilers_[tag].RequestThroughoutRealtime();
    }
    LOG(WARNING) << "Tag [" << tag << "] not exist";
    return 0;
  }

  float UnitThroughoutRealtime(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (profilers_.count(tag)) {
      return profilers_[tag].UnitThroughoutRealtime();
    }
    LOG(WARNING) << "Tag [" << tag << "] not exist";
    return 0;
  }

  ThroughoutStatistic Summary(const std::string& tag) noexcept {
    SpinLockGuard lk(profilers_mutex_);
    if (profilers_.count(tag)) {
      return profilers_[tag].Summary();
    }
    LOG(WARNING) << "Tag [" << tag << "] not exist";
    return {};
  }

  float RequestPerSecond() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    float total = 0;
    for (auto& p : profilers_) {
      total += p.second.RequestPerSecond();
    }
    return total;
  }

  float UnitPerSecond() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    float total = 0;
    for (auto& p : profilers_) {
      total += p.second.UnitPerSecond();
    }
    return total;
  }

  float RequestThroughoutRealtime() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    float total = 0;
    for (auto& p : profilers_) {
      total += p.second.RequestThroughoutRealtime();
    }
    return total;
  }

  float UnitThroughoutRealtime() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    float total = 0;
    for (auto& p : profilers_) {
      total += p.second.UnitThroughoutRealtime();
    }
    return total;
  }

  ThroughoutStatistic Summary() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    ThroughoutStatistic ret;
    for (auto& p : profilers_) {
      auto tmp = p.second.Summary();
      ret.request_cnt += tmp.request_cnt;
      ret.unit_cnt += tmp.unit_cnt;
      ret.rps += tmp.rps;
      ret.ups += tmp.ups;
      ret.rps_rt += tmp.rps_rt;
      ret.ups_rt += tmp.ups_rt;
    }
    return ret;
  }

  void Update() noexcept {
    SpinLockGuard lk(profilers_mutex_);
    for (auto& p : profilers_) {
      p.second.Update();
    }
  }

 private:
  std::unordered_map<std::string, Profiler> profilers_;
  SpinLock profilers_mutex_;
  bool self_update_{true};
};

}  // namespace infer_server

#endif  // INFER_SERVER_CORE_PROFILE_H_
