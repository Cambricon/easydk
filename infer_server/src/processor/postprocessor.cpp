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

#include "cnis/processor.h"
#include "cnrt.h"
#include "core/data_type.h"
#include "model/model.h"
#include "util/env.h"
#include "util/thread_pool.h"

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
struct PostprocessorPrivate {
  static std::unique_ptr<EqualityThreadPool> tp;
  static std::mutex tp_mutex;
  ModelPtr model{nullptr};
  Postprocessor::ProcessFunction process_func;
  // output layouts of model output on device
  vector<DataLayout> layouts;
  // user specified output layout on host
  DataLayout host_layout;
  uint32_t increased_tp{0};
};

// all the postprocessor instance share one thread pool, to reduce total thread number
// add threads into pool in each `Init()`, until reach max_thread_num (1 * CPU core number)
// check idle thread number and remove threads in each destruct
std::unique_ptr<EqualityThreadPool> PostprocessorPrivate::tp{nullptr};
std::mutex PostprocessorPrivate::tp_mutex;

Postprocessor::Postprocessor() noexcept : ProcessorForkable("Postprocessor"), priv_(new PostprocessorPrivate) {}

Postprocessor::~Postprocessor() {
  std::unique_lock<std::mutex> lk(priv_->tp_mutex);
  if (priv_->increased_tp) {
    uint32_t idle_num = priv_->tp->IdleNumber();
    if (idle_num > priv_->increased_tp) {
      if (priv_->increased_tp == priv_->tp->Size()) {
        VLOG(3) << "Destroy postproc worker thread pool";
        // no any other postproc instance, ensure no task in pool
        priv_->tp->Stop(true);
        priv_->tp.reset();
      } else {
        VLOG(3) << "Reduce " << priv_->increased_tp << " thread in postprocessor pool after destruct Postprocessor";
        priv_->tp->Resize(priv_->tp->Size() - priv_->increased_tp);
      }
    }
    lk.unlock();
  }
  delete priv_;
  priv_ = nullptr;
}

Status Postprocessor::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id", "host_output_layout"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << p << " has not been set";
      return Status::INVALID_PARAM;
    }
  }

  int parallel = 0;
  try {
    priv_->model = GetParam<ModelPtr>("model_info");
    priv_->host_layout = GetParam<DataLayout>("host_output_layout");
    priv_->process_func = HaveParam("process_function") ? GetParam<ProcessFunction>("process_function") : nullptr;
    if (!priv_->process_func) {
      LOG(WARNING) << "process_function has not been set, postprocessor will output ModelIO directly";
    }
    int device_id = GetParam<int>("device_id");

    parallel = HaveParam("parallel") ? GetParam<int>("parallel") : 0;

    if (!SetCurrentDevice(device_id)) return Status::ERROR_BACKEND;
  } catch (bad_any_cast&) {
    LOG(ERROR) << "unmatched param type";
    return Status::WRONG_TYPE;
  }

  if (priv_->process_func) {
    // increased number of threads limited in [1, 16]
    priv_->increased_tp = parallel > 0 ? (parallel < 16 ? parallel : 16) : 4;
    std::unique_lock<std::mutex> lk(priv_->tp_mutex);
    if (!priv_->tp) {
      VLOG(3) << "Create postproc worker thread pool";
      priv_->tp.reset(new EqualityThreadPool(nullptr));
    }
    int th_num = priv_->tp->Size();
    static const int max_th_num = GetCpuCoreNumber();
    if (th_num < max_th_num) {
      // TODO(dmh): user set?
      VLOG(3) << "Increase " << priv_->increased_tp << " thread in postprocessor pool when init postprocessor";
      priv_->tp->Resize(th_num + priv_->increased_tp);
    }
    lk.unlock();
  }

  size_t o_num = priv_->model->OutputNum();
  priv_->layouts.reserve(o_num);
  for (size_t i = 0; i < o_num; ++i) {
    priv_->layouts.emplace_back(priv_->model->OutputLayout(i));
  }

  return Status::SUCCESS;
}

