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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>

#include "cnis/infer_server.h"
#include "cnis/shape.h"

namespace py = pybind11;

namespace infer_server {

class PyModelInfo : public ModelInfo {
 public:
  const Shape& InputShape(int index) const noexcept override {
    PYBIND11_OVERRIDE_PURE(const Shape&, ModelInfo, InputShape, index);
  }
  const Shape& OutputShape(int index) const noexcept override {
    PYBIND11_OVERRIDE_PURE(const Shape&, ModelInfo, OutputShape, index);
  }
  const DataLayout& InputLayout(int index) const noexcept override {
    PYBIND11_OVERRIDE_PURE(const DataLayout&, ModelInfo, InputLayout, index);
  }
  const DataLayout& OutputLayout(int index) const noexcept override {
    PYBIND11_OVERRIDE_PURE(const DataLayout&, ModelInfo, OutputLayout, index);
  }
  uint32_t InputNum() const noexcept override { PYBIND11_OVERRIDE_PURE(uint32_t, ModelInfo, InputNum); }
  uint32_t OutputNum() const noexcept override { PYBIND11_OVERRIDE_PURE(uint32_t, ModelInfo, OutputNum); }
  uint32_t BatchSize() const noexcept override { PYBIND11_OVERRIDE_PURE(uint32_t, ModelInfo, BatchSize); }
  std::string GetKey() const noexcept override { PYBIND11_OVERRIDE_PURE(std::string, ModelInfo, GetKey); }
  bool FixedOutputShape() noexcept override { PYBIND11_OVERRIDE_PURE(bool, ModelInfo, FixedOutputShape); }
};  // class PyModelInfo

void ModelInfoWrapper(const py::module& m) {
  py::class_<ModelInfo, std::shared_ptr<ModelInfo>, PyModelInfo>(m, "ModelInfo")
      .def(py::init())
      .def("input_shape", &ModelInfo::InputShape)
      .def("output_shape", &ModelInfo::OutputShape)
      .def("input_layout", &ModelInfo::InputLayout)
      .def("output_layout", &ModelInfo::OutputLayout)
      .def("input_num", &ModelInfo::InputNum)
      .def("output_num", &ModelInfo::OutputNum)
      .def("batch_size", &ModelInfo::BatchSize)
      .def("get_key", &ModelInfo::GetKey);
}

}  //  namespace infer_server
