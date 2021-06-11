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

#include <map>
#include <memory>
#include <utility>

#include "buffer.h"
#include "core/data_type.h"
#include "easybang/resize_and_colorcvt.h"
#include "preprocess_impl.h"

namespace infer_server {
namespace video {

std::map<int, std::unique_ptr<Buffer>> detail::Scaler::buffers_map_;

struct PreprocessorMLUPrivate {
  std::unique_ptr<detail::PreprocessBase> executor{nullptr};
  std::unique_ptr<MluMemoryPool> pool{nullptr};
  Shape shape;
};

PreprocessorMLU::PreprocessorMLU() noexcept : ProcessorForkable("PreprocessorMLU"), priv_(new PreprocessorMLUPrivate) {}

PreprocessorMLU::~PreprocessorMLU() {
  delete priv_;
}

Status PreprocessorMLU::Init() noexcept {
  constexpr const char* params[] = {"model_info", "device_id", "preprocess_type", "dst_format"};
  for (auto p : params) {
    if (!HaveParam(p)) {
      LOG(ERROR) << p << " has not been set!";
      return Status::INVALID_PARAM;
    }
  }

  try {
    auto type = GetParam<PreprocessType>("preprocess_type");
    auto model = GetParam<ModelPtr>("model_info");
    auto dev_id = GetParam<int>("device_id");
    auto dst_fmt = GetParam<PixelFmt>("dst_format");

    int core_number = model->BatchSize();
    if (HaveParam("core_number")) {
      core_number = GetParam<int>("core_number");
    }
    bool keep_aspect_ratio = false;
    if (HaveParam("keep_aspect_ratio")) {
      keep_aspect_ratio = GetParam<bool>("keep_aspect_ratio");
    }

    edk::MluContext ctx;
    ctx.SetDeviceId(dev_id);
    ctx.BindDevice();

    if (type == PreprocessType::RESIZE_CONVERT) {
      priv_->executor.reset(
          new detail::ResizeConvert(model, dev_id, dst_fmt, ctx.GetCoreVersion(), core_number, keep_aspect_ratio));
    } else if (type == PreprocessType::SCALER) {
      priv_->executor.reset(new detail::Scaler(model->InputShape(0), dev_id, dst_fmt, keep_aspect_ratio));
#ifdef CNIS_HAVE_CNCV
    } else if (type == PreprocessType::CNCV_RESIZE_CONVERT) {
      priv_->executor.reset(new detail::CncvResizeConvert(model, dev_id, dst_fmt, keep_aspect_ratio));
#endif
    } else {
      LOG(ERROR) << "not support!";
      return Status::INVALID_PARAM;
    }

    const Shape& shape = model->InputShape(0);
    priv_->shape = shape;
    const DataLayout& layout = model->InputLayout(0);
    // FIXME(dmh): 3 buffer?
    priv_->pool.reset(new MluMemoryPool(shape.BatchDataCount() * GetTypeSize(layout.dtype), 3, dev_id));
  } catch (bad_any_cast&) {
    LOG(ERROR) << "Unmatched data type";
    return Status::WRONG_TYPE;
  } catch (edk::Exception& e) {
    LOG(ERROR) << e.what();
    return Status::ERROR_BACKEND;
  }
  return Status::SUCCESS;
}

Status PreprocessorMLU::Process(PackagePtr pack) noexcept {
  CHECK(pack);
  if (pack->data.empty()) {
    LOG(ERROR) << "no data in package";
    return Status::INVALID_PARAM;
  }
  auto preproc_output = priv_->pool->Request();
  bool ret = true;
  try {
    ret = priv_->executor->Execute(pack.get(), &preproc_output);
  } catch (bad_any_cast&) {
    LOG(ERROR) << "Unmatched data type";
    return Status::WRONG_TYPE;
  }

  // release input data
  for (auto& it : pack->data) {
    it->data.reset();
  }
  if (!ret) {
    LOG(ERROR) << "preprocess failed";
    return Status::ERROR_BACKEND;
  }
  ModelIO model_input;
  model_input.buffers.emplace_back(std::move(preproc_output));
  model_input.shapes.emplace_back(priv_->shape);
  pack->predict_io.reset(new InferData);
  pack->predict_io->Set(std::move(model_input));
  return Status::SUCCESS;
}

}  // namespace video
}  // namespace infer_server
