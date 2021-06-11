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
#include <memory>
#include <random>
#include <utility>

#include "core/cache.h"
#include "core/session.h"

namespace infer_server {
namespace {

TEST(InferServerCoreDeathTest, PushNullToCache) {
  CacheDynamic cache_d(16, Priority(0), 0);
  cache_d.Start();
  EXPECT_DEATH(cache_d.Push(nullptr), "");
  auto pack = std::make_shared<Package>();
  pack->data.emplace_back(new InferData);
  EXPECT_DEATH(cache_d.Push(std::move(pack)), "");
  cache_d.Stop();

  CacheStatic cache_s(16, Priority(0));
  cache_s.Start();
  EXPECT_DEATH(cache_s.Push(nullptr), "");
  pack = std::make_shared<Package>();
  pack->data.emplace_back(new InferData);
  EXPECT_DEATH(cache_s.Push(std::move(pack)), "");
  cache_s.Stop();
}

auto empty_response_func = [](Status, PackagePtr) {};
auto empty_notifier_func = [](const RequestControl*) {};
constexpr uint32_t d_batch_size = 4;
constexpr uint32_t s_batch_size = 8;
constexpr Priority priority(0);
constexpr uint32_t batch_timeout = 0;

TEST(InferServerCore, DynamicCache_StartStop) {
  constexpr uint32_t capacity = 10;

  std::shared_ptr<CacheBase> cache = std::make_shared<CacheDynamic>(d_batch_size, priority, batch_timeout);
  ASSERT_EQ(cache->BatchSize(), d_batch_size);
  ASSERT_EQ(cache->GetPriority(), priority);

  ASSERT_FALSE(cache->Running());
  EXPECT_FALSE(cache->Push(std::make_shared<Package>()));
  cache->Start();
  ASSERT_TRUE(cache->Running());
  ASSERT_TRUE(cache->Push(std::make_shared<Package>()));

  std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 0, d_batch_size * capacity));
  for (size_t idx = 0; idx < capacity; ++idx) {
    auto pack = std::make_shared<Package>();
    for (size_t b_idx = 0; b_idx < d_batch_size; ++b_idx) {
      pack->data.emplace_back(new InferData);
      pack->data[b_idx]->ctrl = ctrl.get();
    }
    ASSERT_TRUE(cache->Push(std::move(pack)));
  }

  auto out = cache->Pop();
  ASSERT_TRUE(out);
  ASSERT_EQ(out->data.size(), d_batch_size);
  for (auto& it : out->data) {
    EXPECT_TRUE(it->ctrl);
  }

  // clear cache
  cache->Stop();
  uint32_t out_cnt = 0;
  while ((out = cache->Pop())) {
    ASSERT_EQ(out->data.size(), d_batch_size);
    for (auto& it : out->data) {
      EXPECT_TRUE(it->ctrl);
    }
    ++out_cnt;
  }
  EXPECT_EQ(out_cnt, capacity - 1);
}

TEST(InferServerCore, DynamicCache_OverBatchSize) {
  constexpr uint32_t capacity = 12;
  std::shared_ptr<CacheBase> cache = std::make_shared<CacheDynamic>(d_batch_size, priority, batch_timeout);
  cache->Start();
  ASSERT_TRUE(cache->Running());

  std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 0, d_batch_size * capacity));
  for (size_t idx = 0; idx < d_batch_size; ++idx) {
    auto pack = std::make_shared<Package>();
    for (size_t b_idx = 0; b_idx < capacity; ++b_idx) {
      pack->data.emplace_back(new InferData);
      pack->data[b_idx]->ctrl = ctrl.get();
    }
    ASSERT_TRUE(cache->Push(std::move(pack)));
  }

  cache->Stop();
  uint32_t out_cnt = 0;
  while (auto out = cache->Pop()) {
    ASSERT_EQ(out->data.size(), d_batch_size);
    for (auto& it : out->data) {
      EXPECT_TRUE(it->ctrl);
    }
    ++out_cnt;
  }
  EXPECT_EQ(out_cnt, capacity);
}

TEST(InferServerCore, StaticCache_StartStop) {
  constexpr uint32_t capacity = 10;
  constexpr uint32_t data_num = 6;

  std::shared_ptr<CacheBase> cache = std::make_shared<CacheStatic>(s_batch_size, priority);
  ASSERT_EQ(cache->BatchSize(), s_batch_size);
  ASSERT_EQ(cache->GetPriority(), priority);

  ASSERT_FALSE(cache->Running());
  EXPECT_FALSE(cache->Push(std::make_shared<Package>()));
  cache->Start();
  ASSERT_TRUE(cache->Running());
  EXPECT_TRUE(cache->Push(std::make_shared<Package>()));

  std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 0, data_num * capacity));
  for (size_t idx = 0; idx < capacity; ++idx) {
    auto pack = std::make_shared<Package>();
    for (size_t b_idx = 0; b_idx < data_num; ++b_idx) {
      pack->data.emplace_back(new InferData);
      pack->data[b_idx]->ctrl = ctrl.get();
    }
    ASSERT_TRUE(cache->Push(std::move(pack)));
  }

  auto out = cache->Pop();
  ASSERT_EQ(out->data.size(), data_num);
  for (auto& it : out->data) {
    EXPECT_TRUE(it->ctrl);
  }
}

