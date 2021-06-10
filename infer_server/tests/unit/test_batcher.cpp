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

#include <future>
#include <list>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "test_base.h"
#include "util/batcher.h"

namespace infer_server {

TEST(InferServerUtil, Batcher) {
  constexpr uint32_t set_size = 100;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dis(0, 10000);
  std::set<int> int_set;
  while (int_set.size() < set_size) {
    int_set.insert(dis(gen));
  }

  std::vector<int> batch_out;
  batch_out.reserve(set_size);

  std::cout << "test without timeout\n";
  for (int time = 0; time < 100; ++time) {
    constexpr uint32_t timeout = 0;
    std::uniform_int_distribution<uint32_t> bs_dis(1, 50);
    const uint32_t batch_size = bs_dis(gen);
    auto notifier = [&batch_out](std::vector<int>&& out) { batch_out.insert(batch_out.end(), out.begin(), out.end()); };
    Batcher<int> b(notifier, timeout, batch_size);
    uint32_t index = 0;
    for (auto& it : int_set) {
      EXPECT_EQ(b.Size(), index++);
      b.AddItem(it);
      index %= batch_size;
    }
    EXPECT_EQ(batch_out.size(), set_size - set_size % batch_size);
    b.Emit();
    EXPECT_EQ(batch_out.size(), set_size);

    index = 0;
    for (auto& it : int_set) {
      EXPECT_EQ(batch_out[index++], it);
    }
    batch_out.clear();
  }

  std::cout << "test with timeout\n";

  {
    constexpr uint32_t batch_size = 12;
    constexpr uint32_t send_number_before_timeout = 10;
    constexpr uint32_t timeout = 50;
    std::promise<void> notify_flag;
    auto notifier = [&batch_out, &notify_flag, send_number_before_timeout](std::vector<int>&& out) {
      EXPECT_EQ(out.size(), send_number_before_timeout);
      batch_out.insert(batch_out.end(), out.begin(), out.end());
      notify_flag.set_value();
    };

    Batcher<int> b(notifier, timeout, batch_size);
    uint32_t index = 0;
    for (auto& it : int_set) {
      b.AddItem(it);
      EXPECT_EQ(b.Size(), ++index);
      if (index == send_number_before_timeout) {
        index = 0;
        notify_flag.get_future().get();
        notify_flag = std::promise<void>();
      }
    }
    b.Emit();
    EXPECT_EQ(batch_out.size(), set_size);

    index = 0;
    for (auto& it : int_set) {
      EXPECT_EQ(batch_out[index++], it);
    }
    batch_out.clear();
  }
}

}  // namespace infer_server
