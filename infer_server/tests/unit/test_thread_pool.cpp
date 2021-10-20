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
#include <memory>
#include <thread>
#include <vector>

#include "cnis/infer_server.h"
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
  for (int i = 3; i < 13; ++i) {
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

TEST(InferServerUtil, ThreadPoolStable) {
  std::unique_ptr<infer_server::PriorityThreadPool> main_pool;
  std::unique_ptr<infer_server::EqualityThreadPool> pre_pool;
  std::unique_ptr<infer_server::EqualityThreadPool> post_pool;
  main_pool.reset(new infer_server::PriorityThreadPool(nullptr));

  {
    main_pool->Resize(8);
    pre_pool.reset(new infer_server::EqualityThreadPool(nullptr));
    pre_pool->Resize(4);
    pre_pool->Resize(8);
    post_pool.reset(new infer_server::EqualityThreadPool(nullptr));
    post_pool->Resize(4);
    post_pool->Resize(8);

    std::promise<void> end;
    std::function<void(infer_server::any, int)> task = [&main_pool, &pre_pool, &post_pool, &end](infer_server::any next,
                                                                                                 int i) {
      if (i == 2) {
        auto fut = post_pool->Push(0, []() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          return true;
        });
        fut.get();
      } else if (i == 0) {
        auto fut = pre_pool->Push(0, []() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          return true;
        });
        fut.get();
      }
      if (i < 4) {
        ++i;
        main_pool->VoidPush(i, infer_server::any_cast<std::function<void(infer_server::any, int)>&>(next), next, i);
      } else {
        end.set_value();
      }
    };

    main_pool->VoidPush(0, task, infer_server::any(task), 0);
    end.get_future().get();
    post_pool->Resize(4);
    post_pool->Stop(true);
    post_pool.reset();
    main_pool->Resize(0);
  }
}