TEST(InferServerCore, StaticCache_RandomDataNum) {
  constexpr uint32_t capacity = 50;
  std::shared_ptr<CacheBase> cache = std::make_shared<CacheStatic>(s_batch_size, priority);
  cache->Start();
  ASSERT_TRUE(cache->Running());

  std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 0, d_batch_size * capacity));
  std::vector<uint32_t> data_num_record;
  for (size_t idx = 0; idx < capacity - 5;) {
    auto pack = std::make_shared<Package>();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> data_num_dis(1, 4 * s_batch_size);
    int data_num = data_num_dis(gen);
    for (int b_idx = 0; b_idx < data_num; ++b_idx) {
      pack->data.emplace_back(new InferData);
      pack->data[b_idx]->ctrl = ctrl.get();
    }
    ASSERT_TRUE(cache->Push(std::move(pack)));
    while (data_num > 0) {
      data_num_record.push_back(static_cast<uint32_t>(data_num) < s_batch_size ? data_num : s_batch_size);
      data_num -= s_batch_size;
      ++idx;
    }
  }

  cache->Stop();
  int index = 0;
  while (auto out = cache->Pop()) {
    ASSERT_EQ(out->data.size(), data_num_record[index]);
    ++index;
    for (auto& it : out->data) {
      EXPECT_TRUE(it->ctrl);
    }
  }
}

TEST(InferServerCore, DynamicCache_ConcurrentAndDiscard) {
  constexpr uint32_t capacity = 50;
  constexpr uint32_t parallel = 3;
  constexpr uint32_t total_data_num = capacity * d_batch_size;

  std::shared_ptr<CacheBase> cache = std::make_shared<CacheDynamic>(d_batch_size, priority, batch_timeout);
  cache->Start();
  ASSERT_TRUE(cache->Running());

  std::vector<std::future<void>> rets;
  std::vector<std::shared_ptr<RequestControl>> ctrls;
  rets.reserve(parallel);
  ctrls.reserve(parallel);
  for (size_t push_idx = 0; push_idx < parallel; ++push_idx) {
    auto ctrl = new RequestControl(empty_response_func, empty_notifier_func, std::to_string(push_idx), push_idx,
                                   d_batch_size * capacity);
    ctrls.emplace_back(ctrl);
    rets.emplace_back(std::async(std::launch::async, [cache, ctrl]() {
      std::uniform_int_distribution<uint32_t> data_num_dis(1, d_batch_size);
      std::random_device rd;
      std::mt19937 gen(rd());
      for (size_t idx = 0; idx < total_data_num / parallel - d_batch_size;) {
        auto pack = std::make_shared<Package>();
        uint32_t data_num = data_num_dis(gen);
        idx += data_num;
        for (size_t b_idx = 0; b_idx < data_num; ++b_idx) {
          pack->data.emplace_back(new InferData);
          pack->data[b_idx]->ctrl = ctrl;
        }
        ASSERT_TRUE(cache->Push(std::move(pack)));
      }
    }));
  }

  for (auto& it : rets) {
    it.get();
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> discard_dis(0, parallel - 1);
  uint32_t discard_idx = discard_dis(gen);
  ctrls[discard_idx]->Discard();

  cache->Stop();
  while (auto pack = cache->Pop()) {
    for (auto& it : pack->data) {
      EXPECT_FALSE(it->ctrl->IsDiscarded()) << "discarded data should not be popped out";
      EXPECT_NE(it->ctrl->Tag(), std::to_string(discard_idx));
    }
  }
}

TEST(InferServerCore, StaticCache_ConcurrentAndDiscard) {
  constexpr uint32_t capacity = 50;
  constexpr uint32_t batch_size = 16;
  constexpr uint32_t parallel = 3;

  std::shared_ptr<CacheBase> cache = std::make_shared<CacheStatic>(batch_size, priority);
  cache->Start();
  ASSERT_TRUE(cache->Running());

  std::vector<std::future<void>> rets;
  std::vector<std::unique_ptr<RequestControl>> ctrls;
  rets.reserve(parallel);
  ctrls.reserve(parallel);
  for (size_t push_idx = 0; push_idx < parallel; ++push_idx) {
    ctrls.emplace_back(new RequestControl(empty_response_func, empty_notifier_func, std::to_string(push_idx), 0,
                                          batch_size * capacity));
    rets.emplace_back(std::async(std::launch::async, [cache, push_idx, &ctrls]() {
      for (size_t idx = 0; idx < capacity / parallel; ++idx) {
        auto pack = std::make_shared<Package>();
        for (size_t b_idx = 0; b_idx < batch_size; ++b_idx) {
          pack->data.emplace_back(new InferData);
          pack->data[b_idx]->ctrl = ctrls[push_idx].get();
          pack->tag = ctrls[push_idx]->Tag();
        }
        ASSERT_TRUE(cache->Push(std::move(pack)));
      }
    }));
  }

  for (auto& it : rets) {
    it.get();
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> discard_dis(0, parallel - 1);
  uint32_t discard_idx = discard_dis(gen);
  ctrls[discard_idx]->Discard();

  cache->Stop();
  while (auto pack = cache->Pop()) {
    for (auto& it : pack->data) {
      EXPECT_FALSE(it->ctrl->IsDiscarded());
      EXPECT_NE(it->ctrl->Tag(), std::to_string(discard_idx));
    }
  }
}

}  // namespace
}  // namespace infer_server
