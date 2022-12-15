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
#include <string>
#include <utility>

#include "../test_base.h"
#include "cnedk_buf_surface.h"
#include "cnedk_platform.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "fixture.h"

namespace infer_server {

struct MyData {
  float* data;
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
    auto preproc_output = pool_.GetBufSurfaceWrapper(2000);

    ModelIO model_input;
    model_input.surfs.emplace_back(preproc_output);
    model_input.shapes.emplace_back(model_->InputShape(0));
    pack->predict_io.reset(new InferData);
    pack->predict_io->Set(std::move(model_input));
    return Status::SUCCESS;
  }

  Status Init() noexcept override {
    constexpr const char* params[] = {"model_info", "device_id"};
    for (auto p : params) {
      if (!HaveParam(p)) {
        LOG(ERROR) << "[EasyDK Tests] [InferServer] " << p << " has not been set!";
        return Status::INVALID_PARAM;
      }
    }
    try {
      model_ = GetParam<ModelPtr>("model_info");
      dev_id_ = GetParam<int>("device_id");

      if (!SetCurrentDevice(dev_id_)) return Status::ERROR_BACKEND;

      auto shape = model_->InputShape(0);
      auto layout = model_->InputLayout(0);
      uint32_t model_input_w, model_input_h, model_input_c;
      model_input_w = shape[2];
      model_input_h = shape[1];
      model_input_c = shape[3];
      if (layout.order == DimOrder::NCHW) {
        model_input_w = shape[3];
        model_input_h = shape[2];
        model_input_c = shape[1];
      }

      CnedkBufSurfaceCreateParams create_params;
      memset(&create_params, 0, sizeof(create_params));
      if (cnedk::IsEdgePlatform(dev_id_)) {
        create_params.mem_type = CNEDK_BUF_MEM_VB;
      } else {
        create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
      }
      create_params.force_align_1 = 1;
      create_params.device_id = dev_id_;
      create_params.batch_size = shape.BatchSize();
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
      create_params.size = model_input_w * model_input_h * model_input_c;
      create_params.width = model_input_w;
      create_params.height = model_input_h;
      if (layout.dtype == DataType::UINT8)
        create_params.size = create_params.size;
      else if (layout.dtype == DataType::INT16)
        create_params.size *= 2;
      else if (layout.dtype == DataType::INT32)
        create_params.size *= 4;
      else if (layout.dtype == DataType::FLOAT16)
        create_params.size *= 2;
      else if (layout.dtype == DataType::FLOAT32)
        create_params.size *= 4;

      pool_.CreatePool(&create_params, 3);
    } catch (bad_any_cast&) {
      LOG(ERROR) << "[EasyDK Tests] [InferServer] Unmatched data type";
      return Status::WRONG_TYPE;
    }

    return Status::SUCCESS;
  }

 private:
  cnedk::BufPool pool_;
  ModelPtr model_;
  int dev_id_;
};

TEST_F(InferServerTestAPI, UserDefine) {
  auto model = server_->LoadModel(GetModelInfoStr("resnet50", "url"));
  ASSERT_TRUE(model) << "[EasyDK Tests] [InferServer] Load model failed";
  auto preproc = MyProcessor::Create();
  SessionDesc desc;
  desc.name = "test user define";
  desc.model = model;
  desc.model_input_format = NetworkInputFormat::RGB;
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
