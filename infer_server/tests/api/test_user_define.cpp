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

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "buffer.h"
#include "device/mlu_context.h"
#include "fixture.h"
#include "infer_server.h"
#include "processor.h"

namespace infer_server {

constexpr const char* model_url =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";

struct MyData {
  Buffer data;
};

class MyProcessor : public ProcessorForkable<MyProcessor> {
 public:
  MyProcessor() noexcept : ProcessorForkable<MyProcessor>("MyProcessor") {}
  ~MyProcessor() {}
  Status Process(PackagePtr pack) noexcept override {
    try {
      edk::MluContext ctx;
      ctx.SetDeviceId(dev_id_);
      ctx.BindDevice();
    } catch (edk::Exception& e) {
      LOG(ERROR) << e.what();
      return Status::ERROR_BACKEND;
    }

    // discard all input and pass empty data to next processor
    pack->data.clear();
    auto preproc_output = pool_->Request();
    ModelIO model_input;
    model_input.buffers.emplace_back(std::move(preproc_output));
    model_input.shapes.emplace_back(model_->InputShape(0));
    pack->data.emplace_back(new InferData);
    pack->data[0]->Set(std::move(model_input));
    return Status::SUCCESS;
  }

  Status Init() noexcept override {
    constexpr const char* params[] = {"model_info", "device_id"};
    for (auto p : params) {
      if (!HaveParam(p)) {
        LOG(ERROR) << p << " has not been set!";
        return Status::INVALID_PARAM;
      }
    }
    try {
      model_ = GetParam<ModelPtr>("model_info");
      dev_id_ = GetParam<int>("device_id");

      edk::MluContext ctx;
      ctx.SetDeviceId(dev_id_);
      ctx.BindDevice();

      auto shape = model_->InputShape(0);
      auto layout = model_->InputLayout(0);
      pool_.reset(new MluMemoryPool(shape.BatchDataCount() * GetTypeSize(layout.dtype), 3, dev_id_));
    } catch (bad_any_cast&) {
      LOG(ERROR) << "Unmatched data type";
      return Status::WRONG_TYPE;
    } catch (edk::Exception& e) {
      LOG(ERROR) << e.what();
      return Status::ERROR_BACKEND;
    }

    return Status::SUCCESS;
  }

 private:
  std::unique_ptr<MluMemoryPool> pool_{nullptr};
  ModelPtr model_;
  int dev_id_;
};

TEST_F(InferServerTestAPI, UserDefine) {
  auto model = server_->LoadModel(model_url, "subnet0");
  if (!model) {
    std::cerr << "load model failed";
    std::terminate();
  }
  auto preproc = MyProcessor::Create();
  SessionDesc desc;
  desc.name = "test user define";
  desc.model = model;
  desc.strategy = BatchStrategy::DYNAMIC;
  desc.preproc = std::move(preproc);
  desc.batch_timeout = 100;
  desc.engine_num = 1;
  desc.show_perf = true;
  desc.priority = 0;

  Session_t session = server_->CreateSyncSession(desc);
  ASSERT_TRUE(session);

  auto input = std::make_shared<Package>();
  input->data.reserve(32);
  for (uint32_t idx = 0; idx < 32; ++idx) {
    input->data.emplace_back(new InferData);
    input->data[idx]->Set(MyData());
  }
  auto output = std::make_shared<Package>();
  Status status;
  ASSERT_TRUE(server_->RequestSync(session, input, &status, output));
  ASSERT_EQ(status, Status::SUCCESS);
  EXPECT_EQ(output->data.size(), 32u);
  EXPECT_NO_THROW(output->data[0]->Get<ModelIO>());
  server_->DestroySession(session);
}

}  // namespace infer_server
