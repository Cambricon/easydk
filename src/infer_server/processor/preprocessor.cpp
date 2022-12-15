/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
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

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cnrt.h"

#include "cnedk_buf_surface.h"
#include "cnis/processor.h"
#include "cnedk_platform.h"
#include "cnedk_transform.h"
#include "cnedk_buf_surface_util.hpp"
#include "core/data_type.h"
#include "../common/utils.hpp"

namespace infer_server {

static std::mutex gOnParamsMutex;
static std::set<std::string> gOnParamsSet;

static bool EnableOnTensorParams(const std::string &key) {
  std::unique_lock<std::mutex> lk(gOnParamsMutex);
  bool res = (gOnParamsSet.count(key) == 0);
  if (res == true) gOnParamsSet.insert(key);
  return res;
}

static std::mutex gPreprocMapMutex;
static std::map<std::string, IPreproc *> gPreprocMap;

void SetPreprocHandler(const std::string &key, IPreproc *handler) {
  std::unique_lock<std::mutex> lk(gPreprocMapMutex);
  gPreprocMap[key] = handler;
}

IPreproc *GetPreprocHandler(const std::string &key) {
  std::unique_lock<std::mutex> lk(gPreprocMapMutex);
  if (gPreprocMap.count(key)) {
    return gPreprocMap[key];
  }
  return nullptr;
}

void RemovePreprocHandler(const std::string &key) {
  {
    std::unique_lock<std::mutex> lk(gPreprocMapMutex);
    if (gPreprocMap.find(key) != gPreprocMap.end()) {
      gPreprocMap.erase(key);
    }
  }
  {
    std::unique_lock<std::mutex> lk(gOnParamsMutex);
    if (gOnParamsSet.count(key) != 0) {
      gOnParamsSet.erase(key);
    }
  }
}

class Solver {
 public:
  Solver(IPreproc *handler, int dev_id, const std::string &key, NetworkInputFormat model_input_format)
      : handler_(handler), dev_id_(dev_id), key_(key), model_input_format_(model_input_format) {}
  ~Solver() = default;

  int CheckAllocResource(const CnPreprocTensorParams &tensor_params) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (initialized_) return err_;
    if (!handler_) return -1;

    err_ = 0;
    tensor_params_ = tensor_params;
    if (EnableOnTensorParams(key_)) {
      if (handler_->OnTensorParams(&tensor_params_) < 0) {
        err_ = -1;
        return -1;
      }
    }
    err_ = CreatePool();
    if (err_ < 0) return -1;
    initialized_ = true;
    return 0;
  }

  int CreatePool();
  int Execute(Package *pack, cnedk::BufSurfWrapperPtr* output);

 private:
  cnedk::BufPool pool_;

 private:
  std::mutex mutex_;
  IPreproc *handler_ = nullptr;
  int dev_id_{0};
  std::string key_;
  NetworkInputFormat model_input_format_;
  int err_ = -1;
  CnPreprocTensorParams tensor_params_;
  bool initialized_ = false;
};  // class Solver

int Solver::Execute(Package *pack, cnedk::BufSurfWrapperPtr* output) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (err_ < 0) return -1;
  if (!handler_) return -1;
  if (pack->data.size() == 0) return 0;

  std::vector<CnedkBufSurfaceParams> surface_list(pack->data.size());
  CnedkBufSurface src_surf;
  cnedk::BufSurfWrapperPtr src_surf_wrapper = std::make_shared<cnedk::BufSurfaceWrapper>(&src_surf, false);
  memset(&src_surf, 0, sizeof(CnedkBufSurface));
  src_surf.batch_size = pack->data.size();
  src_surf.device_id = dev_id_;
  src_surf.is_contiguous = false;
  src_surf.num_filled = pack->data.size();
  src_surf.surface_list = surface_list.data();

  std::vector<CnedkTransformRect> src_rects;
  for (size_t batch_idx = 0; batch_idx < pack->data.size(); ++batch_idx) {
    PreprocInput input = pack->data[batch_idx]->GetLref<PreprocInput>();
    CnedkBufSurface *surf = input.surf->GetBufSurface();

    // the batch shares the same mem type
    src_surf.mem_type = surf->mem_type;
    surface_list[batch_idx] = surf->surface_list[0];

    if (!input.has_bbox) continue;
    CnedkTransformRect rect;
    rect.left = surf->surface_list->width * input.bbox.x;
    rect.top = surf->surface_list->height * input.bbox.y;
    rect.width = surf->surface_list->width * input.bbox.w;
    rect.height = surf->surface_list->height * input.bbox.h;
    src_rects.push_back(rect);
  }

  *output = pool_.GetBufSurfaceWrapper(2000);
  if (*output) {
    cnrtSetDevice(dev_id_);
    if (handler_->OnPreproc(src_surf_wrapper, *output, src_rects) < 0) {
      LOG(ERROR) << "[EasyDK InferServer] [Solver] Execute(): OnPreproc failed";
      return -1;
    }
    return 0;
  }
  LOG(ERROR) << "[EasyDK InferServer] [Solver] Execute(): Get BufSurface wrapper failed";
  return -1;
}

