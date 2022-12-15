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

#include "cnedk_platform.h"
#include "cnedk_buf_surface_util.hpp"
#include "cnis/processor.h"
#include "core/data_type.h"
#include "model/model.h"
#include "../common/utils.hpp"

using std::shared_ptr;
using std::vector;

namespace infer_server {

struct PredictorPrivate {
  ModelPtr model{nullptr};
  vector<std::shared_ptr<cnedk::BufPool>> output_pools;
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
      LOG(ERROR) << "[EasyDK InferServer] [Predictor] " << p << " has not been set";
      return Status::INVALID_PARAM;
    }
  }

  int device_id = 0;
  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    device_id = GetParam<int>("device_id");

    if (cnrtSetDevice(device_id) != cnrtSuccess) return Status::ERROR_BACKEND;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "[EasyDK InferServer] [Predictor] Unmatched param type";
    return Status::WRONG_TYPE;
  }

  priv_->runner = ModelManager::Instance()->GetModel(priv_->model->GetKey())->GetRunner(device_id);
  if (!priv_->runner) {
    return Status::INVALID_PARAM;
  }

  CnedkPlatformInfo platform_info;
  if (CnedkPlatformGetInfo(device_id, &platform_info) < 0) {
    return Status::INVALID_PARAM;
  }
  std::string platform_name(platform_info.name);

  size_t o_num = priv_->model->OutputNum();
  priv_->layouts.reserve(o_num);

  // Create output memory pool only if it is possible to get model output shape before execute the model.
  if (priv_->model->FixedOutputShape()) {
    for (size_t i = 0; i < o_num; ++i) {
      priv_->layouts.emplace_back(priv_->model->OutputLayout(i));
      // FIXME(dmh): 3 buffer?
      std::shared_ptr<cnedk::BufPool> pool = std::make_shared<cnedk::BufPool>();
      CnedkBufSurfaceCreateParams create_params;
      memset(&create_params, 0, sizeof(create_params));
      if (cnedk::IsEdgePlatform(platform_name)) {
        create_params.mem_type = CNEDK_BUF_MEM_UNIFIED_CACHED;
      } else {
        create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
      }
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
      create_params.device_id = device_id;
      create_params.batch_size = priv_->model->BatchSize();
      create_params.force_align_1 = 1;  // to meet mm's requirement
      create_params.size = priv_->model->OutputShape(i).BatchDataCount() * GetTypeSize(priv_->layouts[i].dtype);
      create_params.size /= create_params.batch_size;
      pool->CreatePool(&create_params, 3);
      priv_->output_pools.emplace_back(pool);
    }
  }
  return Status::SUCCESS;
}

Status Predictor::Process(PackagePtr pack) noexcept {
  CHECK(pack) << "[EasyDK InferServer] [Predictor] Process pack. It should not be empty";
  if (!pack->predict_io || !pack->predict_io->HasValue()) {
    LOG(ERROR) << "[EasyDK InferServer] [Predictor] Can process continuous data only";
    return Status::INVALID_PARAM;
  }

  // previous processor must provide continuous_data to avoid copy
  InferDataPtr& cdata = pack->predict_io;

  ModelIO out_mlu;
  out_mlu.surfs.reserve(priv_->model->OutputNum());
  out_mlu.shapes.reserve(priv_->model->OutputNum());
  Status s = Status::SUCCESS;
  try {
    ModelIO& in_mlu = cdata->GetLref<ModelIO>();
    if (priv_->runner->CanInferOutputShape() && priv_->model->FixedOutputShape()) {
      for (size_t idx = 0; idx < priv_->output_pools.size(); ++idx) {
        out_mlu.surfs.emplace_back(priv_->output_pools[idx]->GetBufSurfaceWrapper(1000));
        out_mlu.shapes.emplace_back(priv_->model->OutputShape(idx));
      }
    }
    s = priv_->runner->Run(&in_mlu, &out_mlu);
  } catch (bad_any_cast&) {
    LOG(ERROR) << "[EasyDK InferServer] [Predictor] Received unsupported data type";
    return Status::WRONG_TYPE;
  }

  cdata->Set(std::move(out_mlu));
  return s;
}

std::string Predictor::Backend() noexcept { return "magicmind"; }

}  // namespace infer_server
