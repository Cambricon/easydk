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

#include "core/request_ctrl.h"
#include "core/session.h"

namespace infer_server {
namespace {

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int64_t> request_id_dis(0, std::numeric_limits<int64_t>::max());
std::uniform_int_distribution<uint32_t> data_num_dis(1, 100);

TEST(InferServerCore, RequestCtrl) {
  int repeat_times = 10;
  while (repeat_times--) {
    int64_t request_id = request_id_dis(gen);
    uint32_t data_num = data_num_dis(gen);
    std::string tag = "test ctrl" + std::to_string(request_id);
    PackagePtr out;
    Status status;
    bool response_done_flag = false;
    bool notify_flag = false;
    auto response = [&status, &out, &response_done_flag](Status s, PackagePtr pack) {
      out = std::move(pack);
      status = s;
      response_done_flag = true;
    };
    auto notifier = [&notify_flag](const RequestControl* caller) { notify_flag = true; };
    std::unique_ptr<RequestControl> ctrl(new RequestControl(response, notifier, tag, request_id, data_num));
    std::future<void> flag = ctrl->ResponseDonePromise();
    EXPECT_EQ(tag, ctrl->Tag());
    EXPECT_EQ(request_id, ctrl->RequestId());
    ASSERT_EQ(data_num, ctrl->DataNum());
    ASSERT_FALSE(ctrl->IsDiscarded());
    ASSERT_FALSE(ctrl->IsProcessFinished());
    ASSERT_TRUE(ctrl->IsSuccess());

    for (uint32_t idx = 0; idx < data_num; ++idx) {
      ASSERT_FALSE(ctrl->IsProcessFinished());
      ASSERT_FALSE(notify_flag);
      ASSERT_TRUE(ctrl->IsSuccess());
      ctrl->ProcessDone(Status::SUCCESS, nullptr, idx, {});
    }
    ASSERT_TRUE(notify_flag);
    ASSERT_TRUE(ctrl->IsProcessFinished());
    ASSERT_TRUE(ctrl->IsSuccess());

    ASSERT_EQ(flag.wait_for(std::chrono::milliseconds(1)), std::future_status::timeout);
    ASSERT_FALSE(response_done_flag);
    ctrl->Response();
    ASSERT_TRUE(response_done_flag);

    ctrl.reset();
    ASSERT_NE(flag.wait_for(std::chrono::milliseconds(1)), std::future_status::timeout);
    flag.get();
    ASSERT_TRUE(out);
    ASSERT_EQ(status, Status::SUCCESS);
    ASSERT_EQ(out->data.size(), data_num);
  }
}

auto empty_response_func = [](Status s, PackagePtr pack) {};
auto empty_notifier_func = [](const RequestControl*) {};

inline Status GenError() {
  std::uniform_int_distribution<int> err_dis(1, static_cast<int>(Status::STATUS_COUNT));
  return static_cast<Status>(err_dis(gen));
}

TEST(InferServerCore, RequestCtrlFailed) {
  int repeat_times = 10;
  while (repeat_times--) {
    int64_t request_id = request_id_dis(gen);
    uint32_t data_num = data_num_dis(gen);
    std::uniform_int_distribution<uint32_t> error_idx_dis(0, data_num - 1);
    std::set<uint32_t> err_idxs;
    uint32_t err_num = error_idx_dis(gen) / 2 + 1;
    while (err_idxs.size() < err_num) {
      err_idxs.insert(error_idx_dis(gen));
    }
    std::string tag = "test ctrl" + std::to_string(request_id);
    PackagePtr out;
    Status status;
    auto response = [&status, &out](Status s, PackagePtr pack) {
      out = std::move(pack);
      status = s;
    };
    bool notify_flag = false;
    auto notifier = [&notify_flag](const RequestControl* caller) { notify_flag = true; };
    std::unique_ptr<RequestControl> ctrl(new RequestControl(response, notifier, tag, request_id, data_num));
    std::future<void> flag = ctrl->ResponseDonePromise();

    ASSERT_FALSE(ctrl->IsDiscarded());
    ASSERT_FALSE(ctrl->IsProcessFinished());
    ASSERT_TRUE(ctrl->IsSuccess());

    Status first_err = Status::SUCCESS;
    for (uint32_t idx = 0; idx < data_num; ++idx) {
      if (err_idxs.count(idx)) {
        Status err = GenError();
        ASSERT_NE(err, Status::SUCCESS);
        if (first_err == Status::SUCCESS) first_err = err;
        ctrl->ProcessFailed(err);
        EXPECT_FALSE(ctrl->IsSuccess());
      } else {
        ctrl->ProcessDone(Status::SUCCESS, nullptr, idx, {});
      }
    }
    ASSERT_TRUE(notify_flag);
    ASSERT_TRUE(ctrl->IsProcessFinished());
    ASSERT_FALSE(ctrl->IsSuccess());

    ctrl->Response();
    ctrl.reset();
    flag.get();

    ASSERT_EQ(first_err, status);
    ASSERT_TRUE(out);
    ASSERT_EQ(out->data.size(), data_num);
  }
}

TEST(InferServerCore, RequestCtrlDiscard) {
  std::unique_ptr<RequestControl> ctrl(new RequestControl(empty_response_func, empty_notifier_func, "", 0, 4u));
  ASSERT_FALSE(ctrl->IsDiscarded());
  ctrl->Discard();
  ASSERT_TRUE(ctrl->IsDiscarded());
}

#ifdef CNIS_RECORD_PERF

TEST(InferServerCore, RequestCtrlPerf) {
  PackagePtr out;
  Status status;
  auto response = [&status, &out](Status s, PackagePtr pack) {
    out = std::move(pack);
    status = s;
  };
  uint32_t data_num = 4;
  std::unique_ptr<RequestControl> ctrl(new RequestControl(response, empty_notifier_func, "", 0, data_num));
  ctrl->BeginRecord();

  std::uniform_real_distribution<float> perf_dis(0, 10000);
  float a_sum = 0, b_sum = 0;
  for (uint32_t idx = 0; idx < data_num; ++idx) {
    float a = perf_dis(gen);
    float b = perf_dis(gen);
    a_sum += a;
    b_sum += b;
    ctrl->ProcessDone(Status::SUCCESS, nullptr, idx, {{"a", a}, {"b", b}});
  }

  auto perf = ctrl->Performance();
  ASSERT_EQ(perf.size(), 2u);
  ASSERT_NO_THROW(perf.at("a"));
  ASSERT_NO_THROW(perf.at("b"));
  EXPECT_FLOAT_EQ(a_sum, perf["a"]);
  EXPECT_FLOAT_EQ(b_sum, perf["b"]);

  ctrl->Response();

  ASSERT_EQ(status, Status::SUCCESS);
  ASSERT_TRUE(out);
  EXPECT_EQ(out->perf, perf);
  EXPECT_EQ(out->data.size(), data_num);
}

#endif

}  // namespace
}  // namespace infer_server
