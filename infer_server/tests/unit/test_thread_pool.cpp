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

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "util/thread_pool.h"

TEST(InferServerUtil, EqualityThreadPool) {
  infer_server::EqualityThreadPool tp(nullptr, 5);
  EXPECT_EQ(5u, tp.Size());
  tp.Resize(10);
  EXPECT_EQ(10u, tp.Size());
  tp.Resize(3);
  EXPECT_EQ(3u, tp.Size());
  tp.Stop();
  EXPECT_EQ(0u, tp.Size());

  infer_server::EqualityThreadPool p(nullptr, 10);
  std::vector<int> res;
  std::mutex m_lock;
  std::vector<std::future<void>> ret;
  for (int i = 3; i <= 12; ++i) {
    ret.emplace_back(p.Push(
        0,
        [&m_lock, &res](int n) {
          std::lock_guard<std::mutex> lk(m_lock);
          res.push_back(n);
        },
        i));
  }

  for (auto& it : ret) {
    it.get();
  }

  for (int i = 3; i < 13; i++) {
    auto it = std::find(res.begin(), res.end(), i);
    EXPECT_NE(it, res.end());
  }
}
