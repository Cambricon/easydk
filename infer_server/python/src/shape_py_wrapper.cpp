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
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <vector>

#include "cnis/shape.h"

namespace py = pybind11;

namespace infer_server {

void ShapeWrapper(const py::module &m) {
  py::class_<Shape, std::shared_ptr<Shape>>(m, "Shape")
      .def(py::init())
      .def(py::init<const std::vector<Shape::value_type> &>())
      .def("size", &Shape::Size)
      .def("empty", &Shape::Empty)
      .def("vectorize", &Shape::Vectorize)
      .def("batch_size", &Shape::BatchSize)
      .def("data_count", &Shape::DataCount)
      .def("batch_data_count", &Shape::BatchDataCount)
      .def("__setitem__", [](Shape &self, unsigned index, Shape::value_type val) { self[index] = val; })
      .def("__getitem__", [](Shape &self, unsigned index) { return self[index]; })
      .def(py::self == py::self)
      .def(py::self != py::self);
}

}  //  namespace infer_server
