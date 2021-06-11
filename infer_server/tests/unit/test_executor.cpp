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

class Executor;
using Executor_t = Executor*;
class InferServerPrivate;

static const char* model_url = "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";

static SessionDesc ReturnSessionDesc(const std::string& name, std::shared_ptr<Processor> preproc,
                              size_t batch_timeout, BatchStrategy strategy, uint32_t engine_num) {
  auto postproc_ = std::make_shared<Postprocessor>();
  SessionDesc desc;
  desc.name = name;
  desc.model = InferServer::LoadModel(model_url, "subnet0");
  if (!desc.model) {
    std::cout << "Load Model fail, check it!" << std::endl;
  }
  desc.strategy = strategy;
  desc.postproc = postproc_;
  desc.batch_timeout = 10;
  desc.engine_num = engine_num;
  desc.show_perf = true;
  desc.priority = 0;
  desc.host_output_layout = {infer_server::DataType::FLOAT32, infer_server::DimOrder::NHWC};
  if (preproc) {
    desc.preproc = preproc;
    desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", g_empty_preproc_func);
  }
  return desc;
}

TEST(InferServerCoreDeathTest, InitExecutorFail) {
  PriorityThreadPool tp(nullptr);
  SessionDesc desc =
      ReturnSessionDesc("test executor", std::make_shared<PreprocessorHost>(), 5, BatchStrategy::DYNAMIC, 1);

  // fail init, threadpool == nullptr
  ASSERT_DEATH(new Executor(desc, nullptr, 0), "");
  // fail init, device_id < 0
  ASSERT_DEATH(new Executor(desc, &tp, -1), "");
  // fail init, engine_num == 0
  ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", std::make_shared<PreprocessorHost>(), 5,
                                              BatchStrategy::DYNAMIC, 0),
                            &tp, 0),
               "");
  // fail init, preproc == nullptr
  ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", nullptr, 5, BatchStrategy::DYNAMIC, 1), &tp, 0), "");
  // fail init, batchstrategy unsupported
  ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", std::make_shared<PreprocessorHost>(), 5,
                                              BatchStrategy::SEQUENCE, 1),
                            &tp, 0),
               "");
}

TEST(InferServerCore, Executor) {
  // Executor init
  int device_id = 0;
  PriorityThreadPool tp(
      [device_id]() -> bool {
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
      ReturnSessionDesc("test executor", std::make_shared<PreprocessorHost>(), 200, BatchStrategy::STATIC, 1);

  // Executor other function
  // Link()
  std::unique_ptr<Executor> executor(new Executor(desc, &tp, device_id));
  std::unique_ptr<Session> session(new Session("init session", executor.get(), false, true));
  executor->Link(session.get());
  size_t get_session_number = executor->GetSessionNum();
  // one session
  ASSERT_EQ(get_session_number, 1u);

  // GetDesc()
  auto get_sessiondesc = executor->GetDesc();
  ASSERT_EQ(get_sessiondesc.batch_timeout, desc.batch_timeout);

  // GetName()
  auto get_sessiondesc_name = executor->GetName();
  ASSERT_EQ(get_sessiondesc_name, desc.name);

  // GetPriority()
  Priority priority_(desc.priority);
  auto get_priority = executor->GetPriority();
  ASSERT_EQ(get_priority, priority_);

  // GetEngineNum()
  auto get_engine_num = executor->GetEngineNum();
  ASSERT_EQ(get_engine_num, desc.engine_num);

  // GetThreadPool(), can't be nullptr
  auto get_thread_pool = executor->GetThreadPool();
  ASSERT_EQ(get_thread_pool, &tp);

  // Upload()
  std::promise<void> response_flag;
  auto empty_response_func = [](Status, PackagePtr) {};
  auto empty_notifier_func = [&response_flag](const RequestControl*) { response_flag.set_value(); };
  std::unique_ptr<RequestControl> ctrl(new RequestControl(empty_response_func, empty_notifier_func, "", 0, 1));
  int empty_data = 0;
  auto input = Package::Create(1);
  input->data[0]->Set(empty_data);
  input->data[0]->ctrl = ctrl.get();
  input->data[0]->index = 0;
  EXPECT_TRUE(executor->WaitIfCacheFull(-1));       // Executor::DispatchLoop() will cache_->Pop() the data
  ASSERT_TRUE(executor->Upload(std::move(input)));  // cache_num = engine_num * 3
  auto ret = response_flag.get_future().wait_for(std::chrono::seconds(1));
  EXPECT_EQ(std::future_status::ready, ret);
  ASSERT_NO_THROW(executor->Unlink(session.get()));
  get_session_number = executor->GetSessionNum();
  // no session
  ASSERT_EQ(get_session_number, 0u);
}

}  // namespace infer_server
