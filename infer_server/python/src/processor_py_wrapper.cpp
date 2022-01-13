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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>

#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

class PyPreprocessorHost : public PreprocessorHost {
 public:
  using PreprocessorHost::PreprocessorHost;
  Status Init() noexcept override { PYBIND11_OVERRIDE(Status, PreprocessorHost, Init); }
  Status Process(PackagePtr data) noexcept override { PYBIND11_OVERRIDE(Status, PreprocessorHost, Process, data); }
};  // class PyPreprocessorHost

class PyPostprocessor : public Postprocessor {
 public:
  using Postprocessor::Postprocessor;
  Status Init() noexcept override { PYBIND11_OVERRIDE(Status, Postprocessor, Init); }
  Status Process(PackagePtr data) noexcept override { PYBIND11_OVERRIDE(Status, Postprocessor, Process, data); }
};  // class PyPostprocessor

void ProcessorWrapper(const py::module &m) {
  py::class_<Processor, PyProcessor, std::shared_ptr<Processor>>(m, "Processor")
      .def(py::init<const std::string &>())
      .def("type_name", &Processor::TypeName);

  // PreprocessorHost
  py::class_<PreprocessorHost, PyPreprocessorHost, Processor, std::shared_ptr<PreprocessorHost>>(m, "PreprocessorHost")
      .def(py::init<>());

  // Postprocessor
  py::class_<Postprocessor, Processor, PyPostprocessor, std::shared_ptr<Postprocessor>>(m, "Postprocessor")
      .def(py::init<>());

  py::class_<ModelIO, std::shared_ptr<ModelIO>>(m, "ModelIO")
      .def(py::init<>())
      .def_readwrite("buffers", &ModelIO::buffers)
      .def_readwrite("shapes", &ModelIO::shapes);
}

}  //  namespace infer_server