int Solver::CreatePool() {
  uint32_t model_input_w, model_input_h, model_input_c;
  if (tensor_params_.input_order == DimOrder::NHWC) {
    model_input_w = tensor_params_.input_shape[2];
    model_input_h = tensor_params_.input_shape[1];
    model_input_c = tensor_params_.input_shape[3];
  } else if (tensor_params_.input_order == DimOrder::NCHW) {
    model_input_w = tensor_params_.input_shape[3];
    model_input_h = tensor_params_.input_shape[2];
    model_input_c = tensor_params_.input_shape[1];
  } else {
    LOG(ERROR) << "[EasyDK InferServer] [Solver] CreatePool(): Unsupported input dim order";
    return -1;
  }

  if (model_input_format_ == NetworkInputFormat::BGR || model_input_format_ == NetworkInputFormat::RGB) {
    if (model_input_c != 3) {
      LOG(ERROR) << "[EasyDK InferServer] [Solver] CreatePool(): input c must be 3 for RGB/BGR";
      return -1;
    }
  }

  CnedkPlatformInfo platform_info;
  if (CnedkPlatformGetInfo(dev_id_, &platform_info) < 0) {
    LOG(ERROR) << "[EasyDK InferServer] [Solver] CreatePool(): Get platform information failed";
    return -1;
  }

  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  std::string platform_name(platform_info.name);
  if (cnedk::IsEdgePlatform(platform_name)) {
    create_params.mem_type = CNEDK_BUF_MEM_VB;
    if (model_input_w < 64 || model_input_h < 64) {
      create_params.mem_type = CNEDK_BUF_MEM_UNIFIED;
      VLOG(5) << "[EasyDK InferServer] [Solver] CreatePool(): preprocess memory type CNEDK_BUF_MEM_UNIFIED";
    }
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }
  create_params.force_align_1 = 1;  // to meet mm's requirement
  create_params.device_id = dev_id_;
  create_params.batch_size = tensor_params_.batch_num;
  switch (model_input_format_) {
    case NetworkInputFormat::BGR: {
      create_params.width = model_input_w;
      create_params.height = model_input_h;
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
      break;
    }
    case NetworkInputFormat::RGB: {
      create_params.width = model_input_w;
      create_params.height = model_input_h;
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_RGB;
      break;
    }
    case NetworkInputFormat::GRAY: {
      create_params.width = model_input_w;
      create_params.height = model_input_h;
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_GRAY8;
      break;
    }
    case NetworkInputFormat::TENSOR: {
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
      create_params.size = model_input_w * model_input_h * model_input_c;
      create_params.width = model_input_w;
      create_params.height = model_input_h;
      if (tensor_params_.input_dtype == DataType::UINT8) {
        create_params.size = create_params.size;
      } else if (tensor_params_.input_dtype == DataType::INT16) {
        create_params.size *= 2;
      } else if (tensor_params_.input_dtype == DataType::INT32) {
        create_params.size *= 4;
      } else if (tensor_params_.input_dtype == DataType::FLOAT16) {
        create_params.size *= 2;
      } else if (tensor_params_.input_dtype == DataType::FLOAT32) {
        create_params.size *= 4;
      } else {
        LOG(ERROR) << "[EasyDK InferServer] [Solver] CreatePool(): invalid input data type";
        return -1;
      }
      break;
    }
    default: {
      LOG(ERROR) << "[EasyDK InferServer] [Solver] CreatePool(): invalid model input pixel format";
      return -1;
    }
  }
  if (pool_.CreatePool(&create_params, 3) < 0) {
    return -1;
  }
  return 0;
}

class PreprocImpl {
 public:
  int dev_id;
  IPreproc *handler = nullptr;
  ModelPtr model;
  NetworkInputFormat model_input_format;
  std::unique_ptr<Solver> executor{nullptr};
  CnPreprocTensorParams tensor_params;
  CnedkPlatformInfo platform_info;

