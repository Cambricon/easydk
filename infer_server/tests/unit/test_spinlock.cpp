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

#include <gtest/gtest.h>

#include <thread>

#include "test_base.h"
#include "util/spinlock.h"

namespace infer_server {

void AddOnShare(int* shared_number, SpinLock* lock, int times, int value) {
  do {
    lock->Lock();
    *shared_number += value;
    lock->Unlock();
  } while (--times);
}

TEST(InferServerUtil, SpinLock) {
  SpinLock lk;
  lk.Lock();
  ASSERT_TRUE(lk.IsLocked());
  lk.Unlock();
  ASSERT_FALSE(lk.IsLocked());
  ASSERT_NO_THROW(lk.Unlock());

  constexpr int parallel = 10;
  constexpr int times = 1000;
  constexpr int value = 7;
  int shared_value = 0;
  std::vector<std::thread> ths;
  ths.reserve(parallel);
  for (int i = 0; i < parallel; ++i) {
    ths.emplace_back(&AddOnShare, &shared_value, &lk, times, value);
  }
  for (auto& th : ths) {
    th.join();
  }
  EXPECT_EQ(shared_value, parallel * times * value);
}

void AddOnShare_Guard(int* shared_number, SpinLock* lock, int times, int value) {
  do {
    SpinLockGuard lk(*lock);
    *shared_number += value;
  } while (--times);
}

TEST(InferServerUtil, SpinLockGuard) {
  SpinLock lk;

  {
    SpinLockGuard guard(lk);
    ASSERT_TRUE(lk.IsLocked());
    ASSERT_NO_THROW(guard.Lock());
  }
  ASSERT_FALSE(lk.IsLocked());

  {
    SpinLockGuard guard(lk);
    ASSERT_TRUE(lk.IsLocked());
    guard.Unlock();
    ASSERT_FALSE(lk.IsLocked());
    ASSERT_NO_THROW(guard.Unlock());
    guard.Lock();
    ASSERT_TRUE(lk.IsLocked());
  }
  ASSERT_FALSE(lk.IsLocked());

  constexpr int parallel = 10;
  constexpr int times = 1000;
  constexpr int value = 3;
  int shared_value = 0;
  std::vector<std::thread> ths;
  ths.reserve(parallel);
  for (int i = 0; i < parallel; ++i) {
    ths.emplace_back(&AddOnShare_Guard, &shared_value, &lk, times, value);
  }
  for (auto& th : ths) {
    th.join();
  }
  EXPECT_EQ(shared_value, parallel * times * value);
}

}  // namespace infer_server