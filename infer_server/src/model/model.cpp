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

#include "model.h"

#include <glog/logging.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "cnrt.h"
#include "core/data_type.h"
#include "internal/cnrt_wrap.h"

using std::string;
using std::vector;

#define CHECK_CNRT_RET(ret, msg, val)                \
  do {                                               \
    if ((ret) != CNRT_RET_SUCCESS) {                 \
      LOG(ERROR) << (msg) << " error code: " << (ret); \
      return val;                                    \
    }                                                \
  } while (0)

#define CALL_CNRT_FUNC(func, msg) CHECK_CNRT_RET(func, msg, Status::ERROR_BACKEND)
#ifdef CNIS_USE_MAGICMIND
namespace infer_server {

#define MM_SAFECALL(func, val)                                   \
  do {                                                           \
    auto ret = (func);                                           \
    if (!ret.ok()) {                                             \
      LOG(ERROR) << "Call " #func " failed, extra msg: " << ret; \
      return val;                                                \
    }                                                            \
  } while (0)

bool ModelRunner::Init(MModel* model, mm_unique_ptr<MContext> ctx) noexcept {
  input_num_ = model->GetInputNum();
  output_num_ = model->GetOutputNum();
  ctx_ = std::move(ctx);

  i_shapes_.resize(input_num_);
  o_shapes_.resize(output_num_);

  MM_SAFECALL(mm::CreateInputTensors(ctx_.get(), &inputs_), false);
  MM_SAFECALL(mm::CreateOutputTensors(ctx_.get(), &outputs_), false);

  auto dims2shape = [](const mm::Dims& d) { return Shape(d.GetDims()); };
  std::vector<mm::Dims> in_dims = model->GetInputDimensions();
  std::vector<Shape> in_shapes;
  std::transform(in_dims.begin(), in_dims.end(), std::back_inserter(in_shapes), dims2shape);
  auto out_shape = InferOutputShape(in_shapes);
  if (out_shape.empty()) {
    for (auto tensor : outputs_) {
      tensor->Destroy();
    }
    outputs_.clear();
  }

  VLOG(3) << "Create cnrt queue";
  cnrtRet_t ret = cnrt::QueueCreate(&task_queue_);
  CHECK_CNRT_RET(ret, "Create Queue failed", false);
#ifdef PERF_HARDWARE_TIME
  // create notifier for hardware time
  ret = cnrt::NotifierCreate(&notifier_start_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
  ret = cnrt::NotifierCreate(&notifier_end_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
#endif
  return true;
}

ModelRunner::~ModelRunner() {
  SetCurrentDevice(device_id_);
  if (task_queue_) {
    cnrt::QueueDestroy(task_queue_);
    task_queue_ = nullptr;
  }

  for (auto& it : inputs_) {
    it->Destroy();
  }
  for (auto& it : outputs_) {
    it->Destroy();
  }
#ifdef PERF_HARDWARE_TIME
  if (notifier_start_) {
    cnrt::NotifierDestroy(&notifier_start_);
    notifier_start_ = nullptr;
  }
  if (notifier_end_) {
    cnrt::NotifierDestroy(&notifier_end_);
    notifier_end_ = nullptr;
  }
#endif
}

std::vector<Shape> ModelRunner::InferOutputShape(const std::vector<Shape>& input) noexcept {
  if (input.size() != inputs_.size()) return {};
  bool same_shape = true;
  for (uint32_t idx = 0; idx < i_shapes_.size(); ++idx) {
    if (i_shapes_[idx] != input[idx]) {
      same_shape = false;
      break;
    }
  }
  if (same_shape) return o_shapes_;

  // shape changed, infer output shape
  for (uint32_t idx = 0; idx < inputs_.size(); ++idx) {
    inputs_[idx]->SetDimensions(mm::Dims(input[idx].Vectorize()));
  }
  MM_SAFECALL(ctx_->InferOutputShape(inputs_, outputs_), {});
  for (uint32_t idx = 0; idx < outputs_.size(); ++idx) {
    o_shapes_[idx] = outputs_[idx]->GetDimensions().GetDims();
  }
  i_shapes_ = input;
  VLOG(4) << "inference shape changed ---"
          << "\n\tinput: " << i_shapes_ << "\n\toutput: " << o_shapes_;
  return o_shapes_;
}

Status ModelRunner::Run(ModelIO* in, ModelIO* out) noexcept {  // NOLINT
  auto& input = in->buffers;
  auto& output = out->buffers;
  CHECK_EQ(input_num_, input.size());

  VLOG(6) << "Process inference once, input num: " << input_num_ << " output num: " << output_num_;

  for (uint32_t i_idx = 0; i_idx < input_num_; ++i_idx) {
    inputs_[i_idx]->SetData(input[i_idx].MutableData());
  }
  if (!output.empty()) {
    CHECK_EQ(output_num_, output.size());
    for (uint32_t o_idx = 0; o_idx < output_num_; ++o_idx) {
      outputs_[o_idx]->SetData(output[o_idx].MutableData());
    }
  }

#ifdef PERF_HARDWARE_TIME
  // place start event
  CALL_CNRT_FUNC(cnrt::PlaceNotifier(notifier_start_, task_queue_), "Place event failed");
#endif

  if (output.empty()) {
    // buffer is managed by magicmind, pass empty vector as output
    MM_SAFECALL(ctx_->Enqueue(inputs_, &outputs_, task_queue_), Status::ERROR_BACKEND);
  } else {
    MM_SAFECALL(ctx_->Enqueue(inputs_, outputs_, task_queue_), Status::ERROR_BACKEND);
  }

#ifdef PERF_HARDWARE_TIME
  // place end event
  CALL_CNRT_FUNC(cnrt::PlaceNotifier(notifier_end_, task_queue_), "Place event failed");
#endif

  CALL_CNRT_FUNC(cnrt::QueueSync(task_queue_), "Sync queue failed.");

  if (output.empty()) {
    for (MTensor* tensor : outputs_) {
      output.emplace_back(tensor->GetMutableData(), tensor->GetSize(),
                          [tensor](void* mem, int dev_id) { tensor->Destroy(); }, device_id_);
      out->shapes.emplace_back(tensor->GetDimensions().GetDims());
    }
    outputs_.clear();
  }

#ifdef PERF_HARDWARE_TIME
  float hw_time{0};
  CALL_CNRT_FUNC(cnrt::NotifierDuration(notifier_start_, notifier_end_, &hw_time), "Calculate elapsed time failed.");
  hw_time /= 1000.0f;
  VLOG(3) << "Inference hardware time " << hw_time << " ms";
#endif

  return Status::SUCCESS;
}

bool Model::Init(const string& model_file) noexcept {
  model_file_ = model_file;
  model_.reset(mm::CreateIModel());
  VLOG(3) << "(success) Load model from graph file: " << model_file_;
  MM_SAFECALL(model_->DeserializeFromFile(model_file_.c_str()), false);

  has_init_ = GetModelInfo();
  return has_init_;
}

bool Model::Init(void* mem_ptr, size_t size) noexcept {
  std::ostringstream ss;
  ss << mem_ptr;
  model_file_ = ss.str();
  model_.reset(mm::CreateIModel());

  VLOG(3) << "(success) Load model from memory: " << model_file_;
  MM_SAFECALL(model_->DeserializeFromMemory(mem_ptr, size), false);

  has_init_ = GetModelInfo();
  return has_init_;
}

bool Model::GetModelInfo() noexcept {
  // get IO messages
  // get io number and data size
  i_num_ = model_->GetInputNum();
  o_num_ = model_->GetOutputNum();
  std::vector<mm::Dims> in_dims = model_->GetInputDimensions();
  std::vector<mm::Dims> out_dims = model_->GetOutputDimensions();

  model_batch_size_ = in_dims[0].GetDimValue(0);

  // get io shapes
  auto dims2shape = [](const mm::Dims& d) { return Shape(d.GetDims()); };
  std::transform(in_dims.begin(), in_dims.end(), std::back_inserter(input_shapes_), dims2shape);
  std::transform(out_dims.begin(), out_dims.end(), std::back_inserter(output_shapes_), dims2shape);

  // get mlu io data type
  std::vector<mm::DataType> i_dtypes = model_->GetInputDataTypes();
  std::vector<mm::DataType> o_dtypes = model_->GetOutputDataTypes();
  auto dtype2layout = [](mm::DataType t) { return DataLayout{detail::CastDataType(t), DimOrder::NCHW}; };
  std::transform(i_dtypes.begin(), i_dtypes.end(), std::back_inserter(i_mlu_layouts_), dtype2layout);
  std::transform(o_dtypes.begin(), o_dtypes.end(), std::back_inserter(o_mlu_layouts_), dtype2layout);

  // since we can not get dimorder from model, deduce it from shape
  // FIXME(dmh): not robust
  for (int i_idx = 0; i_idx < i_num_; ++i_idx) {
    if (input_shapes_[i_idx].Size() == 4 && input_shapes_[i_idx][3] <= 4) {
      i_mlu_layouts_[i_idx].order = DimOrder::NHWC;
    }
  }

  VLOG(3) << "Model Info: input number = " << i_num_ << ";\toutput number = " << o_num_;
  for (int i = 0; i < i_num_; ++i) {
    VLOG(3) << "----- input index [" << i;
    VLOG(3) << "      data type " << detail::DataTypeStr(i_mlu_layouts_[i].dtype);
    VLOG(3) << "      dim order " << detail::DimOrderStr(i_mlu_layouts_[i].order);
    VLOG(3) << "      shape " << input_shapes_[i];
  }
  for (int i = 0; i < o_num_; ++i) {
    VLOG(3) << "----- output index [" << i;
    VLOG(3) << "      data type " << detail::DataTypeStr(o_mlu_layouts_[i].dtype);
    VLOG(3) << "      dim order " << detail::DimOrderStr(o_mlu_layouts_[i].order);
    VLOG(3) << "      shape " << output_shapes_[i];
  }
  return true;
}

Model::~Model() {
  VLOG(3) << "Unload model: " << model_file_;
}
}  // namespace infer_server

#endif  // CNIS_USE_MAGICMIND
