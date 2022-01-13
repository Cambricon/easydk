/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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

#include <memory>
#include <utility>
#include <vector>

#include "cnis/processor.h"
#include "core/data_type.h"
#include "util/env.h"
#include "util/thread_pool.h"

namespace infer_server {

struct PreprocessorPrivate {
  ModelPtr model{nullptr};
  Preprocessor::ProcessFunction process_func{nullptr};

  std::vector<std::unique_ptr<MluMemoryPool>> pools;
  std::vector<Shape> shapes;
  std::vector<DataLayout> layouts;
};

Preprocessor::Preprocessor() noexcept
    : ProcessorForkable("Preprocessor"), priv_(new PreprocessorPrivate) {}

Preprocessor::~Preprocessor() {
  for (size_t i = 0; i < priv_->pools.size(); ++i) {
    if (priv_->pools[i]) {
      priv_->pools[i].reset(nullptr);
    }
  }
  priv_->pools.clear();

  delete priv_;
  priv_ = nullptr;
}

Status Preprocessor::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << p << " has not been set!";
      return Status::INVALID_PARAM;
    }
  }

  int device_id = 0;
  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    device_id = GetParam<int>("device_id");
    priv_->process_func = HaveParam("process_function") ? GetParam<ProcessFunction>("process_function") : nullptr;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "wrong param type";
    return Status::WRONG_TYPE;
  }

  // no process function will just pass through
  if (priv_->process_func) {
    // init dst memory and shapes
    size_t i_num = priv_->model->InputNum();
    priv_->shapes.reserve(i_num);
    priv_->layouts.reserve(i_num);
    for (size_t i_idx = 0; i_idx < i_num; ++i_idx) {
      priv_->shapes.emplace_back(priv_->model->InputShape(i_idx));
      priv_->layouts.emplace_back(priv_->model->InputLayout(i_idx));
      size_t data_count = priv_->shapes[i_idx].BatchDataCount();
      priv_->pools.emplace_back(new MluMemoryPool(data_count * GetTypeSize(priv_->layouts[i_idx].dtype), 3, device_id));
    }
  }

  return Status::SUCCESS;
}

Status Preprocessor::Process(PackagePtr pack) noexcept {
  size_t batch_size = pack->data.size();
  if (!priv_->process_func) {
    VLOG(5) << "No preprocess function, package pass through";
    return Status::SUCCESS;
  }
  size_t i_num = priv_->model->InputNum();
  ModelIO input;
  input.buffers.reserve(i_num);
  input.shapes.reserve(i_num);
  for (size_t i_idx = 0; i_idx < i_num; ++i_idx) {
    input.shapes.emplace_back(priv_->shapes[i_idx]);
    switch (priv_->layouts[i_idx].order) {
      case DimOrder::NCHW:
      case DimOrder::NHWC:
        input.shapes[i_idx][0] = batch_size;
        break;
      case DimOrder::HWCN:
        input.shapes[i_idx][3] = batch_size;
        break;
      default:
        LOG(ERROR) << "Unsupported input dim order: " << static_cast<int>(priv_->layouts[i_idx].order);
        return Status::ERROR_BACKEND;
    }
    input.buffers.emplace_back(priv_->pools[i_idx]->Request());
  }
  try {
    bool ret = priv_->process_func(&input, std::ref(pack->data), priv_->model.get());
    if (!ret) {
      LOG(ERROR) << "preprocess function failed";
      return Status::ERROR_BACKEND;
    }
  } catch (std::exception& e) {
    LOG(ERROR) << "Catch exception in preprocess: " << e.what();
    return Status::ERROR_BACKEND;
  }
  pack->predict_io.reset(new InferData);
  pack->predict_io->Set(std::move(input));

  return Status::SUCCESS;
}

}  // namespace infer_server
