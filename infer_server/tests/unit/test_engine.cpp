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
#include <utility>
#include <vector>

#include "core/engine.h"
#include "core/request_ctrl.h"
#include "infer_server.h"
#include "processor.h"
#include "test_base.h"

namespace infer_server {
namespace {

constexpr const char* model_url =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
auto empty_response_func = [](Status, PackagePtr) {};
auto empty_notifier_func = [](const RequestControl*) {};

std::vector<std::shared_ptr<Processor>> PrepareProcessors(int device_id) {
  auto model = InferServer::LoadModel(model_url, "subnet0");
  if (!model) {
    std::cerr << "load model failed\n";
    return {};
  }
  std::vector<std::shared_ptr<Processor>> processors;
  // preproc
  DataLayout host_input_layout_{DataType::UINT8, DimOrder::NHWC};
  auto preproc = std::make_shared<PreprocessorHost>();
  auto empty_preproc_func = [](ModelIO*, const InferData&, const ModelInfo&) { return true; };
  preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", empty_preproc_func);
  preproc->SetParams("model_info", model, "device_id", device_id, "host_input_layout", host_input_layout_);
  EXPECT_EQ(preproc->Init(), Status::SUCCESS);
  if (preproc->Init() != Status::SUCCESS) {
    std::cerr << "Init preproc failed\n";
    return {};
  }
  processors.push_back(preproc);

  // predictor
  auto predictor = std::make_shared<Predictor>();
  predictor->SetParams("model_info", model, "device_id", device_id);
  if (predictor->Init() != Status::SUCCESS) {
    std::cerr << "Init predictor failed\n";
    return {};
  }
  processors.push_back(predictor);

  // postproc
  DataLayout host_output_layout{infer_server::DataType::FLOAT32, infer_server::DimOrder::NHWC};
  std::shared_ptr<Processor> postproc = std::make_shared<Postprocessor>();
  postproc->SetParams("model_info", model, "device_id", device_id, "host_output_layout", host_output_layout);
  if (postproc->Init() != Status::SUCCESS) {
    std::cerr << "Init postproc failed\n";
    return {};
  }
  processors.push_back(postproc);
  return processors;
}

TEST(InferServerCore, EngineIdle) {
  int device_id = 0;

  auto processors = PrepareProcessors(device_id);
  ASSERT_EQ(processors.size(), 3u);
  auto preproc = processors[0];
  auto predictor = processors[1];
  auto postproc = processors[2];

  // test idle
  {
    PriorityThreadPool tp([device_id]() -> bool {
      try {
        edk::MluContext ctx;
        ctx.SetDeviceId(device_id);
        ctx.BindDevice();
        return true;
      } catch (edk::Exception& e) {
        LOG(ERROR) << "Init thread context failed, error: " << e.what();
        return false;
      }
    });

    std::unique_ptr<Engine> engine(new Engine({preproc, predictor, postproc}, [](Engine* idle) {}, &tp));
    ASSERT_TRUE(engine);
    EXPECT_NE(engine->Fork().get(), engine.get());

    std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 0, 3));
    ASSERT_TRUE(ctrl);
    // engine tasknode number = 3
    size_t idx = 0;
    for (; idx < 2; ++idx) {
      auto input = Package::Create(1);
      input->data[0]->ctrl = ctrl.get();
      input->data[0]->index = idx;
      ASSERT_NO_THROW(engine->Run(std::move(input)));
    }

    // task load = 2, remained capacity = 1
    ASSERT_TRUE(engine->IsIdle());

    auto input = Package::Create(1);
    input->data[0]->ctrl = ctrl.get();
    input->data[0]->index = idx;
    ASSERT_NO_THROW(engine->Run(std::move(input)));

    // have used 3 tasknode, free tasknode == 0
    ASSERT_FALSE(engine->IsIdle());

    // get all task done
    tp.Resize(3);

    while (!ctrl->IsProcessFinished()) {}
  }
}

TEST(InferServerCore, EngineProcess) {
  int device_id = 0;

  auto processors = PrepareProcessors(device_id);
  ASSERT_EQ(processors.size(), 3u);
  auto preproc = processors[0];
  auto predictor = processors[1];
  auto postproc = processors[2];
  {
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

    std::promise<void> done_flag;
    std::unique_ptr<Engine> engine(
        new Engine({preproc, predictor, postproc}, [&done_flag](Engine* idle) { done_flag.set_value(); }, &tp));
    ASSERT_TRUE(engine);

    std::unique_ptr<RequestControl> ctrl(
      new RequestControl(empty_response_func, empty_notifier_func, "", 1, 1));
    ASSERT_TRUE(ctrl);
    auto input = Package::Create(1);
    input->data[0]->ctrl = ctrl.get();
    input->data[0]->index = 0;
    ASSERT_NO_THROW(engine->Run(std::move(input)));

    auto done_ret = done_flag.get_future().wait_for(std::chrono::seconds(1));
    ASSERT_EQ(std::future_status::ready, done_ret);
  }
}

}  // namespace
}  // namespace infer_server
