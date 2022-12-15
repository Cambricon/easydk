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
#include "../../common/utils.hpp"

using std::string;
using std::vector;

namespace infer_server {

#define MM_SAFECALL(func, val)                                                    \
  do {                                                                            \
    auto ret = (func);                                                            \
    if (!ret.ok()) {                                                              \
      LOG(ERROR) << "[EasyDK InferServer] Call " #func " failed, ret = " << ret;  \
      return val;                                                                 \
    }                                                                             \
  } while (0)

bool ModelRunner::Init(MModel* model, mm_unique_ptr<MContext> ctx, const std::vector<Shape>& input_shape) noexcept {
  input_num_ = model->GetInputNum();
  output_num_ = model->GetOutputNum();
  ctx_ = std::move(ctx);

  i_shapes_.resize(input_num_);
  o_shapes_.resize(output_num_);
  i_layouts_.resize(input_num_);
  o_layouts_.resize(output_num_);

#if MM_MAJOR_VERSION <= 0 && MM_MINOR_VERSION < 12
  MM_SAFECALL(mm::CreateInputTensors(ctx_.get(), &inputs_), false);
  MM_SAFECALL(mm::CreateOutputTensors(ctx_.get(), &outputs_), false);
#else
  MM_SAFECALL(ctx_->CreateInputTensors(&inputs_), false);
  MM_SAFECALL(ctx_->CreateOutputTensors(&outputs_), false);
#endif

  auto dims2shape = [](const mm::Dims& d) { return Shape(d.GetDims()); };
  std::vector<mm::Dims> in_dims = model->GetInputDimensions();
  std::vector<Shape> in_shapes;
  std::transform(in_dims.begin(), in_dims.end(), std::back_inserter(in_shapes), dims2shape);
  if (!FixedShape(in_shapes)) {
    fixed_input_shape_ = false;
    if (!input_shape.empty() && input_shape.size() == in_shapes.size()) {
      VLOG(1) << "[EasyDK InferServer] [ModelRunner] Model with mutable input shape. Input shape is set by user.";
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
    fixed_output_shape_ = false;
  }

  VLOG(1) << "[EasyDK InferServer] [ModelRunner] Create CNRT queue";
  CNRT_SAFECALL(cnrtQueueCreate(&task_queue_), "[InferServer] [ModelRunner] Create Queue failed", false);
#ifdef PERF_HARDWARE_TIME
  // create notifier for hardware time
  CNRT_SAFECALL(recnrtNotifierCreate(&notifier_start_), "[InferServer] [ModelRunner] Create notifier failed", false);
  CNRT_SAFECALL(cnrtNotifierCreate(&notifier_end_), "[InferServer] [ModelRunner] Create notifier failed", false);
#endif
  return true;
}

ModelRunner::~ModelRunner() {
  SetCurrentDevice(device_id_);
  if (task_queue_) {
    cnrtQueueDestroy(task_queue_);
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
    cnrtNotifierDestroy(&notifier_start_);
    notifier_start_ = nullptr;
  }
  if (notifier_end_) {
    cnrtNotifierDestroy(&notifier_end_);
    notifier_end_ = nullptr;
  }
#endif
}

class TensorDeleter : public cnedk::IBufDeleter {
 public:
  explicit TensorDeleter(MTensor* tensor) : tensor_(tensor) {}
  virtual ~TensorDeleter() {
    if (tensor_) {
      tensor_->Destroy();
    }
  }

 private:
  MTensor* tensor_ = nullptr;
};
std::vector<Shape> ModelRunner::InferOutputShape(const std::vector<Shape>& input) noexcept {
  if (!fixed_output_shape_) return {};
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
  // MM_SAFECALL(ctx_->InferOutputShape(inputs_, outputs_), {});
  auto ret = ctx_->InferOutputShape(inputs_, outputs_);
  if (!ret.ok()) outputs_.clear();
  for (uint32_t idx = 0; idx < outputs_.size(); ++idx) {
    o_shapes_[idx] = outputs_[idx]->GetDimensions().GetDims();
  }
  i_shapes_ = input;
  VLOG(2) << "[EasyDK InferServer] [ModelRunner] inference shape changed ---"
          << "\n\tinput: " << i_shapes_ << "\n\toutput: " << o_shapes_;
  if (outputs_.empty()) return {};
  return o_shapes_;
}

Status ModelRunner::Run(ModelIO* in, ModelIO* out) noexcept {  // NOLINT
  auto& input = in->surfs;
  auto& output = out->surfs;
  CHECK_EQ(input_num_, input.size()) << "EasyDK InferServer] [ModelRunner] Input number is mismatched";

  VLOG(5) << "[EasyDK InferServer] [ModelRunner] Process inference once, input num: " << input_num_ << " output num: "
          << output_num_;

  for (uint32_t i_idx = 0; i_idx < input_num_; ++i_idx) {
    inputs_[i_idx]->SetData(input[i_idx]->GetData(0));
    if (!fixed_input_shape_) {
      if (in->shapes.size() > i_idx && FixedShape({in->shapes[i_idx]})) {
        inputs_[i_idx]->SetDimensions(mm::Dims(in->shapes[i_idx].Vectorize()));
      } else if (i_shapes_.size() > i_idx && FixedShape({i_shapes_[i_idx]})) {
        inputs_[i_idx]->SetDimensions(mm::Dims(i_shapes_[i_idx].Vectorize()));
      } else {
        LOG(ERROR) << "[EasyDK InferServer] [ModelRunner] Can not get valid shape of the input tensor.";
        return Status::ERROR_BACKEND;
      }
    }
  }
  if (!output.empty()) {
    CHECK_EQ(output_num_, output.size()) << "[EasyDK InferServer] [ModelRunner] Output number is mismatched";
    for (uint32_t o_idx = 0; o_idx < output_num_; ++o_idx) {
      outputs_[o_idx]->SetData(output[o_idx]->GetData(0));
    }
  }

#ifdef PERF_HARDWARE_TIME
  // place start event
  CNRT_SAFECALL(cnrtPlaceNotifier(notifier_start_, task_queue_),
                "[InferServer] [ModelRunner] Place event failed", Status::ERROR_BACKEND);
#endif

  if (output.empty()) {
    // buffer is managed by magicmind, pass empty vector as output
    MM_SAFECALL(ctx_->Enqueue(inputs_, &outputs_, task_queue_), Status::ERROR_BACKEND);
  } else {
    MM_SAFECALL(ctx_->Enqueue(inputs_, outputs_, task_queue_), Status::ERROR_BACKEND);
  }

#ifdef PERF_HARDWARE_TIME
  // place end event
  CNRT_SAFECALL(cnrtPlaceNotifier(notifier_end_, task_queue_),
                "[InferServer] [ModelRunner] Place event failed", Status::ERROR_BACKEND);
#endif

  CNRT_SAFECALL(cnrtQueueSync(task_queue_), "[InferServer] [ModelRunner] Sync queue failed.", Status::ERROR_BACKEND);

  if (output.empty()) {
    for (auto& tensor : outputs_) {
      cnedk::IBufDeleter* deleter = new TensorDeleter(tensor);
      CnedkBufSurfaceMemType mem_type;
      switch (tensor->GetMemoryLocation()) {
        case magicmind::TensorLocation::kHost:
          mem_type = CNEDK_BUF_MEM_SYSTEM;
          break;
        case magicmind::TensorLocation::kMLU:
          mem_type = CNEDK_BUF_MEM_DEVICE;
          break;
        default:
          return Status::ERROR_BACKEND;
      }
      auto surfPtr = std::make_shared<cnedk::BufSurfaceWrapper>(tensor->GetMutableData(), tensor->GetSize(), mem_type,
                                                                device_id_, deleter);
      output.emplace_back(surfPtr);
      out->shapes.emplace_back(tensor->GetDimensions().GetDims());
    }
    outputs_.clear();
  }
#ifdef PERF_HARDWARE_TIME
  float hw_time{0};
  CNRT_SAFECALL(cnrt::NotifierDuration(notifier_start_, notifier_end_, &hw_time),
                "[InferServer] [ModelRunner] Calculate elapsed time failed.", Status::ERROR_BACKEND);
  hw_time /= 1000.0f;
  VLOG(1) << "[EasyDK InferServer] [ModelRunner] Inference hardware time " << hw_time << " ms";
#endif

  return Status::SUCCESS;
}

bool Model::Init(const string& model_file, const std::vector<Shape>& in_shape) noexcept {
  model_file_ = model_file;
  model_.reset(mm::CreateIModel());
  VLOG(1) << "[EasyDK InferServer] [Model] (success) Load model from graph file: " << model_file_;
  MM_SAFECALL(model_->DeserializeFromFile(model_file_.c_str()), false);

  has_init_ = GetModelInfo(in_shape);
  return has_init_;
}

bool Model::Init(void* mem_ptr, size_t size, const std::vector<Shape>& in_shape) noexcept {
  std::ostringstream ss;
  ss << mem_ptr;
  model_file_ = ss.str();
  model_.reset(mm::CreateIModel());

  VLOG(1) << "[EasyDK InferServer] [Model] (success) Load model from memory: " << model_file_;
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
  // get engine on device binded to this thread. If no device is binded, get engine on dev 0.
  int dev_id = 0;
  CNRT_SAFECALL(cnrtGetDevice(&dev_id), "[InferServer] [Model] Get device id failed", false);
  MEngine* engine = GetEngine(dev_id);
  MContext* ctx = engine->CreateIContext();
  std::vector<MTensor*> inputs, outputs;
#if MM_MAJOR_VERSION <= 0 && MM_MINOR_VERSION < 12
  MM_SAFECALL(mm::CreateInputTensors(ctx, &inputs), false);
  MM_SAFECALL(mm::CreateOutputTensors(ctx, &outputs), false);
#else
  MM_SAFECALL(ctx->CreateInputTensors(&inputs), false);
  MM_SAFECALL(ctx->CreateOutputTensors(&outputs), false);
#endif
  auto trans2layout = [](mm::DataType t, mm::Layout l) {
    DimOrder order = detail::CastDimOrder(l);
    if (order == DimOrder::INVALID) {
      LOG(WARNING) << "[EasyDK InferServer] [Model] DimOrder is invalid, use NHWC instead";
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
      VLOG(1) << "[EasyDK InferServer] [Model] Model with mutable input shape. Input shape is set by user.";
      for (unsigned idx = 0; idx < in_shape.size(); idx++) {
        input_shapes_[idx] = in_shape[idx];
        inputs[idx]->SetDimensions(mm::Dims(in_shape[idx].Vectorize()));
      }
      // MM_SAFECALL(ctx->InferOutputShape(inputs, outputs), {});
      auto ret = ctx->InferOutputShape(inputs, outputs);
      if (!ret.ok()) outputs.clear();
      for (uint32_t idx = 0; idx < outputs.size(); ++idx) {
        output_shapes_[idx] = outputs[idx]->GetDimensions().GetDims();
      }
    } else {
      VLOG(1) << "[EasyDK InferServer] [Model] Model with mutable input shape. Input shape is not set.";
    }
  }

  // Destroy mm tensor and context
  for (auto& it : inputs) {
    it->Destroy();
  }
  for (auto& it : outputs) {
    it->Destroy();
  }
  ctx->Destroy();

  switch (i_mlu_layouts_[0].order) {
    case DimOrder::NCHW:
    case DimOrder::NHWC:
    case DimOrder::HWCN:
      if (input_shapes_[0].Size() != 4) {
        LOG(ERROR) << "[EasyDK InferServer] [Model] Input shape and dim order is unmatched.";
        return false;
      }
      break;
    case DimOrder::NTC:
    case DimOrder::TNC:
      if (input_shapes_[0].Size() != 3) {
        LOG(ERROR) << "[EasyDK InferServer] [Model] Input shape and dim order is unmatched.";
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

  VLOG(1) << "[EasyDK InferServer] [Model] Model Info: input number = " << i_num_ << ";\toutput number = " << o_num_;
  VLOG(1) << "[EasyDK InferServer] [Model]             batch size = " << model_batch_size_;
  for (int i = 0; i < i_num_; ++i) {
    VLOG(1) << "[EasyDK InferServer] [Model] ----- input index [" << i;
    VLOG(1) << "[EasyDK InferServer] [Model]       data type " << detail::DataTypeStr(i_mlu_layouts_[i].dtype);
    VLOG(1) << "[EasyDK InferServer] [Model]       dim order " << detail::DimOrderStr(i_mlu_layouts_[i].order);
    VLOG(1) << "[EasyDK InferServer] [Model]       shape " << input_shapes_[i];
  }
  for (int i = 0; i < o_num_; ++i) {
    VLOG(1) << "[EasyDK InferServer] [Model] ----- output index [" << i;
    VLOG(1) << "[EasyDK InferServer] [Model]       data type " << detail::DataTypeStr(o_mlu_layouts_[i].dtype);
    VLOG(1) << "[EasyDK InferServer] [Model]       dim order " << detail::DimOrderStr(o_mlu_layouts_[i].order);
    VLOG(1) << "[EasyDK InferServer] [Model]       shape " << output_shapes_[i];
  }
  return true;
}

Model::~Model() {
  VLOG(1) << "[EasyDK InferServer] [Model] Unload model: " << model_file_;
}
}  // namespace infer_server
