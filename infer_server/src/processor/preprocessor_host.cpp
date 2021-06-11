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

#include <memory>
#include <utility>
#include <vector>

#include "core/data_type.h"
#include "device/mlu_context.h"
#include "processor.h"
#include "util/env.h"
#include "util/thread_pool.h"

namespace infer_server {

#define CHECK_CNRT_RET(ret, msg, val)              \
  do {                                             \
    if ((ret) != CNRT_RET_SUCCESS) {               \
      LOG(ERROR) << msg << " error code: " << ret; \
      return val;                                  \
    }                                              \
  } while (0)

struct PreprocessorHostPrivate {
  static std::unique_ptr<EqualityThreadPool> tp;
  static std::mutex tp_mutex;
  ModelPtr model{nullptr};
  PreprocessorHost::ProcessFunction process_func{nullptr};

  std::vector<std::unique_ptr<MluMemoryPool>> pools;
  std::vector<Buffer> dst;
  std::vector<Buffer> dst_tmp;
  std::vector<Shape> shapes;
  std::vector<DataLayout> layouts;
  DataLayout host_layout;
  uint32_t increased_tp{0};
};

// all the preprocessor_host instance share one thread pool, to reduce total thread number
// add threads into pool in each `Init()`, until reach max_thread_num (1 * CPU core number)
// check idle thread number and remove threads in each destruct
std::unique_ptr<EqualityThreadPool> PreprocessorHostPrivate::tp{nullptr};
std::mutex PreprocessorHostPrivate::tp_mutex;

PreprocessorHost::PreprocessorHost() noexcept
    : ProcessorForkable("PreprocessorHost"), priv_(new PreprocessorHostPrivate) {}

PreprocessorHost::~PreprocessorHost() {
  priv_->dst_tmp.clear();
  priv_->dst.clear();
  for (size_t i = 0; i < priv_->pools.size(); ++i) {
    if (priv_->pools[i]) {
      priv_->pools[i].reset(nullptr);
    }
  }
  priv_->pools.clear();

  if (priv_->increased_tp) {
    std::unique_lock<std::mutex> lk(priv_->tp_mutex);
    uint32_t idle_num = priv_->tp->IdleNumber();
    if (idle_num > priv_->increased_tp) {
      if (priv_->increased_tp == priv_->tp->Size()) {
        // if we resize to 0 threads here, segment fault will occur occasionally!
        // but it won't happen under debug mode, so the bug cannot be located.
        // simply avoid resize to 0 thread, as workaround.
        VLOG(3) << "Destroy preproc_host worker thread pool";
        // no any other preproc instance, ensure no task in pool
        priv_->tp->Stop(true);
        priv_->tp.reset();
      } else {
        VLOG(3) << "Reduce " << priv_->increased_tp << " thread in preprocessor pool after destruct PreprocessorHost";
        priv_->tp->Resize(priv_->tp->Size() - priv_->increased_tp);
      }
    }
  }
  delete priv_;
  priv_ = nullptr;
}

Status PreprocessorHost::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id", "host_input_layout"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << p << " has not been set!";
      return Status::INVALID_PARAM;
    }
  }

  int parallel = 0;
  int device_id = 0;
  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    priv_->host_layout = GetParam<DataLayout>("host_input_layout");
    device_id = GetParam<int>("device_id");
    priv_->process_func = HaveParam("process_function") ? GetParam<ProcessFunction>("process_function") : nullptr;
    parallel = HaveParam("parallel") ? GetParam<int>("parallel") : 0;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "wrong param type";
    return Status::WRONG_TYPE;
  }

  // no process function will just pass through
  if (priv_->process_func) {
    // increased number of threads limited in [1, 8]
    priv_->increased_tp = parallel > 0 ? (parallel < 8 ? parallel : 8) : 2;
    std::unique_lock<std::mutex> lk(priv_->tp_mutex);
    if (!priv_->tp) {
      VLOG(3) << "Create preproc_host worker thread pool";
      priv_->tp.reset(new EqualityThreadPool(nullptr));
    }
    int th_num = priv_->tp->Size();
    static const int max_th_num = GetCpuCoreNumber();
    if (th_num < max_th_num) {
      // TODO(dmh): user define?
      VLOG(3) << "Increase " << priv_->increased_tp << " thread in preprocessor pool when init preprocessor";
      priv_->tp->Resize(th_num + priv_->increased_tp);
    }
    lk.unlock();

    // init dst memory and shapes
    size_t i_num = priv_->model->InputNum();
    priv_->shapes.reserve(i_num);
    priv_->layouts.reserve(i_num);
    priv_->dst.reserve(i_num);
    priv_->dst_tmp.reserve(i_num);
    for (size_t i_idx = 0; i_idx < i_num; ++i_idx) {
      priv_->shapes.emplace_back(priv_->model->InputShape(i_idx));
      priv_->layouts.emplace_back(priv_->model->InputLayout(i_idx));
      size_t data_count = priv_->shapes[i_idx].BatchDataCount();
      priv_->dst_tmp.emplace_back(data_count * GetTypeSize(priv_->host_layout.dtype));
      priv_->dst.emplace_back(data_count * GetTypeSize(priv_->layouts[i_idx].dtype));
      priv_->pools.emplace_back(new MluMemoryPool(data_count * GetTypeSize(priv_->layouts[i_idx].dtype), 3, device_id));
    }
  }

  return Status::SUCCESS;
}

