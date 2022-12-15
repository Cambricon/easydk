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
#include <memory>
#include <vector>

#include "pybind11/pybind11.h"

#include "cnis/infer_server.h"
#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

void SessionDescWrapper(const py::module& m) {
  py::class_<SessionDesc, std::shared_ptr<SessionDesc>>(m, "SessionDesc")
      .def(py::init<>())
      .def_readwrite("name", &SessionDesc::name)
      .def_readwrite("model", &SessionDesc::model)
      .def_readwrite("strategy", &SessionDesc::strategy)
      .def_readwrite("model_input_format", &SessionDesc::model_input_format)
      .def_readwrite("preproc", &SessionDesc::preproc)
      .def_readwrite("postproc", &SessionDesc::postproc)
      .def_readwrite("batch_timeout", &SessionDesc::batch_timeout)
      .def_readwrite("priority", &SessionDesc::priority)
      .def_readwrite("engine_num", &SessionDesc::engine_num)
      .def_readwrite("show_perf", &SessionDesc::show_perf);
}

}  //  namespace infer_server
