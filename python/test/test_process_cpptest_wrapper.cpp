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
#include <string>
#include <utility>
#include <vector>

#include "glog/logging.h"
#include "pybind11/pybind11.h"

#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

class PreprocTest : public IPreproc {
 public:
  PreprocTest() { VLOG(1) << "[EasyDK Tests] [InferServer] [PythonAPI] PreprocTest()";}
  int OnTensorParams(const CnPreprocTensorParams *params) override {
    VLOG(1) << "[EasyDK Tests] [InferServer] [PythonAPI] PreprocTest:OnTensorParams()";
    return 0;
  }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect> &src_rects) override {
    VLOG(1) << "[EasyDK Tests] [InferServer] [PythonAPI] PreprocTest:OnPreproc()";
    return 0;
  }
};  // class PreprocTest

class PostprocTest : public IPostproc {
 public:
  PostprocTest() {}

  int OnPostproc(const std::vector<InferData*>& data_vec, const ModelIO& model_output,
                 const ModelInfo* model_info) override {
    VLOG(1) << "[EasyDK Tests] [InferServer] [PythonAPI] PostprocTest:OnPostproc()";
    return 0;
  }
};  // class PostprocTest

void TestProcessWrapper(const py::module &m) {
  py::class_<PreprocTest, std::shared_ptr<PreprocTest>, IPreproc>(m, "PreprocTest")
      .def(py::init<>());
  py::class_<PostprocTest, std::shared_ptr<PostprocTest>, IPostproc>(m, "PostprocTest")
      .def(py::init<>());
}

}  // namespace infer_server
