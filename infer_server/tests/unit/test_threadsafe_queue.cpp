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

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "util/threadsafe_queue.h"

TEST(InferServerUtil, ThreadSafeQueue) {
  infer_server::TSPriorityQueue<int> ts_q;
  std::vector<int> vec = {3, 1, 5, 2, 4, 8, 6, 9};
  std::vector<int> res;
  int tmp;
  for (unsigned int i = 0; i < vec.size(); i++) {
    ts_q.Push(vec[i]);
  }
  EXPECT_FALSE(ts_q.Empty());
  while (!ts_q.Empty()) {
    auto b = ts_q.TryPop(tmp);
    EXPECT_TRUE(b);
    res.push_back(tmp);
  }

  EXPECT_TRUE(ts_q.Empty());
  auto b = ts_q.TryPop(tmp);
  EXPECT_FALSE(b);
  std::sort(vec.begin(), vec.end(), std::greater<int>());
  EXPECT_EQ(vec.size(), res.size());
  for (unsigned int i = 0; i < vec.size(); i++) {
    EXPECT_EQ(vec[i], res[i]);
  }
}
