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

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "cnis/contrib/video_helper.h"

namespace py = pybind11;

namespace infer_server {

struct PreprocTest {
 public:
  PreprocTest() {std::cout << "PreprocTest()" << std::endl;}

  bool Execute(ModelIO* model_input, const InferData& input_data, const ModelInfo* model_info) {
    std::cout << "PreprocTest:Execute()" << std::endl;
    return true;
  }
};  // struct PreprocTest

struct PostprocTest {
 public:
  PostprocTest() {}

  bool Execute(InferData* result, const ModelIO& model_output, const ModelInfo* model) {
    std::cout << "PostprocTest:Execute()" << std::endl;
    return true;
  }
};  // struct PostprocTest

void TestProcessWrapper(const py::module &m) {
  py::class_<PreprocTest>(m, "PreprocTest")
      .def(py::init<>())
      .def("execute", [] (PreprocTest preproc, ModelIO* model_input, std::reference_wrapper<InferData> input_data,
                          const ModelInfo* model) {
          return preproc.Execute(model_input, input_data.get(), model);
      }, py::call_guard<py::gil_scoped_release>());
  py::class_<PostprocTest>(m, "PostprocTest")
      .def(py::init<>())
      .def("execute", [] (PostprocTest postproc, InferData* result, std::reference_wrapper<ModelIO> model_output,
                          const ModelInfo* model) {
          return postproc.Execute(result, model_output.get(), model);
      }, py::call_guard<py::gil_scoped_release>());
}

}  // namespace infer_server
