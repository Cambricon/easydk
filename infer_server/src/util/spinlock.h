/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

/**
 * @file spinlock.h
 *
 * This file contains a declaration of the SpinLock class, and helper class SpinLockGuard.
 */

#ifndef INFER_SERVER_UTIL_SPINLOCK_H_
#define INFER_SERVER_UTIL_SPINLOCK_H_

#include <atomic>

namespace infer_server {

/**
 * @brief Spin lock implementation using atomic_flag and memory_order
 */
class SpinLock {
 public:
  /**
   * @brief Lock the spinlock, blocks if the atomic_flag is not available
   */
  void Lock() {
    while (true) {
      if (!lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }

      while (lock_.load(std::memory_order_relaxed)) {
      }
    }
  }

  /**
   * @brief Unlock the spinlock
   */
  void Unlock() { lock_.store(false, std::memory_order_release); }

  /**
   * @brief Query lock status
   * @return ture if is locked
   */
  bool IsLocked() { return lock_.load(); }

 private:
  std::atomic_bool lock_{false};
};

/**
 * @brief Spin lock helper class, provide RAII management
 */
class SpinLockGuard {
 public:
  /**
   * Constructor, lock the spinlock in construction
   * @param lock Spin lock instance.
   */
  explicit SpinLockGuard(SpinLock &lock) : lock_(lock) {  // NOLINT
    lock_.Lock();
    is_locked_.store(true);
  }

  /**
   * @brief Lock the spinlock if have not been locked by this guard
   */
  void Lock() {
    if (!is_locked_.load()) {
      lock_.Lock();
      is_locked_.store(true);
    }
  }

  /**
   * @brief Unlock the spinlock if have been locked by this guard
   */
  void Unlock() {
    if (is_locked_.load()) {
      lock_.Unlock();
      is_locked_.store(false);
    }
  }

  /**
   * Destructor, unlock the spinlock in destruction
   */
  ~SpinLockGuard() {
    if (is_locked_.load()) {
      lock_.Unlock();
    }
    is_locked_.store(false);
  }

 private:
  SpinLock &lock_;
  std::atomic_bool is_locked_{false};
};

}  // namespace infer_server

#endif  // INFER_SERVER_UTIL_SPINLOCK_H_
