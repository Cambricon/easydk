/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
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

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "fixture.h"

namespace infer_server {
namespace {

std::map<std::string, int> g_param_set{{"number1", 1}, {"number2", 2}, {"number4", 4}};

class TestProcessor : public ProcessorForkable<TestProcessor> {
 public:
  TestProcessor() noexcept : ProcessorForkable("TestProcessor") {
    VLOG(1) << "[EasyDK Tests] [InferServer]TestProcessor Construct";
  }

  ~TestProcessor() { VLOG(1) << "[EasyDK Tests] [InferServer] TestProcessor Destruct"; }

  Status Process(PackagePtr data) noexcept override {
    VLOG(4) << "[EasyDK Tests] [InferServer] TestProcessor Process\n";
    if (!initialized_) return Status::ERROR_BACKEND;
    for (auto& it : g_param_set) {
      if (!HaveParam(it.first)) return Status::INVALID_PARAM;
      if (GetParam<int>(it.first) != it.second) return Status::INVALID_PARAM;
    }
    return Status::SUCCESS;
  }

  Status Init() noexcept override {
    VLOG(1) << "[EasyDK Tests] [InferServer] TestProcessor Init";
    initialized_ = true;
    return Status::SUCCESS;
  }

 private:
  bool initialized_{false};
};

TEST_F(InferServerTestAPI, Processor) {
  TestProcessor processor;
  for (auto& it : g_param_set) {
    processor.SetParams(it.first, it.second);
  }
  ASSERT_EQ(processor.Init(), Status::SUCCESS);
  ASSERT_EQ(processor.Process({}), Status::SUCCESS);
  auto fork = processor.Fork();
  ASSERT_TRUE(fork);
  ASSERT_NE(fork.get(), &processor);
  ASSERT_EQ(fork->Process({}), Status::SUCCESS);

  // two processor should have independent params
  processor.PopParam<int>(g_param_set.begin()->first);
  EXPECT_TRUE(fork->HaveParam(g_param_set.begin()->first));
}

class PostprocHandleTest : public IPostproc {
 public:
  ~PostprocHandleTest() { }
  int OnPostproc(const std::vector<InferData*> &data_vec, const ModelIO& model_output,
                  const ModelInfo* model_info) override { return 0; }
};

TEST_F(InferServerTestAPI, Postprocessor) {
  PostprocHandleTest postproc_handler;
  auto model = server_->LoadModel(GetModelInfoStr("resnet50", "url"));
  {
    auto processor = Postprocessor::Create();
    processor->SetParams("model_info", model,
                         "device_id", 0);
    SetPostprocHandler(model->GetKey(), &postproc_handler);
    EXPECT_EQ(processor->Init(), Status::SUCCESS);
  }
}

}  // namespace
}  // namespace infer_server
