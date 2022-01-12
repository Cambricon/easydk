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
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#if (CV_MAJOR_VERSION >= 3)
#include <opencv2/imgcodecs/imgcodecs.hpp>
#endif

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cnis/shape.h"

namespace py = pybind11;

namespace infer_server {

// For converting cv::Mat to python numpy array
py::dtype GetNpDType(int depth);
std::vector<std::size_t> GetMatShape(cv::Mat& mat);  // NOLINT
py::capsule MakeCapsuleForMat(cv::Mat& mat);  // NOLINT
py::array MatToArray(cv::Mat& mat);  // NOLINT
cv::Mat ArrayToMat(const py::array& array);
size_t GetTotalSize(const py::array& array);
py::array PointerToArray(void* input, Shape shape, DataLayout layout);
py::array PointerToArray(void* input, std::vector<size_t> data_shape, DataType dtype);
py::array PointerToArray(void* input, std::vector<size_t> data_shape, py::dtype dtype);
bool DefaultPreprocExecute(ModelIO* model_input, const InferData& input_data, const ModelInfo* model_info);
}  // namespace infer_server
