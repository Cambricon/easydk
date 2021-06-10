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

#include "core/session.h"
#include "infer_server.h"
#include "processor.h"
#include "test_base.h"

namespace infer_server {

auto g_empty_preproc_func = [](ModelIO*, const InferData&, const ModelInfo&) { return true; };

constexpr int device_id = 0;

static const char* model_url = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";

class TestObserver : public Observer {
 public:
  TestObserver(std::condition_variable* response_cond, std::mutex* response_mutex, std::atomic<bool>* done)
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

static SessionDesc ReturnSessionDesc(const std::string& name, std::shared_ptr<Processor> preproc, size_t batch_timeout,
                                     BatchStrategy strategy, uint32_t engine_num) {
  auto postproc_ = std::make_shared<Postprocessor>();
  SessionDesc desc;
  desc.name = name;
  desc.model = InferServer::LoadModel(model_url, "subnet0");
  desc.strategy = strategy;
  desc.postproc = postproc_;
  desc.batch_timeout = 10;
  desc.engine_num = engine_num;
  desc.show_perf = true;
  desc.priority = 0;
  desc.host_output_layout = {infer_server::DataType::FLOAT32, infer_server::DimOrder::NCHW};
  if (preproc) {
    desc.preproc = preproc;
    desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", g_empty_preproc_func);
  }
  return desc;
}

TEST(InferServerCore, SessionInit) {
  // Session init
  PriorityThreadPool tp(nullptr);
  auto preproc = std::make_shared<PreprocessorHost>();
  SessionDesc desc = ReturnSessionDesc("test session", preproc, 5, BatchStrategy::DYNAMIC, 1);
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
  std::shared_ptr<Observer> test_observer = std::make_shared<TestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(test_observer);
  ASSERT_EQ(session->GetRawObserver(), test_observer.get());

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionSend) {
  PriorityThreadPool tp(
      []() -> bool {
        try {
          edk::MluContext ctx;
          ctx.SetDeviceId(device_id);
          ctx.BindDevice();
          return true;
        } catch (edk::Exception& e) {
          LOG(ERROR) << "Init thread context failed, error: " << e.what();
          return false;
        }
      },
      3);
  SessionDesc desc =
      ReturnSessionDesc("test session", std::make_shared<PreprocessorHost>(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<Observer> test_observer = std::make_shared<TestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(std::move(test_observer));

  // Session send sucess
  std::string tag = "test tag";
  auto input = Package::Create(1, tag);
  any user_data;

  ASSERT_TRUE(
      session->Send(std::move(input), std::bind(&Observer::Response, session->GetRawObserver(), std::placeholders::_1,
                                                std::placeholders::_2, std::move(user_data))));

  std::unique_lock<std::mutex> lk(response_mutex);
  response_cond.wait(lk, [&done]() { return done.load(); });
  ASSERT_NO_THROW(session->WaitTaskDone(tag));

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionCheckAndResponse) {
  PriorityThreadPool tp(
      []() -> bool {
        try {
          edk::MluContext ctx;
          ctx.SetDeviceId(device_id);
          ctx.BindDevice();
          return true;
        } catch (edk::Exception& e) {
          LOG(ERROR) << "Init thread context failed, error: " << e.what();
          return false;
        }
      },
      3);
  SessionDesc desc =
      ReturnSessionDesc("test session", std::make_shared<PreprocessorHost>(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<Observer> test_observer = std::make_shared<TestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(std::move(test_observer));

  auto input = Package::Create(1, "test tag");

  auto ctrl = session->Send(std::move(input), std::bind(&Observer::Response, session->GetRawObserver(),
                                                        std::placeholders::_1, std::placeholders::_2, nullptr));
  std::unique_lock<std::mutex> lk(response_mutex);
  response_cond.wait(lk, [&]() { return done.load(); });
  ASSERT_NO_THROW(session->CheckAndResponse(ctrl));

  executor->Unlink(session.get());
}

TEST(InferServerCore, SessionDiscardTask) {
  PriorityThreadPool tp(
      []() -> bool {
        try {
          edk::MluContext ctx;
          ctx.SetDeviceId(device_id);
          ctx.BindDevice();
          return true;
        } catch (edk::Exception& e) {
          LOG(ERROR) << "Init thread context failed, error: " << e.what();
          return false;
        }
      },
      3);
  SessionDesc desc =
      ReturnSessionDesc("test session", std::make_shared<PreprocessorHost>(), 5, BatchStrategy::DYNAMIC, 1);
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, 0));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());

  std::condition_variable response_cond;
  std::mutex response_mutex;
  std::atomic<bool> done(false);
  std::shared_ptr<TestObserver> test_observer = std::make_shared<TestObserver>(&response_cond, &response_mutex, &done);
  session->SetObserver(std::move(test_observer));
  // stream1
  std::string tag1 = "test tag1";
  auto input1 = Package::Create(20, tag1);

  // stream2
  std::string tag2 = "test tag2";
  auto input2 = Package::Create(20, tag2);

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
