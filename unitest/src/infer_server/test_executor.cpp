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
#include "cnrt.h"
#include "core/session.h"

namespace infer_server {

class Executor;
using Executor_t = Executor *;
class InferServerPrivate;

class PreprocHandleTest : public infer_server::IPreproc {
 public:
  PreprocHandleTest() = default;
  ~PreprocHandleTest() = default;

 private:
  int OnTensorParams(const infer_server::CnPreprocTensorParams *params) override { return 0; }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    return 0;
  }
};

static SessionDesc ReturnSessionDesc(const std::string &name, infer_server::IPreproc *preproc_handle,
                                     size_t batch_timeout, BatchStrategy strategy, uint32_t engine_num) {
  SessionDesc desc;
  desc.name = name;
  desc.model = InferServer::LoadModel(GetModelInfoStr("resnet50", "url"));

  if (!desc.model) {
    LOG(ERROR) << "[EasyDK Tests] [InferServer] Load Model failed, Please check it.";
  }
  desc.strategy = strategy;
  desc.postproc = Postprocessor::Create();
  desc.batch_timeout = 10;
  desc.engine_num = engine_num;
  desc.show_perf = true;
  desc.priority = 0;
  if (preproc_handle) {
    desc.preproc = infer_server::Preprocessor::Create();
    infer_server::SetPreprocHandler(desc.model->GetKey(), preproc_handle);
    desc.model_input_format = infer_server::NetworkInputFormat::RGB;
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

TEST(InferServerCoreDeathTest, InitExecutorFail) {
  PriorityThreadPool tp(nullptr);
  pid_t pid = fork();
  if (pid == 0) {
    std::shared_ptr<PreprocHandleTest> handler = std::make_shared<PreprocHandleTest>();
    SessionDesc desc = ReturnSessionDesc("test executor", handler.get(), 5, BatchStrategy::DYNAMIC, 1);
    // fail init, threadpool == nullptr
    ASSERT_DEATH(new Executor(desc, nullptr, 0), "");
    // fail init, device_id < 0
    ASSERT_DEATH(new Executor(desc, &tp, -1), "");
    // fail init, engine_num == 0
    ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", handler.get(), 5, BatchStrategy::DYNAMIC, 0), &tp, 0),
                 "");
    // fail init, preproc == nullptr
    ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", nullptr, 5, BatchStrategy::DYNAMIC, 1), &tp, 0), "");
    _exit(0);
  } else {
    std::shared_ptr<PreprocHandleTest> handler = std::make_shared<PreprocHandleTest>();
    // fail init, batchstrategy unsupported
    ASSERT_DEATH(new Executor(ReturnSessionDesc("test executor", handler.get(), 5, BatchStrategy::SEQUENCE, 1), &tp, 0),
                 "");
    int status;
    wait(&status);
  }
  InferServer::ClearModelCache();
}

TEST(InferServerCore, Executor) {
  int device_id = 0;
  // Executor init
  PriorityThreadPool tp([device_id]() -> bool { return SetCurrentDevice(device_id); }, 3);
  std::shared_ptr<PreprocHandleTest> handler = std::make_shared<PreprocHandleTest>();
  SessionDesc desc = ReturnSessionDesc("test executor", handler.get(), 200, BatchStrategy::STATIC, 1);

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
  auto empty_notifier_func = [&response_flag](const RequestControl *) { response_flag.set_value(); };
  std::unique_ptr<RequestControl> ctrl(new RequestControl(empty_response_func, empty_notifier_func, "", 0, 1));
  auto input = Package::Create(1);
  CnedkBufSurfaceCreateParams create_params;
  CreateBufSurfaceParams(device_id, &create_params);
  PreprocInput preproc_input;
  PrepareInput(&create_params, &preproc_input);
  input->data[0]->Set(std::move(preproc_input));
  input->data[0]->ctrl = ctrl.get();
  input->data[0]->index = 0;
  EXPECT_TRUE(executor->WaitIfCacheFull(-1));                   // Executor::DispatchLoop() will cache_->Pop() the data
  ASSERT_TRUE(executor->Upload(std::move(input), ctrl.get()));  // cache_num = engine_num * 3
  auto ret = response_flag.get_future().wait_for(std::chrono::seconds(1));
  EXPECT_EQ(std::future_status::ready, ret);
  ASSERT_NO_THROW(executor->Unlink(session.get()));
  get_session_number = executor->GetSessionNum();
  // no session
  ASSERT_EQ(get_session_number, 0u);
}

}  // namespace infer_server
