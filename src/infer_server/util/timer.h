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

#ifndef INFER_SERVER_UTIL_TIMER_H_
#define INFER_SERVER_UTIL_TIMER_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace infer_server {

/**
 * @brief A timer
 *
 * @note An instance only holds one task at a time.
 */
class Timer {
 public:
  using Notifier = std::function<void()>;

  /**
   * @brief Start a timer. Invoke notifier after specified interval time
   *
   * @tparam callable Type of callable object
   * @tparam arguments Type of arguments passed to callable
   *
   * @param t_ms Specified interval time
   * @param f Callable object to be invoked
   * @param args Arguments passed to callable
   *
   * @retval true Success
   * @retval false Timer is busy
   */
  template <typename callable, typename... arguments, typename = std::result_of<callable(arguments...)>>
  bool NotifyAfter(uint32_t t_ms, callable&& f, arguments&&... args) {
    auto task = std::bind(std::forward<callable>(f), std::forward<arguments>(args)...);
    return Start(t_ms, std::move(task), false);
  }

  /**
   * @brief Start a loop timer. Invoke notifier every interval time
   *
   * @tparam callable Type of callable object
   * @tparam arguments Type of arguments passed to callable
   *
   * @param t_ms Specified interval time
   * @param f Callable object to be invoked
   * @param args Arguments passed to callable
   *
   * @retval true Success
   * @retval false Timer is busy
   */
  template <typename callable, typename... arguments, typename = std::result_of<callable(arguments...)>>
  bool NotifyEvery(uint32_t t_ms, callable&& f, arguments&&... args) {
    auto task = std::bind(std::forward<callable>(f), std::forward<arguments>(args)...);
    return Start(t_ms, std::move(task), true);
  }

  /**
   * @brief Cancel the held task
   */
  void Cancel();

  /**
   * @brief Check if this timer holds task
   *
   * @retval true This timer has not held a task
   * @retval false This timer has held a task
   */
  bool Idle() { return !timer_id_.load(); }

  /**
   * @brief Destroy the Timer object
   */
  ~Timer() {
    if (!Idle()) Cancel();
  }

 private:
  bool Start(uint32_t t_ms, Notifier&& notifier, bool loop = false);
  std::atomic<int64_t> timer_id_{0};
};  // class Timer

}  // namespace infer_server

#endif  // INFER_SERVER_UTIL_TIMER_H_