Status PreprocessorHost::Process(PackagePtr pack) noexcept {
  size_t batch_size = pack->data.size();
  if (!priv_->process_func) {
    VLOG(5) << "No preprocess function, package pass through";
    return Status::SUCCESS;
  }
  size_t i_num = priv_->model->InputNum();
  // offset
  std::vector<ModelIO> dst_tmp_batch;
  size_t host_type_size = GetTypeSize(priv_->host_layout.dtype);
  dst_tmp_batch.reserve(batch_size);
  for (size_t batch_idx = 0; batch_idx < pack->data.size(); ++batch_idx) {
    dst_tmp_batch.emplace_back();
    auto& tmp = dst_tmp_batch[batch_idx];
    tmp.buffers.reserve(i_num);
    tmp.shapes.reserve(i_num);
    for (size_t i_idx = 0; i_idx < i_num; ++i_idx) {
      Shape s = priv_->shapes[i_idx];
      s[0] = 1;
      tmp.buffers.emplace_back(priv_->dst_tmp[i_idx](batch_idx * s.DataCount() * host_type_size));
      tmp.shapes.emplace_back(std::move(s));
    }
  }

  std::vector<std::future<bool>> res;
  res.reserve(batch_size);
  for (size_t batch_idx = 0; batch_idx < pack->data.size(); ++batch_idx) {
    res.emplace_back(priv_->tp->Push(0, priv_->process_func, &dst_tmp_batch[batch_idx],
                                     std::ref(*(pack->data[batch_idx])), std::ref(*(priv_->model))));
  }

  // wait for process finish
  try {
    for (auto& fut : res) {
      if (!fut.get()) {
        return Status::ERROR_BACKEND;
      }
    }
  } catch (std::exception& e) {
    LOG(ERROR) << "Catch exception in preprocess: " << e.what();
    return Status::ERROR_BACKEND;
  }
  // release input data
  for (auto& it : pack->data) {
    it->data.reset();
  }

  dst_tmp_batch.clear();
  // transform layout and copy to MLU
  ModelIO input;
  input.buffers.reserve(i_num);
  input.shapes.reserve(i_num);
  for (size_t i_idx = 0; i_idx < i_num; ++i_idx) {
    if (!detail::TransLayout(priv_->dst_tmp[i_idx].MutableData(), priv_->dst[i_idx].MutableData(),
                             priv_->host_layout, priv_->layouts[i_idx], priv_->shapes[i_idx])) {
      return Status::ERROR_BACKEND;
    }
    input.shapes.emplace_back(priv_->shapes[i_idx]);
    input.buffers.emplace_back(priv_->pools[i_idx]->Request());
    input.buffers[i_idx].CopyFrom(priv_->dst[i_idx],
                                  priv_->shapes[i_idx].BatchDataCount() * GetTypeSize(priv_->layouts[i_idx].dtype));
  }
  pack->predict_io.reset(new InferData);
  pack->predict_io->Set(std::move(input));

  return Status::SUCCESS;
}

}  // namespace infer_server
