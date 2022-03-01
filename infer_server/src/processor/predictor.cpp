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
#include "util/env.h"

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
#ifndef NDEBUG
  void PrintTo(std::ostream& os, const void* data, DataType dtype, size_t cnt) {
    if (dtype == DataType::FLOAT32) {
      const float* i_data = reinterpret_cast<const float*>(data);
      for (size_t idx = 0; idx < cnt; ++idx) {
        os << i_data[idx] << "\n";
      }
    } else if (dtype == DataType::FLOAT16) {
      const uint16_t* i_data = reinterpret_cast<const uint16_t*>(data);
      float d;
      for (size_t idx = 0; idx < cnt; ++idx) {
        auto ret = cnrtConvertHalfToFloat(&d, i_data[idx]);
        if (ret != CNRT_RET_SUCCESS) throw std::runtime_error("internal error");
        os << d << "\n";
      }
    } else if (dtype == DataType::UINT8) {
      const uint8_t* i_data = reinterpret_cast<const uint8_t*>(data);
      for (size_t idx = 0; idx < cnt; ++idx) {
        os << static_cast<uint32_t>(i_data[idx]) << "\n";
      }
    } else {
      throw std::runtime_error("unsupported dtype");
    }
  }
  void DumpData(vector<Buffer>& in, vector<Buffer>& out) {  // NOLINT
    LOG(INFO) << "dump model input/output   --" << model->GetKey();
    // input
    for (uint32_t i_idx = 0; i_idx < in.size(); ++i_idx) {
      int64_t in_data_count = model->InputShape(i_idx).BatchDataCount();
      size_t in_data_size = in_data_count * GetTypeSize(model->InputLayout(i_idx).dtype);
      Buffer in_cpu(in_data_size);
      in_cpu.CopyFrom(in[i_idx], in_data_size);

      std::string f_name = "in_" + std::to_string(i_idx) + ".txt";
      std::ofstream in_f(f_name);
      CHECK(in_f.is_open());
      PrintTo(in_f, in_cpu.Data(), model->InputLayout(i_idx).dtype, in_data_count);
      in_f.close();
    }

    // output
    for (uint32_t o_idx = 0; o_idx < out.size(); ++o_idx) {
      int64_t out_data_count = model->OutputShape(o_idx).BatchDataCount();
      size_t out_data_size = out_data_count * GetTypeSize(layouts[o_idx].dtype);
      Buffer out_cpu(out_data_size);
      out_cpu.CopyFrom(out[o_idx], out_data_size);

      std::string f_name = "out_" + std::to_string(o_idx) + ".txt";
      std::ofstream out_f(f_name);
      CHECK(out_f.is_open());
      PrintTo(out_f, out_cpu.Data(), model->OutputLayout(o_idx).dtype, out_data_count);
      out_f.close();
    }
  }
  #endif
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

#ifndef NDEBUG
    static bool dump_data = GetBoolFromEnv("CNIS_DUMP_MODEL_IO", false);
    if (dump_data) { priv_->DumpData(in_mlu.buffers, out_mlu.buffers); }
#endif
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
