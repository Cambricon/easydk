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

#include "glog/logging.h"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#if (CV_MAJOR_VERSION >= 3)
#include "opencv2/imgcodecs/imgcodecs.hpp"
#endif

#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "cnis/shape.h"

namespace py = pybind11;

namespace infer_server {

py::dtype GetNpDType(int depth) {
  switch (depth) {
    case CV_8U:
      return py::dtype::of<uint8_t>();
    case CV_8S:
      return py::dtype::of<int8_t>();
    case CV_16U:
      return py::dtype::of<uint16_t>();
    case CV_16S:
      return py::dtype::of<int16_t>();
    case CV_32S:
      return py::dtype::of<int32_t>();
    case CV_32F:
      return py::dtype::of<float>();
    case CV_64F:
      return py::dtype::of<double>();
    default:
      throw std::invalid_argument("Data type is not supported.");
  }
}

std::vector<std::size_t> GetShape(cv::Mat& m) {  // NOLINT
  if (m.channels() == 1) {
    return {static_cast<size_t>(m.rows), static_cast<size_t>(m.cols)};
  }
  return {static_cast<size_t>(m.rows), static_cast<size_t>(m.cols), static_cast<size_t>(m.channels())};
}

py::capsule MakeCapsule(cv::Mat& m) {  // NOLINT
  return py::capsule(new cv::Mat(m), [](void* v) { delete reinterpret_cast<cv::Mat*>(v); });
}

py::array MatToArray(cv::Mat& m) {  // NOLINT
  if (!m.isContinuous()) {
    throw std::invalid_argument("Only continuous Mat supported.");
  }
  return py::array(GetNpDType(m.depth()), GetShape(m), m.data, MakeCapsule(m));
}

int GetCVDepth(py::dtype dtype) {
  if (dtype.is(py::dtype::of<uchar>()) || dtype.is(py::dtype::of<uint8_t>())) {
    return CV_8U;
  } else if (dtype.is(py::dtype::of<int8_t>())) {
    return CV_8S;
  } else if (dtype.is(py::dtype::of<uint16_t>())) {
    return CV_16U;
  } else if (dtype.is(py::dtype::of<int16_t>())) {
    return CV_16S;
  } else if (dtype.is(py::dtype::of<int32_t>())) {
    return CV_32S;
  } else if (dtype.is(py::dtype::of<float>())) {
    return CV_32F;
  } else if (dtype.is(py::dtype::of<double>())) {
    return CV_64F;
  } else {
    throw std::invalid_argument("Data type is not supported.");
  }
  return CV_8U;
}

size_t GetTotalSize(const py::array& ar) { return ar.dtype().itemsize() * ar.size(); }
// convert an np.array to a cv::Mat
cv::Mat ArrayToMat(const py::array& ar) {
  int ndim = ar.ndim();
  auto shape = ar.shape();
  int rows = 1;
  int cols = 1;
  int channels = 1;
  switch (ndim) {
    case 1:
      rows = shape[0];
      break;
    case 2:
      rows = shape[0];
      cols = shape[1];
      break;
    case 3:
      rows = shape[0];
      cols = shape[1];
      channels = shape[2];
      break;
    default:
      throw std::invalid_argument("Ndim should not be greater than 3.");
      return cv::Mat();
  }
  int type = CV_MAKETYPE(GetCVDepth(ar.dtype()), channels);
  cv::Mat mat = cv::Mat(rows, cols, type);
  memcpy(mat.data, ar.data(), GetTotalSize(ar));

  return mat;
}

py::dtype GetPyDtype(DataType dtype) {
  switch (dtype) {
    case DataType::UINT8:
      return py::dtype::of<uint8_t>();
    case DataType::FLOAT16:
    case DataType::FLOAT32:
      return py::dtype::of<float>();
    case DataType::INT16:
      return py::dtype::of<int16_t>();
    case DataType::INT32:
      return py::dtype::of<int32_t>();
    default:
      throw std::invalid_argument("Datatype is not supported.");
      return py::dtype::of<uint8_t>();
  }
}

py::array PointerToArray(void* input, Shape shape, DataLayout layout) {
  py::dtype py_dtype = GetPyDtype(layout.dtype);
  std::vector<int64_t> py_shape = shape.Vectorize();
  switch (layout.order) {
    case DimOrder::NCHW:
    case DimOrder::NHWC:
      py_shape.erase(py_shape.begin());
      break;
    case DimOrder::HWCN:
      py_shape.erase(py_shape.begin() + 3);
      break;
    default:
      throw std::invalid_argument("DimOrder is not supported.");
      return py::array();
  }

  return py::array(py_dtype, py_shape, input, py::capsule(input));
}

py::array PointerToArray(void* input, std::vector<size_t> data_shape, DataType dtype) {
  py::dtype py_dtype = GetPyDtype(dtype);
  return py::array(py_dtype, data_shape, input, py::capsule(input));
}

py::array PointerToArray(void* input, std::vector<size_t> data_shape, py::dtype py_dtype) {
  return py::array(py_dtype, data_shape, input, py::capsule(input));
}

}  // namespace infer_server
