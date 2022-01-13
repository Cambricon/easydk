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
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnrt.h"

#include "cnis/processor.h"
#include "core/data_type.h"
#include "model/model.h"

using std::shared_ptr;
using std::vector;

namespace infer_server {

#define CALL_CNRT_FUNC(func, msg)                    \
  do {                                               \
    cnrtRet_t ret = (func);                          \
    if (CNRT_RET_SUCCESS != ret) {                   \
      LOG(ERROR) << (msg) << " error code: " << ret; \
      return Status::ERROR_BACKEND;                  \
    }                                                \
  } while (0)
struct PredictorPrivate {
  // FIXME(dmh): data is not always float
  void DumpData(vector<Buffer>& in, vector<Buffer>& out) {
    // input
    int64_t in_data_count = model->InputShape(0).BatchDataCount();
    size_t in_data_size = in_data_count * GetTypeSize(model->InputLayout(0).dtype);
    Buffer in_cpu(in_data_size);
    in_cpu.CopyFrom(in[0], in_data_size);
    const float* i_data = reinterpret_cast<const float*>(in_cpu.Data());

    std::ofstream in_file("in.txt");
    for (int idx = 0; idx < in_data_count; ++idx) {
      in_file << i_data[idx] << "\n";
    }
    in_file.close();

    // output
    int64_t data_count = model->OutputShape(0).BatchDataCount();
    size_t data_size = data_count * GetTypeSize(layouts[0].dtype);
    Buffer out_cpu(data_size);
    out_cpu.CopyFrom(out[0], data_size);
    const float* o_data = reinterpret_cast<const float*>(out_cpu.Data());

    std::ofstream out_file("out.txt");
    for (int idx = 0; idx < data_count; ++idx) {
      out_file << o_data[idx] << "\n";
    }
    out_file.close();
  }
  ModelPtr model{nullptr};
  vector<std::unique_ptr<MluMemoryPool>> output_pools;
  std::shared_ptr<ModelRunner> runner;
  // output layouts of model output on device
  vector<DataLayout> layouts;
};

Predictor::Predictor() noexcept : ProcessorForkable("Predictor"), priv_(new PredictorPrivate) {}

Predictor::~Predictor() {
  priv_->output_pools.clear();

  delete priv_;
}

Status Predictor::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << p << " has not been set";
      return Status::INVALID_PARAM;
    }
  }

  int device_id = 0;
  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    device_id = GetParam<int>("device_id");

    if (!SetCurrentDevice(device_id)) return Status::ERROR_BACKEND;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "unmatched param type";
    return Status::WRONG_TYPE;
  }

  priv_->runner = ModelManager::Instance()->GetModel(priv_->model->GetKey())->GetRunner(device_id);
  if (!priv_->runner) {
    return Status::INVALID_PARAM;
  }

  size_t o_num = priv_->model->OutputNum();
  priv_->layouts.reserve(o_num);

  // Create output memory pool only if it is possible to get model output shape before execute the model.
  if (priv_->model->FixedOutputShape()) {
    for (size_t i = 0; i < o_num; ++i) {
      priv_->layouts.emplace_back(priv_->model->OutputLayout(i));
      // FIXME(dmh): 3 buffer?
      priv_->output_pools.emplace_back(new MluMemoryPool(
          priv_->model->OutputShape(i).BatchDataCount() * GetTypeSize(priv_->layouts[i].dtype), 3, device_id));
    }
#ifndef CNIS_USE_MAGICMIND
  } else {
    LOG(ERROR) << "The output shapes of the model are not fixed.";
    return Status::INVALID_PARAM;
#endif
  }

  return Status::SUCCESS;
}

Status Predictor::Process(PackagePtr pack) noexcept {
  CHECK(pack);
  if (!pack->predict_io || !pack->predict_io->HasValue()) {
    LOG(ERROR) << "Predictor can process continuous data only";
    return Status::INVALID_PARAM;
  }

  // previous processor must provide continuous_data to avoid copy
  InferDataPtr& cdata = pack->predict_io;

  ModelIO out_mlu;
  out_mlu.buffers.reserve(priv_->model->OutputNum());
  out_mlu.shapes.reserve(priv_->model->OutputNum());
  Status s = Status::SUCCESS;
  try {
    ModelIO& in_mlu = cdata->GetLref<ModelIO>();
    if (priv_->runner->CanInferOutputShape() && priv_->model->FixedOutputShape()) {
#ifdef CNIS_INFER_SHAPE_MUTABLE
      out_mlu.shapes = priv_->runner->InferOutputShape(in_mlu.shapes);
      if (out_mlu.shapes.empty()) {
        LOG(ERROR) << "Invalid shapes: " << in_mlu.shapes;
        return Status::ERROR_BACKEND;
      }
      size_t need_size;
      for (size_t idx = 0; idx < priv_->output_pools.size(); ++idx) {
        need_size = out_mlu.shapes[idx].BatchDataCount() * GetTypeSize(priv_->layouts[idx].dtype);
        if (need_size > priv_->output_pools[idx]->MemorySize()) {
          LOG(INFO) << "size larger than mem pool, malloc buffer instantly";
          out_mlu.buffers.emplace_back(need_size, priv_->output_pools[idx]->DeviceId());
        } else {
          out_mlu.buffers.emplace_back(priv_->output_pools[idx]->Request());
        }
      }
#else
      for (size_t idx = 0; idx < priv_->output_pools.size(); ++idx) {
        out_mlu.buffers.emplace_back(priv_->output_pools[idx]->Request());
        out_mlu.shapes.emplace_back(priv_->model->OutputShape(idx));
      }
#endif
    }
    s = priv_->runner->Run(&in_mlu, &out_mlu);
  } catch (bad_any_cast&) {
    LOG(ERROR) << "predictor received unsupported data type";
    return Status::WRONG_TYPE;
  }

  cdata->Set(std::move(out_mlu));
  return s;
}

std::string Predictor::Backend() noexcept {
#ifdef CNIS_USE_MAGICMIND
  return "magicmind";
#else
  return "cnrt";
#endif
}

}  // namespace infer_server
