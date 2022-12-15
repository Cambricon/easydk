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
#include "processor_py_wrapper.hpp"

#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "pybind11/complex.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "pybind11/stl_bind.h"

#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

class PyPreprocessor : public Preprocessor {
 public:
  using Preprocessor::Preprocessor;
  Status Init() noexcept override {
    PYBIND11_OVERRIDE(Status,
                      Preprocessor,
                      Init);
  }
  Status Process(PackagePtr data) noexcept override {
    PYBIND11_OVERRIDE(Status,
                      Preprocessor,
                      Process,
                      data);
  }
};  // class PyPreprocessor

class PyPostprocessor : public Postprocessor {
 public:
  using Postprocessor::Postprocessor;
  Status Init() noexcept override {
    PYBIND11_OVERRIDE(Status,
                      Postprocessor,
                      Init);
  }
  Status Process(PackagePtr data) noexcept override {
    PYBIND11_OVERRIDE(Status,
                      Postprocessor,
                      Process,
                      data);
  }
};  // class PyPostprocessor

int DefaultIPreproc::OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                               const std::vector<CnedkTransformRect> &src_rects) {
  CnedkBufSurface* src_buf = src->GetBufSurface();
  CnedkBufSurface* dst_buf = dst->GetBufSurface();
  uint32_t batch_size = src->GetNumFilled();
  for (uint32_t i = 0; i < batch_size; i++) {
    if (src_buf->surface_list[i].data_size != dst_buf->surface_list[i].data_size) {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPI] Model input data size is unmatched with input data.";
      return -1;
    }
    if (cnrtMemcpy(dst_buf->surface_list[i].data_ptr, src_buf->surface_list[i].data_ptr,
                   src_buf->surface_list[i].data_size, CNRT_MEM_TRANS_DIR_HOST2DEV) != cnrtSuccess) {
      LOG(ERROR) << "[EasyDK InferServer] [PythonAPI] cnrtMemcpy input to model input failed.";
      return -1;
    }
  }
  return 0;
}

void ProcessorWrapper(py::module *m) {
  py::class_<Processor, PyProcessor, std::shared_ptr<Processor>>(*m, "Processor")
      .def(py::init<const std::string &>())
      .def("type_name", &Processor::TypeName);

  // Preprocessor
  py::class_<Preprocessor, PyPreprocessor, Processor, std::shared_ptr<Preprocessor>>(*m, "Preprocessor")
      .def(py::init<>());

  // Postprocessor
  py::class_<Postprocessor, Processor, PyPostprocessor, std::shared_ptr<Postprocessor>>(*m, "Postprocessor")
      .def(py::init<>());

  py::class_<CnPreprocTensorParams>(*m, "CnPreprocTensorParams")
      .def_readwrite("input_order", &CnPreprocTensorParams::input_order)
      .def_readwrite("input_shape", &CnPreprocTensorParams::input_shape)
      .def_readwrite("input_format", &CnPreprocTensorParams::input_format)
      .def_readwrite("input_dtype", &CnPreprocTensorParams::input_dtype)
      .def_readwrite("batch_num", &CnPreprocTensorParams::batch_num);

  // IPreproc
  py::class_<IPreproc, PyIPreproc, std::shared_ptr<IPreproc>>(*m, "IPreproc")
      .def(py::init<>())
      .def("on_tensor_params", &IPreproc::OnTensorParams)
      .def("on_preproc", &IPreproc::OnPreproc);

  // IPostproc
  py::class_<IPostproc, PyIPostproc, std::shared_ptr<IPostproc>>(*m, "IPostproc")
      .def(py::init<>())
      .def("on_postproc", &IPostproc::OnPostproc);

  m->def("set_preproc_handler",
      [](const std::string key, std::shared_ptr<IPreproc> preproc) {
        SetPreprocHandler(key, preproc.get());
      });
  m->def("set_postproc_handler",
      [](const std::string key, std::shared_ptr<IPostproc> postproc) {
        SetPostprocHandler(key, postproc.get());
      });
  m->def("remove_preproc_handler", &RemovePreprocHandler);
  m->def("remove_postproc_handler", &RemovePostprocHandler);
}

}  //  namespace infer_server
