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

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnedk_buf_surface_util.hpp"
#include "cnis/processor.h"
#include "cnrt.h"
#include "core/data_type.h"
#include "model/model.h"
#include "util/env.h"
#include "util/thread_pool.h"

using std::vector;

namespace infer_server {

static std::mutex gPostprocMapMutex;
static std::map<std::string, IPostproc *> gPostprocMap;

void SetPostprocHandler(const std::string &key, IPostproc *handler) {
  std::unique_lock<std::mutex> lk(gPostprocMapMutex);
  gPostprocMap[key] = handler;
}

IPostproc *GetPostprocHandler(const std::string &key) {
  std::unique_lock<std::mutex> lk(gPostprocMapMutex);
  if (gPostprocMap.count(key)) {
    return gPostprocMap[key];
  }
  return nullptr;
}

void RemovePostprocHandler(const std::string &key) {
  std::unique_lock<std::mutex> lk(gPostprocMapMutex);
  if (gPostprocMap.find(key) != gPostprocMap.end()) {
    gPostprocMap.erase(key);
  }
}

struct PostprocessorPrivate {
  ModelPtr model{nullptr};
  IPostproc* handler;
  // output layouts of model output on device
  vector<DataLayout> layouts;
};

Postprocessor::Postprocessor() noexcept : ProcessorForkable("Postprocessor"), priv_(new PostprocessorPrivate) {}

Postprocessor::~Postprocessor() {
  delete priv_;
  priv_ = nullptr;
}

Status Postprocessor::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << "[EasyDK InferServer] [Postprocessor] " << p << " has not been set";
      return Status::INVALID_PARAM;
    }
  }

  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    priv_->handler = GetPostprocHandler(priv_->model->GetKey());
    if (!priv_->handler) {
      LOG(WARNING) << "[EasyDK InferServer] [Postprocessor] The IPostproc handler has not been set,"
                   << " postprocessor will output ModelIO directly";
    }
    int device_id = GetParam<int>("device_id");

    if (!SetCurrentDevice(device_id)) return Status::ERROR_BACKEND;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "[EasyDK InferServer] [Postprocessor] Unmatched param type";
    return Status::WRONG_TYPE;
  }

  size_t o_num = priv_->model->OutputNum();
  priv_->layouts.reserve(o_num);
  for (size_t i = 0; i < o_num; ++i) {
    priv_->layouts.emplace_back(priv_->model->OutputLayout(i));
  }

  return Status::SUCCESS;
}

class HostDataDeleter : public cnedk::IBufDeleter {
 public:
  explicit HostDataDeleter(void* data) : data_(data) {}
  ~HostDataDeleter() { free(data_); }

 private:
  void* data_;
};

Status Postprocessor::Process(PackagePtr pack) noexcept {
  CHECK(pack) << "[EasyDK InferServer] [Postprocessor] Process pack. It should not be nullptr";
  if (!pack->predict_io || !pack->predict_io->HasValue()) {
    LOG(ERROR) << "[EasyDK InferServer] [Postprocessor] Can process continuous data only";
    return Status::INVALID_PARAM;
  }

  InferDataPtr cdata = nullptr;
  cdata.swap(pack->predict_io);

  ModelIO& out_mlu = cdata->GetLref<ModelIO>();

  // use real number of data as batch size
  const size_t batch_size = pack->data.size();

  // invoke user postprocess function
  std::vector<InferData*> datav;
  for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    datav.push_back(pack->data[batch_idx].get());
  }

  ModelIO outputs;
  for (size_t out_idx = 0; out_idx < out_mlu.surfs.size(); ++out_idx) {
    outputs.surfs.emplace_back(out_mlu.surfs[out_idx]);
    outputs.shapes.emplace_back(out_mlu.shapes[out_idx]);
  }
  if (priv_->handler) {
    priv_->handler->OnPostproc(datav, outputs, priv_->model.get());
  } else {
    VLOG(4) << "[EasyDK InferServer] [Postprocessor] do not have IPostproc handler, output ModelIO directly";
    for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
      ModelIO out;
      for (size_t out_idx = 0; out_idx < out_mlu.surfs.size(); ++out_idx) {
        void* host_data = out_mlu.surfs[out_idx]->GetHostData(0, batch_idx);
        uint32_t len = out_mlu.surfs[out_idx]->GetSurfaceParams(0)->data_size;
        void* data = malloc(len);
        memcpy(data, host_data, len);
        HostDataDeleter* deleter = new HostDataDeleter(data);
        auto surf = std::make_shared<cnedk::BufSurfaceWrapper>(data, len, CNEDK_BUF_MEM_SYSTEM, -1, deleter);
        out.surfs.emplace_back(surf);
        auto shape = out_mlu.shapes[out_idx];
        shape[0] = 1;
        out.shapes.emplace_back(shape);
      }
      pack->data[batch_idx]->Set(std::move(out));
    }
  }

  cdata.reset();

  return Status::SUCCESS;
}

}  // namespace infer_server
