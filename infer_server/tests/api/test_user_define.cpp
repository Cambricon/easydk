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

#include "cnis/buffer.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "fixture.h"

namespace infer_server {

#ifdef CNIS_USE_MAGICMIND
static const char* model_url = "http://video.cambricon.com/models/MLU370/resnet50_nhwc_tfu_0.8.2_uint8_int8_fp16.model";
#else
constexpr const char* model_url =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
#endif

struct MyData {
  Buffer data;
};

class MyProcessor : public ProcessorForkable<MyProcessor> {
 public:
  MyProcessor() noexcept : ProcessorForkable<MyProcessor>("MyProcessor") {}
  ~MyProcessor() {}
  Status Process(PackagePtr pack) noexcept override {
    if (!SetCurrentDevice(dev_id_)) return Status::ERROR_BACKEND;

    // discard all input and pass empty data to next processor
    for (auto& it : pack->data) {
      it->data.reset();
    }
    auto preproc_output = pool_->Request();
    ModelIO model_input;
    model_input.buffers.emplace_back(std::move(preproc_output));
    model_input.shapes.emplace_back(model_->InputShape(0));
    pack->predict_io.reset(new InferData);
    pack->predict_io->Set(std::move(model_input));
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

      if (!SetCurrentDevice(dev_id_)) return Status::ERROR_BACKEND;

      auto shape = model_->InputShape(0);
      auto layout = model_->InputLayout(0);
      pool_.reset(new MluMemoryPool(shape.BatchDataCount() * GetTypeSize(layout.dtype), 3, dev_id_));
    } catch (bad_any_cast&) {
      LOG(ERROR) << "Unmatched data type";
      return Status::WRONG_TYPE;
    }

    return Status::SUCCESS;
  }

 private:
  std::unique_ptr<MluMemoryPool> pool_{nullptr};
  ModelPtr model_;
  int dev_id_;
};

TEST_F(InferServerTestAPI, UserDefine) {
  auto model = server_->LoadModel(model_url);
  ASSERT_TRUE(model) << "load model failed";
  auto preproc = MyProcessor::Create();
  SessionDesc desc;
  desc.name = "test user define";
  desc.model = model;
  desc.strategy = BatchStrategy::DYNAMIC;
  desc.preproc = std::move(preproc);
  desc.batch_timeout = 10;
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
