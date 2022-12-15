/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#ifndef PROCESSOR_PY_WRAPPER_HPP
#define PROCESSOR_PY_WRAPPER_HPP

#include <memory>
#include <vector>

#include "pybind11/pybind11.h"

#include "cnedk_buf_surface_util.hpp"
#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

class PyProcessor : public Processor {
 public:
  using Processor::Processor;
  Status Init() noexcept override {
    PYBIND11_OVERRIDE_PURE(
        Status,
        Processor,
        Init);
  }
  Status Process(PackagePtr data) noexcept override {
    PYBIND11_OVERRIDE_PURE(
        Status,
        Processor,
        Process,
        data);
  }
  std::shared_ptr<Processor> Fork() override {
    PYBIND11_OVERRIDE_PURE(
        std::shared_ptr<Processor>,
        Processor,
        Fork);
  }
};  // class PyProcessor

class PyIPreproc : public IPreproc {
 public:
  using IPreproc::IPreproc;
  int OnTensorParams(const CnPreprocTensorParams *params) override {
    PYBIND11_OVERRIDE_PURE(
        int,
        IPreproc,
        on_tensor_params,
        params);
  }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    PYBIND11_OVERRIDE_PURE(
        int,
        IPreproc,
        on_preproc,
        src,
        dst,
        src_rects);
  }
};  // class PyIPreproc

class PyIPostproc : public IPostproc {
 public:
  using IPostproc::IPostproc;
  int OnPostproc(const std::vector<InferData*>& data_vec, const ModelIO& model_output,
                 const ModelInfo* model_info) override {
    PYBIND11_OVERRIDE_PURE(
        int,
        IPostproc,
        on_postproc,
        data_vec,
        model_output,
        model_info);
  }
};  // class PyIPostproc

class DefaultIPreproc : public IPreproc {
 public:
  DefaultIPreproc() = default;
  int OnTensorParams(const CnPreprocTensorParams *params) { return 0; }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects);
};
}  //  namespace infer_server

#endif  // PROCESSOR_PY_WRAPPER_HPP
