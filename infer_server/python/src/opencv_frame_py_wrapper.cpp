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
#include <opencv2/opencv.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <vector>
#include <utility>

#include "cnis/contrib/opencv_frame.h"
#include "cnis/infer_server.h"

#include "common_wrapper.hpp"

namespace py = pybind11;

namespace infer_server {

extern std::shared_ptr<py::class_<InferData, std::shared_ptr<InferData>>> gPyInferDataRegister;

class PyOpencvPreproc {
 public:
  PyOpencvPreproc(video::PixelFmt dst_fmt, std::vector<float> mean = {}, std::vector<float> std = {},
                  bool normalize = false, bool keep_aspect_ratio = true, int pad_value = 0, bool transpose = false,
                  DataType src_depth = DataType::UINT8)
      : func_(video::OpencvPreproc::GetFunction(dst_fmt, std::move(mean), std::move(std), normalize, keep_aspect_ratio,
                                                pad_value, transpose, src_depth)) {}
  bool Execute(ModelIO* model_input, const InferData& input_data, const ModelInfo* model_info) {
    return func_(model_input, input_data, model_info);
  }

 private:
  PreprocessorHost::ProcessFunction func_;
};  // class PyOpencvPreproc

void OpencvFrameWrapper(const py::module& m) {
  gPyInferDataRegister->def("set", [](InferData* data, std::reference_wrapper<video::OpencvFrame> cv_frame) {
    data->Set(std::move(cv_frame.get()));
  });
  gPyInferDataRegister->def(
      "get_cv_frame",
      [](InferData* data) { return std::reference_wrapper<video::OpencvFrame>(data->GetLref<video::OpencvFrame>()); },
      py::return_value_policy::reference);

  py::class_<PyOpencvPreproc>(m, "OpencvPreproc")
      .def(py::init<video::PixelFmt, std::vector<float>, std::vector<float>, bool, bool, int, bool, DataType>(),
           py::arg("dst_fmt"), py::arg("mean") = std::vector<float>{}, py::arg("std") = std::vector<float>{},
           py::arg("normalize") = false, py::arg("keep_aspect_ratio") = true, py::arg("pad_value") = 0,
           py::arg("transpose") = false, py::arg("src_depth") = DataType::UINT8)
      .def("execute", [](PyOpencvPreproc preproc, ModelIO* model_input, std::reference_wrapper<InferData> input_data,
                         const ModelInfo* model) { return preproc.Execute(model_input, input_data.get(), model); });

  py::class_<video::OpencvFrame, std::shared_ptr<video::OpencvFrame>>(m, "OpencvFrame")
      .def(py::init<>())
      .def_readwrite("fmt", &video::OpencvFrame::fmt)
      .def_readwrite("roi", &video::OpencvFrame::roi)
      .def_property("img", [](std::shared_ptr<video::OpencvFrame> frame) { return MatToArray(frame->img); },
                    [](std::shared_ptr<video::OpencvFrame> frame, py::array img_array) {
                      cv::Mat cv_img = ArrayToMat(img_array);
                      frame->img = std::move(cv_img);
                    });
}

}  //  namespace infer_server
