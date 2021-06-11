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
#include <memory>
#include <utility>
#include <vector>

#include "cnrt.h"

#include "core/data_type.h"
#include "device/mlu_context.h"
#include "model/model.h"
#include "processor.h"

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

    edk::MluContext ctx;
    ctx.SetDeviceId(device_id);
    ctx.BindDevice();
  } catch (edk::Exception& e) {
    LOG(ERROR) << e.what();
    return Status::ERROR_BACKEND;
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
  for (size_t i = 0; i < o_num; ++i) {
    priv_->layouts.emplace_back(priv_->model->OutputLayout(i));
    // FIXME(dmh): 3 buffer?
    priv_->output_pools.emplace_back(new MluMemoryPool(
        priv_->model->OutputShape(i).BatchDataCount() * GetTypeSize(priv_->layouts[i].dtype), 3, device_id));
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
  Status s = Status::SUCCESS;
  try {
    ModelIO& in_mlu = cdata->GetLref<ModelIO>();
    for (size_t idx = 0; idx < priv_->output_pools.size(); ++idx) {
      out_mlu.buffers.emplace_back(priv_->output_pools[idx]->Request());
      out_mlu.shapes.emplace_back(priv_->model->OutputShape(idx));
    }
    s = priv_->runner->Run(in_mlu.buffers, out_mlu.buffers);
  } catch (bad_any_cast&) {
    LOG(ERROR) << "predictor received unsupported data type";
    return Status::WRONG_TYPE;
  }

  cdata->Set(std::move(out_mlu));
  return s;
}

}  // namespace infer_server