Status Postprocessor::Process(PackagePtr pack) noexcept {
  CHECK(pack);
  if (!pack->predict_io || !pack->predict_io->HasValue()) {
    LOG(ERROR) << "Postprocessor can process continuous data only";
    return Status::INVALID_PARAM;
  }

  InferDataPtr cdata = nullptr;
  cdata.swap(pack->predict_io);

  ModelIO& out_mlu = cdata->GetLref<ModelIO>();
  vector<Buffer> out_cpu;
  out_cpu.reserve(out_mlu.buffers.size());

  size_t host_type_size = GetTypeSize(priv_->host_layout.dtype);
  for (size_t out_idx = 0; out_idx < out_mlu.buffers.size(); ++out_idx) {
    DataLayout out_layout = priv_->layouts[out_idx];
    size_t batch_data_len = out_mlu.shapes[out_idx].BatchDataCount();
    size_t type_size = GetTypeSize(out_layout.dtype);
    Buffer out_tmp(batch_data_len * type_size);
    out_cpu.emplace_back(batch_data_len * host_type_size);

    if ((out_layout.order == DimOrder::NCHW || out_layout.order == DimOrder::NHWC) &&
        (priv_->host_layout.order == DimOrder::NCHW || priv_->host_layout.order == DimOrder::NHWC)) {
      // Trans order only support NCHWTONHWC and NHWCTONCHW.
      // TransLayout need NHWC shape
      Shape s = out_layout.order == DimOrder::NCHW ? Shape(DimNCHW2NHWC(out_mlu.shapes[out_idx].Vectorize()))
                                                   : out_mlu.shapes[out_idx];
      // TODO(dmh): multi output host_layout set by user?
      // do not cast INT32 to float
      bool no_cast = out_layout.dtype == priv_->host_layout.dtype || out_layout.dtype == DataType::INT32;
      // do not transpose when num of dims == 1 / 2
      bool no_transpose = priv_->layouts[out_idx].order == priv_->host_layout.order || s.Size() < 3;
      // copy to host
      if (no_cast && no_transpose) {
        out_mlu.buffers[out_idx].CopyTo(&out_cpu[out_idx], batch_data_len * type_size);
        continue;
      }
      out_mlu.buffers[out_idx].CopyTo(&out_tmp, batch_data_len * type_size);
      // do not cast INT32 to float
      DataLayout tmp = priv_->host_layout;
      tmp.dtype = priv_->layouts[out_idx].dtype == DataType::INT32 ? DataType::INT32 : tmp.dtype;
      // transform data layout
      detail::TransLayout(out_tmp.MutableData(), out_cpu[out_idx].MutableData(), priv_->layouts[out_idx],
                          priv_->host_layout, s);
    } else {
      // Trans order not supported except NCHWTONHWC and NHWCTONCHW.
      if (out_layout.dtype == priv_->host_layout.dtype || out_layout.dtype == DataType::INT32) {
        // no need to cast data type
        out_mlu.buffers[out_idx].CopyTo(&out_cpu[out_idx], batch_data_len * type_size);
      } else {
        // cast data type
        out_mlu.buffers[out_idx].CopyTo(&out_tmp, batch_data_len * type_size);
        detail::CastDataType(out_tmp.MutableData(), out_cpu[out_idx].MutableData(), out_layout.dtype,
                             priv_->host_layout.dtype, out_mlu.shapes[out_idx]);
      }
    }
  }

  // use real number of data as batch size
  const size_t batch_size = pack->data.size();
  std::vector<std::future<bool>> res;
  res.reserve(batch_size);
  // collect output shapes and layouts
  std::vector<Shape> out_shapes;
  std::vector<DataLayout> out_layouts;
  for (size_t out_idx = 0; out_idx < out_cpu.size(); ++out_idx) {
    Shape s = out_mlu.shapes[out_idx];
    if ((priv_->layouts[out_idx].order == DimOrder::NCHW || priv_->layouts[out_idx].order == DimOrder::NHWC) &&
        (priv_->host_layout.order == DimOrder::NCHW || priv_->host_layout.order == DimOrder::NHWC)) {
      bool need_transpose = (priv_->layouts[out_idx].order != priv_->host_layout.order) &&
                            (out_mlu.shapes[out_idx].Size() >= 3);
      // shape is corresponding to buffer after translayout
      if (need_transpose) {
        if (priv_->host_layout.order == DimOrder::NHWC) {
          s = Shape(DimNCHW2NHWC(out_mlu.shapes[out_idx].Vectorize()));
        } else if (priv_->host_layout.order == DimOrder::NCHW) {
          s = Shape(DimNHWC2NCHW(out_mlu.shapes[out_idx].Vectorize()));
        }
      }
      s[0] = 1;
      DataLayout layout = {priv_->host_layout.dtype, priv_->host_layout.order};
      out_layouts.emplace_back(layout);
    } else {
      VLOG(4) << "Not transpose output dim order to host dim order, use the original dim order";
      DataLayout layout = {priv_->host_layout.dtype, priv_->layouts[out_idx].order};
      out_layouts.emplace_back(layout);
    }
    out_shapes.emplace_back(std::move(s));
  }

  // invoke user postprocess function
  for (size_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    ModelIO outputs;
    for (size_t out_idx = 0; out_idx < out_cpu.size(); ++out_idx) {
      outputs.buffers.emplace_back(out_cpu[out_idx](batch_idx * host_type_size * out_shapes[out_idx].DataCount()));
    }
    outputs.shapes = out_shapes;
    if (priv_->process_func) {
      res.emplace_back(priv_->tp->Push(0, priv_->process_func, pack->data[batch_idx].get(), std::move(outputs),
                                       priv_->model.get()));
    } else {
      VLOG(5) << "do not have process_function, output ModelIO directly";
      pack->data[batch_idx]->Set(std::move(outputs));
    }
  }

  // wait for process finish
  try {
    for (auto& fut : res) {
      fut.wait();
      if (!fut.get()) {
        LOG(ERROR) << "postprocess failed";
        return Status::ERROR_BACKEND;
      }
    }
  } catch (std::exception& e) {
    LOG(ERROR) << "Catch exception in postprocess: " << e.what();
    return Status::ERROR_BACKEND;
  }

  cdata.reset();

  return Status::SUCCESS;
}

}  // namespace infer_server