 public:
  int GetTensorParams() {
    auto input_shape = model->InputShape(0);
    DimOrder order = model->InputLayout(0).order;
    DataType dtype = model->InputLayout(0).dtype;

    // FIXME
    switch (order) {
      case DimOrder::NHWC:
        tensor_params.input_order = DimOrder::NHWC;
        break;
      case DimOrder::NCHW:
        tensor_params.input_order = DimOrder::NCHW;
        break;
      case DimOrder::HWCN:
        tensor_params.input_order = DimOrder::HWCN;
        break;
      case DimOrder::NTC:
        tensor_params.input_order = DimOrder::NTC;
        break;
      case DimOrder::TNC:
        tensor_params.input_order = DimOrder::TNC;
        break;
      case DimOrder::NONE:
#if MM_MAJOR_VERSION <= 0 && MM_MINOR_VERSION < 14
        tensor_params.input_order = DimOrder::NONE;
#else
        tensor_params.input_order = DimOrder::ARRAY;
#endif
        break;
      case DimOrder::ARRAY:
        tensor_params.input_order = DimOrder::ARRAY;
        break;
      default:
        tensor_params.input_order = DimOrder::CUSTOM;
        break;
    }

    for (size_t i = 0; i < input_shape.Size(); i++) {
      tensor_params.input_shape.push_back(input_shape[i]);
    }

    switch (dtype) {
      case DataType::UINT8:
        tensor_params.input_dtype = DataType::UINT8;
        break;
      case DataType::INT16:
        tensor_params.input_dtype = DataType::INT16;
        break;
      case DataType::INT32:
        tensor_params.input_dtype = DataType::INT32;
        break;
      case DataType::FLOAT16:
        tensor_params.input_dtype = DataType::FLOAT16;
        break;
      case DataType::FLOAT32:
        tensor_params.input_dtype = DataType::FLOAT32;
        break;
      default:
        tensor_params.input_dtype = DataType::INVALID;
        break;
    }

    tensor_params.input_format = model_input_format;
    tensor_params.batch_num = model->BatchSize();
    return 0;
  }
};

Preprocessor::Preprocessor() noexcept : ProcessorForkable("InferPreprocessor"), impl_(new PreprocImpl) {}

Preprocessor::~Preprocessor() {
  if (impl_) delete impl_, impl_ = nullptr;
}

Status Preprocessor::Init() noexcept {
  constexpr const char *params[] = {"model_info", "device_id", "model_input_format"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << "[EasyDK InferServer] [Preprocessor] Init(): " << p << " has not been set!";
      return Status::INVALID_PARAM;
    }
  }

  try {
    impl_->model = GetParam<ModelPtr>("model_info");
    impl_->dev_id = GetParam<int>("device_id");
    impl_->model_input_format = GetParam<NetworkInputFormat>("model_input_format");
    if (CnedkPlatformGetInfo(impl_->dev_id, &impl_->platform_info) < 0) {
      return Status::INVALID_PARAM;
    }
    if (impl_->GetTensorParams() < 0) {
      return Status::INVALID_PARAM;
    }
    impl_->handler = GetPreprocHandler(impl_->model->GetKey());
    impl_->executor.reset(new Solver(impl_->handler, impl_->dev_id, impl_->model->GetKey(), impl_->model_input_format));
  } catch (infer_server::bad_any_cast &) {
    LOG(ERROR) << "[EasyDK InferServer] [Preprocessor] Init(): Unmatched data type or create executor failed.";
    return Status::WRONG_TYPE;
  }
  return Status::SUCCESS;
}

Status Preprocessor::Process(PackagePtr pack) noexcept {
  if (pack->data.empty()) {
    LOG(ERROR) << "[EasyDK InferServer] [Preprocessor] Process(): No data in package";
    return Status::INVALID_PARAM;
  }
  cnrtSetDevice(impl_->dev_id);
  if (impl_->executor->CheckAllocResource(impl_->tensor_params) < 0) {
    return Status::ERROR_BACKEND;
  }
  cnedk::BufSurfWrapperPtr preproc_output = nullptr;
  int ret = 0;
  try {
    ret = impl_->executor->Execute(pack.get(), &preproc_output);
  } catch (infer_server::bad_any_cast &) {
    LOG(ERROR) << "[EasyDK InferServer] [Preprocessor] Process(): Preprocess error thrown";
    return Status::WRONG_TYPE;
  }
  // release input data
  for (auto &it : pack->data) {
    it->data.reset();
  }
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK InferServer] [Preprocessor] Process(): preprocess failed";
    return Status::ERROR_BACKEND;
  }

  ModelIO model_input;
  model_input.surfs.emplace_back(preproc_output);
  model_input.shapes.emplace_back(impl_->model->InputShape(0));
  pack->predict_io.reset(new InferData);
  pack->predict_io->Set(std::move(model_input));
  return Status::SUCCESS;
}

}  // namespace infer_server
