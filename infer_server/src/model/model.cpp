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
#include <utility>
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

bool ModelRunner::Init(MModel* model, mm_unique_ptr<MContext> ctx, const std::vector<Shape>& input_shape) noexcept {
  input_num_ = model->GetInputNum();
  output_num_ = model->GetOutputNum();
  ctx_ = std::move(ctx);

  i_shapes_.resize(input_num_);
  o_shapes_.resize(output_num_);
  i_layouts_.resize(input_num_);
  o_layouts_.resize(output_num_);

  MM_SAFECALL(mm::CreateInputTensors(ctx_.get(), &inputs_), false);
  MM_SAFECALL(mm::CreateOutputTensors(ctx_.get(), &outputs_), false);

  auto dims2shape = [](const mm::Dims& d) { return Shape(d.GetDims()); };
  std::vector<mm::Dims> in_dims = model->GetInputDimensions();
  std::vector<Shape> in_shapes;
  std::transform(in_dims.begin(), in_dims.end(), std::back_inserter(in_shapes), dims2shape);
  if (!FixedShape(in_shapes)) {
    fixed_input_shape_ = false;
    if (!input_shape.empty() && input_shape.size() == in_shapes.size()) {
      VLOG(3) << "[ModelRunner] Model with mutable input shape. Input shape is set by user.";
      for (unsigned idx = 0; idx < input_shape.size(); idx++) {
        in_shapes[idx] = input_shape[idx];
      }
    }
  }
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
  if (!FixedShape(input)) return {};
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
    if (!fixed_input_shape_) {
      if (in->shapes.size() > i_idx && FixedShape({in->shapes[i_idx]})) {
        inputs_[i_idx]->SetDimensions(mm::Dims(in->shapes[i_idx].Vectorize()));
      } else if (i_shapes_.size() > i_idx && FixedShape({i_shapes_[i_idx]})) {
        inputs_[i_idx]->SetDimensions(mm::Dims(i_shapes_[i_idx].Vectorize()));
      } else {
        LOG(ERROR) << "[ModelRunner] Can not get valid shape of the input tensor.";
        return Status::ERROR_BACKEND;
      }
    }
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

bool Model::Init(const string& model_file, const std::vector<Shape>& in_shape) noexcept {
  model_file_ = model_file;
  model_.reset(mm::CreateIModel());
  VLOG(3) << "(success) Load model from graph file: " << model_file_;
  MM_SAFECALL(model_->DeserializeFromFile(model_file_.c_str()), false);

  has_init_ = GetModelInfo(in_shape);
  return has_init_;
}

bool Model::Init(void* mem_ptr, size_t size, const std::vector<Shape>& in_shape) noexcept {
  std::ostringstream ss;
  ss << mem_ptr;
  model_file_ = ss.str();
  model_.reset(mm::CreateIModel());

  VLOG(3) << "(success) Load model from memory: " << model_file_;
  MM_SAFECALL(model_->DeserializeFromMemory(mem_ptr, size), false);

  has_init_ = GetModelInfo(in_shape);
  return has_init_;
}

bool Model::GetModelInfo(const std::vector<Shape>& in_shape) noexcept {
  // get IO messages
  // get io number and data size
  i_num_ = model_->GetInputNum();
  o_num_ = model_->GetOutputNum();

  // get mlu io data type
  std::vector<mm::DataType> i_dtypes = model_->GetInputDataTypes();
  std::vector<mm::DataType> o_dtypes = model_->GetOutputDataTypes();

  // get mlu io dim order
  // FIXME(gaoyujia) : hard code get engine on device 0
  MEngine* engine = GetEngine(0);
  MContext* ctx = engine->CreateIContext();
  std::vector<MTensor*> inputs, outputs;
  MM_SAFECALL(mm::CreateInputTensors(ctx, &inputs), false);
  MM_SAFECALL(mm::CreateOutputTensors(ctx, &outputs), false);
  auto trans2layout = [](mm::DataType t, mm::Layout l) {
    DimOrder order = detail::CastDimOrder(l);
    if (order == DimOrder::INVALID) {
      LOG(WARNING) << "DimOrder is invalid, use NHWC instead";
       order = DimOrder::NHWC;
    }
    return DataLayout{detail::CastDataType(t), order};
  };
  i_mlu_layouts_.reserve(i_num_);
  o_mlu_layouts_.reserve(o_num_);
  for (int idx = 0; idx < i_num_; ++idx) {
    i_mlu_layouts_.push_back(trans2layout(i_dtypes[idx], inputs[idx]->GetLayout()));
  }
  for (int idx = 0; idx < o_num_; ++idx) {
    o_mlu_layouts_.push_back(trans2layout(o_dtypes[idx], outputs[idx]->GetLayout()));
  }

  std::vector<mm::Dims> in_dims = model_->GetInputDimensions();
  std::vector<mm::Dims> out_dims = model_->GetOutputDimensions();

  auto dims2shape = [](const mm::Dims& d) { return Shape(d.GetDims()); };
  std::transform(in_dims.begin(), in_dims.end(), std::back_inserter(input_shapes_), dims2shape);
  std::transform(out_dims.begin(), out_dims.end(), std::back_inserter(output_shapes_), dims2shape);

  if (!FixedShape(input_shapes_)) {
    if (!in_shape.empty() && in_shape.size() == input_shapes_.size()) {
      VLOG(3) << "Model with mutable input shape. Input shape is set by user.";
      for (unsigned idx = 0; idx < in_shape.size(); idx++) {
        input_shapes_[idx] = in_shape[idx];
        inputs[idx]->SetDimensions(mm::Dims(in_shape[idx].Vectorize()));
      }
      MM_SAFECALL(ctx->InferOutputShape(inputs, outputs), {});
      for (uint32_t idx = 0; idx < outputs.size(); ++idx) {
        output_shapes_[idx] = outputs[idx]->GetDimensions().GetDims();
      }
    } else {
      VLOG(3) << "Model with mutable input shape. Input shape is not set.";
    }
  }

  // Destroy mm tensor and context
  for (auto& it : inputs) { it->Destroy(); }
  for (auto& it : outputs) { it->Destroy(); }
  ctx->Destroy();

  switch (i_mlu_layouts_[0].order) {
    case DimOrder::NCHW:
    case DimOrder::NHWC:
    case DimOrder::HWCN:
      if (input_shapes_[0].Size() != 4) {
        LOG(ERROR) << "Input shape and dim order is unmatched.";
        return false;
      }
      break;
    case DimOrder::NTC:
    case DimOrder::TNC:
      if (input_shapes_[0].Size() != 3) {
        LOG(ERROR) << "Input shape and dim order is unmatched.";
        return false;
      }
      break;
    default:
      break;
  }
  switch (i_mlu_layouts_[0].order) {
    case DimOrder::NCHW:
    case DimOrder::NHWC:
    case DimOrder::NTC:
      model_batch_size_ = input_shapes_[0][0];
      break;
    case DimOrder::TNC:
      model_batch_size_ = input_shapes_[0][1];
      break;
    case DimOrder::HWCN:
      model_batch_size_ = input_shapes_[0][3];
      break;
    default:
      model_batch_size_ = input_shapes_[0][0];
      break;
  }

  VLOG(3) << "Model Info: input number = " << i_num_ << ";\toutput number = " << o_num_;
  VLOG(3) << "            batch size = " << model_batch_size_;
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
