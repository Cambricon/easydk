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
#include <memory>
#include <vector>

#include "cnrt.h"
#include "core/data_type.h"
#include "device/mlu_context.h"

using std::shared_ptr;
using std::string;
using std::vector;

#define CHECK_CNRT_RET(ret, msg, val)              \
  do {                                             \
    if ((ret) != CNRT_RET_SUCCESS) {               \
      LOG(ERROR) << msg << " error code: " << ret; \
      return val;                                  \
    }                                              \
  } while (0)
namespace infer_server {

bool ModelRunner::Init(Model* model) noexcept {
  input_num_ = model->InputNum();
  output_num_ = model->OutputNum();
  cnrtRet_t ret = cnrtCreateRuntimeContext(&ctx_, model->GetFunction(), NULL);
  CHECK_CNRT_RET(ret, "Create runtime context failed!", false);

  cnrtChannelType_t channel = CNRT_CHANNEL_TYPE_NONE;
  ret = cnrtSetRuntimeContextChannel(ctx_, channel);
  CHECK_CNRT_RET(ret, "Set Runtime Context Channel failed!", false);
  ret = cnrtSetRuntimeContextDeviceId(ctx_, device_id_);
  CHECK_CNRT_RET(ret, "Set Runtime Context Device Id failed!", false);
  ret = cnrtInitRuntimeContext(ctx_, NULL);
  CHECK_CNRT_RET(ret, "Init runtime context failed!", false);

  VLOG(3) << "Create cnrt queue from runtime context";
  ret = cnrtRuntimeContextCreateQueue(ctx_, &task_queue_);
  CHECK_CNRT_RET(ret, "Runtime Context Create Queue failed", false);

  params_ = new void*[input_num_ + output_num_];
#ifdef PERF_HARDWARE_TIME
  // create notifier for hardware time
  ret = cnrtCreateNotifier(&notifier_start_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
  ret = cnrtCreateNotifier(&notifier_end_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
#endif
  return true;
}

bool ModelRunner::ForkFrom(const ModelRunner& other) noexcept {
  device_id_ = other.device_id_;
  input_num_ = other.input_num_;
  output_num_ = other.output_num_;
  cnrtRet_t ret = cnrtForkRuntimeContext(&ctx_, other.ctx_, nullptr);
  CHECK_CNRT_RET(ret, "fork cnrtRuntimeContext_t failed", false);

  VLOG(3) << "Create cnrt queue from runtime context";
  ret = cnrtRuntimeContextCreateQueue(ctx_, &task_queue_);
  CHECK_CNRT_RET(ret, "Runtime Context Create Queue failed", false);

  params_ = new void*[input_num_ + output_num_];
#ifdef PERF_HARDWARE_TIME
  // create notifier for hardware time
  ret = cnrtCreateNotifier(&notifier_start_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
  ret = cnrtCreateNotifier(&notifier_end_);
  CHECK_CNRT_RET(ret, "Create notifier failed", false);
#endif
  return true;
}

ModelRunner::~ModelRunner() {
  try {
    edk::MluContext ctx;
    ctx.SetDeviceId(device_id_);
    ctx.BindDevice();
    if (task_queue_) {
      cnrtDestroyQueue(task_queue_);
      task_queue_ = nullptr;
    }
    if (ctx_) {
      cnrtDestroyRuntimeContext(ctx_);
      ctx_ = nullptr;
    }
#ifdef PERF_HARDWARE_TIME
    if (notifier_start_) {
      cnrtDestroyNotifier(&notifier_start_);
      notifier_start_ = nullptr;
    }
    if (priv_->notifier_end) {
      cnrtDestroyNotifier(&notifier_end_);
      notifier_end_ = nullptr;
    }
#endif
    if (params_) {
      delete[] params_;
      params_ = nullptr;
    }
  } catch (edk::Exception& e) {
    LOG(ERROR) << e.what();
  }
}

#define CALL_CNRT_FUNC(func, msg)                    \
  do {                                               \
    cnrtRet_t ret = (func);                          \
    if (CNRT_RET_SUCCESS != ret) {                   \
      LOG(ERROR) << (msg) << " error code: " << ret; \
      return Status::ERROR_BACKEND;                  \
    }                                                \
  } while (0)

Status ModelRunner::Run(vector<Buffer>& input, vector<Buffer>& output) noexcept {  // NOLINT
  CHECK_EQ(input_num_, input.size());
  CHECK_EQ(output_num_, output.size());

  VLOG(6) << "Process inference once, input num: " << input_num_ << " output num: " << output_num_;
  // prepare params for invokefunction
  for (size_t i = 0; i < input_num_; ++i) {
    params_[i] = input[i].MutableData();
  }
  for (size_t i = 0; i < output_num_; ++i) {
    params_[input_num_ + i] = output[i].MutableData();
  }

#ifdef PERF_HARDWARE_TIME
  // place start event
  CALL_CNRT_FUNC(cnrtPlaceNotifier(notifier_start_, task_queue_), "Place event failed");
#endif

  CALL_CNRT_FUNC(cnrtInvokeRuntimeContext(ctx_, params_, task_queue_, NULL), "Invoke Runtime Context failed");

#ifdef PERF_HARDWARE_TIME
  // place end event
  CALL_CNRT_FUNC(cnrtPlaceNotifier(notifier_end_, task_queue_), "Place event failed");
#endif

  CALL_CNRT_FUNC(cnrtSyncQueue(task_queue_), "Sync queue failed.");

#ifdef PERF_HARDWARE_TIME
  float hw_time{0};
  CALL_CNRT_FUNC(cnrtNotifierDuration(notifier_start_, notifier_end_, &hw_time), "Calculate elapsed time failed.");
  hw_time /= 1000.0f;
  VLOG(3) << "Inference hardware time " << hw_time << " ms";
#endif

  return Status::SUCCESS;
}

bool Model::Init(const string& model_path, const string& func_name) noexcept {
  path_ = model_path;
  func_name_ = func_name;
  VLOG(3) << "Load model from file: " << model_path;
  cnrtRet_t error_code = cnrtLoadModel(&model_, model_path.c_str());
  CHECK_CNRT_RET(error_code, "Load model failed.", false);

  has_init_ = LoadFunction(func_name);
  return has_init_;
}

bool Model::Init(void* mem_ptr, const string& func_name) noexcept {
  func_name_ = func_name;
  std::ostringstream ss;
  ss << mem_ptr;
  path_ = ss.str();

  LOG(INFO) << "Load model from memory, " << mem_ptr;
  cnrtRet_t error_code = cnrtLoadModelFromMem(&model_, reinterpret_cast<char*>(mem_ptr));
  CHECK_CNRT_RET(error_code, "Load model from memory failed.", false);

  has_init_ = LoadFunction(func_name);
  return has_init_;
}

bool Model::LoadFunction(const string& func_name) noexcept {
  cnrtRet_t error_code;

  error_code = cnrtCreateFunction(&function_);
  CHECK_CNRT_RET(error_code, "Create function failed.", false);
  error_code = cnrtExtractFunction(&function_, model_, func_name.c_str());
  CHECK_CNRT_RET(error_code, "Extract function failed.", false);
  int model_parallelism;
  error_code = cnrtQueryModelParallelism(model_, &model_parallelism);
  CHECK_CNRT_RET(error_code, "Query Model Parallelism failed.", false);
  CHECK_GE(model_parallelism, 0) << "model parallelism is negative";
  model_batch_size_ = model_parallelism;

  LOG(INFO) << "Load function from offline model succeeded";
  return GetModelInfo();
}

bool Model::GetModelInfo() noexcept {
  // get IO messages
  // get io number and data size
  cnrtRet_t error_code;
  int64_t* input_sizes = nullptr;
  int input_num = 0;
  error_code = cnrtGetInputDataSize(&input_sizes, &input_num, function_);
  CHECK_CNRT_RET(error_code, "Get input data size failed.", false);
  i_num_ = input_num;
  i_data_sizes_ = std::vector<int64_t>(input_sizes, input_sizes + input_num);

  int64_t* output_sizes = nullptr;
  int output_num = 0;
  error_code = cnrtGetOutputDataSize(&output_sizes, &output_num, function_);
  CHECK_CNRT_RET(error_code, "Get output data size failed.", false);
  o_num_ = output_num;
  o_data_sizes_ = std::vector<int64_t>(output_sizes, output_sizes + output_num);

  // get io shapes
  int* input_dim_values = nullptr;
  int dim_num = 0;
  input_shapes_.clear();
  for (int i_idx = 0; i_idx < input_num; ++i_idx) {
    error_code = cnrtGetInputDataShape(&input_dim_values, &dim_num, i_idx, function_);
    CHECK_CNRT_RET(error_code, "Get input data size failed.", false);
    // nhwc shape
    input_shapes_.emplace_back(std::vector<int>(input_dim_values, input_dim_values + dim_num));
    free(input_dim_values);
  }

  int* output_dim_values = nullptr;
  output_shapes_.clear();
  for (int o_idx = 0; o_idx < output_num; ++o_idx) {
    error_code = cnrtGetOutputDataShape(&output_dim_values, &dim_num, o_idx, function_);
    CHECK_CNRT_RET(error_code, "Get output data shape failed.", false);
    // nhwc shape
    output_shapes_.emplace_back(std::vector<int>(output_dim_values, output_dim_values + dim_num));
    free(output_dim_values);
  }

  // get mlu io data type
  cnrtDataType_t* input_dtypes = nullptr;
  error_code = cnrtGetInputDataType(&input_dtypes, &input_num, function_);
  CHECK_CNRT_RET(error_code, "Get input data type failed.", false);
  CHECK_EQ(static_cast<size_t>(input_num), i_data_sizes_.size())
      << "Internal error, maybe input number from cnrtGetInputDataType is wrong.";
  i_mlu_layouts_.resize(i_num_);
  for (int i = 0; i < i_num_; ++i) {
    i_mlu_layouts_[i].dtype = detail::CastDataType(input_dtypes[i]);
    i_mlu_layouts_[i].order = DimOrder::NHWC;  // mlu data order is always NHWC
  }

  cnrtDataType_t* output_dtypes = nullptr;
  error_code = cnrtGetOutputDataType(&output_dtypes, &output_num, function_);
  CHECK_CNRT_RET(error_code, "Get output data type failed.", false);
  CHECK_EQ(static_cast<size_t>(output_num), o_data_sizes_.size())
      << "Internal error, maybe output number from cnrtGetOutputDataType is wrong.";
  o_mlu_layouts_.resize(o_num_);
  for (int i = 0; i < o_num_; ++i) {
    o_mlu_layouts_[i].dtype = detail::CastDataType(output_dtypes[i]);
    o_mlu_layouts_[i].order = DimOrder::NHWC;  // mlu data order is always NHWC
  }

  VLOG(5) << "Model Info: input number = " << i_num_ << ";\toutput number = " << o_num_;
  for (int i = 0; i < i_num_; ++i) {
    VLOG(5) << "----- input index [" << i;
    VLOG(5) << "      data type " << detail::DataTypeStr(i_mlu_layouts_[i].dtype);
    VLOG(5) << "      shape " << input_shapes_[i];
  }
  for (int i = 0; i < o_num_; ++i) {
    VLOG(5) << "----- output index [" << i;
    VLOG(5) << "      data type " << detail::DataTypeStr(o_mlu_layouts_[i].dtype);
    VLOG(5) << "      shape " << output_shapes_[i];
  }
  return true;
}

Model::~Model() {
  if (has_init_) {
    LOG(INFO) << "Destroy neural network function";
    cnrtRet_t error_code = cnrtDestroyFunction(function_);
    if (error_code != CNRT_RET_SUCCESS) {
      LOG(ERROR) << "Destroy function failed. error code: " << error_code;
    }

    LOG(INFO) << "Unload offline model";
    error_code = cnrtUnloadModel(model_);
    if (error_code != CNRT_RET_SUCCESS) {
      LOG(ERROR) << "Unload model failed. error code: " << error_code;
    }
  }
}

}  // namespace infer_server
