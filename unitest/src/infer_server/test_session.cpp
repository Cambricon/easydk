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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../test_base.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cnis_test_base.h"
#include "core/session.h"

namespace infer_server {

auto g_empty_preproc_func = [](ModelIO*, const InferData&, const ModelInfo*) { return true; };

class PreprocHandleTest : public IPreproc {
 public:
  PreprocHandleTest() = default;
  ~PreprocHandleTest() = default;

 private:
  int OnTensorParams(const CnPreprocTensorParams* params) override { return 0; }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect>& src_rects) override {
    return 0;
  }
};

constexpr int device_id = 0;

class SessTestObserver : public Observer {
 public:
  SessTestObserver(std::condition_variable* response_cond, std::mutex* response_mutex, std::atomic<bool>* done)
      : response_cond_(response_cond), response_mutex_(response_mutex), done_(done) {}

  void Response(Status status, PackagePtr data, any user_data) noexcept override {
    std::unique_lock<std::mutex> lk(*response_mutex_);
    done_->store(true);
    lk.unlock();
    response_cond_->notify_one();
  }

 private:
  std::condition_variable* response_cond_;
  std::mutex* response_mutex_;
  std::atomic<bool>* done_;
};

static SessionDesc ReturnSessionDesc(const std::string& name, PreprocHandleTest* handler, size_t batch_timeout,
                                     BatchStrategy strategy, uint32_t engine_num) {
  SessionDesc desc;
  desc.name = name;
  desc.model = InferServer::LoadModel(GetModelInfoStr("resnet50", "url"));
  desc.strategy = strategy;
  desc.postproc = Postprocessor::Create();
  desc.batch_timeout = 10;
  desc.engine_num = engine_num;
  desc.show_perf = true;
  desc.priority = 0;
  desc.model_input_format = infer_server::NetworkInputFormat::BGR;
  if (handler) {
    desc.preproc = Preprocessor::Create();
    SetPreprocHandler(desc.model->GetKey(), handler);
  }
  return desc;
}

static void CreateBufSurfaceParams(int device_id, CnedkBufSurfaceCreateParams* create_params) {
  memset(create_params, 0, sizeof(*create_params));
  create_params->device_id = device_id;
  create_params->batch_size = 1;
  create_params->size = 100;
  create_params->color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
  InitPlatform();
  create_params->mem_type = CNEDK_BUF_MEM_DEFAULT;
}

static void PrepareInput(CnedkBufSurfaceCreateParams* params, PreprocInput* input) {
  CnedkBufSurface* surf;
  EXPECT_EQ(CnedkBufSurfaceCreate(&surf, params), 0);
  cnedk::BufSurfWrapperPtr buf_surface = std::make_shared<cnedk::BufSurfaceWrapper>(surf);
  input->surf = buf_surface;
}

TEST(InferServerCore, SessionInit) {
  // Session init
  PriorityThreadPool tp(nullptr);
  auto handler = std::make_shared<PreprocHandleTest>();
  SessionDesc desc = ReturnSessionDesc("test session", handler.get(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));

  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  // Session other function
  std::string get_session_name = session->GetName();
  ASSERT_EQ(session->GetName(), "init session");

  ASSERT_EQ(session->GetExecutor(), executor.get());
  ASSERT_EQ(session->IsSyncLink(), false);

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<Observer> test_observer = std::make_shared<SessTestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(test_observer);
  ASSERT_EQ(session->GetRawObserver(), test_observer.get());

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionSend) {
  PriorityThreadPool tp([]() -> bool { return SetCurrentDevice(device_id); }, 3);
  auto handler = std::make_shared<PreprocHandleTest>();
  SessionDesc desc = ReturnSessionDesc("test session", handler.get(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<Observer> test_observer = std::make_shared<SessTestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(test_observer);

  // Session send sucess
  std::string tag = "test tag";
  auto input = Package::Create(1, tag);
  CnedkBufSurfaceCreateParams create_params;
  CreateBufSurfaceParams(device_id, &create_params);
  PreprocInput preproc_input;
  PrepareInput(&create_params, &preproc_input);
  input->data[0]->Set(std::move(preproc_input));

  any user_data;
  EXPECT_TRUE(
      session->Send(std::move(input), std::bind(&Observer::Response, session->GetRawObserver(), std::placeholders::_1,
                                                std::placeholders::_2, std::move(user_data))));

  std::unique_lock<std::mutex> lk(response_mutex);
  response_cond.wait(lk, [&done]() { return done.load(); });
  ASSERT_NO_THROW(session->WaitTaskDone(tag));

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionCheckAndResponse) {
  PriorityThreadPool tp([]() -> bool { return SetCurrentDevice(device_id); }, 3);
  auto handler = std::make_shared<PreprocHandleTest>();
  SessionDesc desc = ReturnSessionDesc("test session", handler.get(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<Observer> test_observer = std::make_shared<SessTestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(std::move(test_observer));

  auto input = Package::Create(1, "test tag");
  CnedkBufSurfaceCreateParams create_params;
  CreateBufSurfaceParams(device_id, &create_params);
  PreprocInput preproc_input;
  PrepareInput(&create_params, &preproc_input);
  input->data[0]->Set<PreprocInput>(std::move(preproc_input));

  auto ctrl = session->Send(std::move(input), std::bind(&Observer::Response, session->GetRawObserver(),
                                                        std::placeholders::_1, std::placeholders::_2, nullptr));
  std::unique_lock<std::mutex> lk(response_mutex);
  response_cond.wait(lk, [&]() { return done.load(); });
  ASSERT_NO_THROW(session->CheckAndResponse(ctrl));

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionDiscardTask) {
  PriorityThreadPool tp([]() -> bool { return SetCurrentDevice(device_id); }, 3);
  auto handler = std::make_shared<PreprocHandleTest>();
  SessionDesc desc = ReturnSessionDesc("test session", handler.get(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<SessTestObserver> test_observer =
      std::make_shared<SessTestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(std::move(test_observer));

  CnedkBufSurfaceCreateParams create_params;
  CreateBufSurfaceParams(device_id, &create_params);

  // stream1
  std::string tag1 = "test tag1";
  auto input1 = Package::Create(20, tag1);
  for (auto it : input1->data) {
    PreprocInput preproc_input;
    PrepareInput(&create_params, &preproc_input);
    it->Set<PreprocInput>(std::move(preproc_input));
  }

  // stream2
  std::string tag2 = "test tag2";
  auto input2 = Package::Create(20, tag2);
  for (auto it : input2->data) {
    PreprocInput preproc_input;
    PrepareInput(&create_params, &preproc_input);
    it->Set<PreprocInput>(std::move(preproc_input));
  }

  session->Send(std::move(input1), std::bind(&Observer::Response, session->GetRawObserver(), std::placeholders::_1,
                                             std::placeholders::_2, nullptr));
  session->Send(std::move(input2), std::bind(&Observer::Response, session->GetRawObserver(), std::placeholders::_1,
                                             std::placeholders::_2, nullptr));
  ASSERT_NO_THROW(session->DiscardTask(tag1));
  std::unique_lock<std::mutex> lk(response_mutex);
  response_cond.wait(lk, [&]() { return done.load(); });

  executor->Unlink(session.get());
}

}  // namespace infer_server
