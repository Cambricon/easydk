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

#include "processor_py_wrapper.hpp"

#include "cnis/contrib/video_helper.h"

namespace py = pybind11;

namespace infer_server {

class PyPreprocessorMlu : public video::PreprocessorMLU {
 public:
  using video::PreprocessorMLU::PreprocessorMLU;
  Status Init() noexcept override { PYBIND11_OVERRIDE(Status, video::PreprocessorMLU, Init); }
  Status Process(PackagePtr data) noexcept override {
    PYBIND11_OVERRIDE(Status, video::PreprocessorMLU, Process, data);
  }
};  // class PyPreprocessorMlu

void VideoProcessorWrapper(const py::module &m) {
  py::class_<video::PreprocessorMLU, Processor, PyPreprocessorMlu, std::shared_ptr<video::PreprocessorMLU>>(
      m, "VideoPreprocessorMLU")
      .def(py::init<>());
}

}  //  namespace infer_server
