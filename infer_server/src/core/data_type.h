/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#ifndef INFER_SERVER_CORE_DATATYPE_H_
#define INFER_SERVER_CORE_DATATYPE_H_

#include <glog/logging.h>
#include <string>
#include <vector>

#include "cnis/infer_server.h"
#include "cnrt.h"

#ifdef CNIS_USE_MAGICMIND
#include "interface_runtime.h"
#endif

namespace infer_server {
namespace detail {

inline std::string DataTypeStr(DataType type) noexcept {
  switch (type) {
#define DATATYPE2STR(type) \
  case DataType::type:     \
    return #type;
    DATATYPE2STR(UINT8)
    DATATYPE2STR(FLOAT16)
    DATATYPE2STR(FLOAT32)
    DATATYPE2STR(INT32)
    DATATYPE2STR(INT16)
#undef DATATYPE2STR
    default:
      LOG(ERROR) << "Unsupported data type";
      return "INVALID";
  }
}

inline std::string DimOrderStr(DimOrder order) noexcept {
  switch (order) {
#define DIMORDER2STR(order) \
  case DimOrder::order:     \
    return #order;
    DIMORDER2STR(NCHW)
    DIMORDER2STR(NHWC)
    DIMORDER2STR(HWCN)
    DIMORDER2STR(TNC)
    DIMORDER2STR(NTC)
    DIMORDER2STR(NONE)
#undef DIMORDER2STR
    default:
      LOG(ERROR) << "Unsupported dim order";
      return "INVALID";
  }
}

inline cnrtDataType CastDataType(DataType type) noexcept {
  switch (type) {
#define RETURN_DATA_TYPE(type) \
  case DataType::type:         \
    return CNRT_##type;
    RETURN_DATA_TYPE(UINT8)
    RETURN_DATA_TYPE(FLOAT16)
    RETURN_DATA_TYPE(FLOAT32)
    RETURN_DATA_TYPE(INT32)
    RETURN_DATA_TYPE(INT16)
#undef RETURN_DATA_TYPE
    default:
      LOG(ERROR) << "Unsupported data type";
      return CNRT_INVALID;
  }
}

#ifdef CNIS_USE_MAGICMIND
inline DataType CastDataType(magicmind::DataType type) noexcept {
  switch (type) {
#define RETURN_DATA_TYPE(type)    \
  case magicmind::DataType::type: \
    return DataType::type;
    RETURN_DATA_TYPE(UINT8)
    RETURN_DATA_TYPE(FLOAT16)
    RETURN_DATA_TYPE(FLOAT32)
    RETURN_DATA_TYPE(INT32)
    RETURN_DATA_TYPE(INT16)
#undef RETURN_DATA_TYPE
    default:
      LOG(ERROR) << "Unsupported data type";
      return DataType::INVALID;
  }
}
#endif

inline DataType CastDataType(cnrtDataType type) noexcept {
  switch (type) {
#define RETURN_DATA_TYPE(type) \
  case CNRT_##type:            \
    return DataType::type;
    RETURN_DATA_TYPE(UINT8)
    RETURN_DATA_TYPE(FLOAT16)
    RETURN_DATA_TYPE(FLOAT32)
    RETURN_DATA_TYPE(INT32)
    RETURN_DATA_TYPE(INT16)
#undef RETURN_DATA_TYPE
    default:
      LOG(ERROR) << "Unsupported data type";
      return DataType::INVALID;
  }
}

#ifdef CNIS_USE_MAGICMIND
inline DimOrder CastDimOrder(magicmind::Layout order) noexcept {
  switch (order) {
#define RETURN_DIM_ORDER(order)    \
  case magicmind::Layout::order: \
    return DimOrder::order;
    RETURN_DIM_ORDER(NCHW)
    RETURN_DIM_ORDER(NHWC)
    RETURN_DIM_ORDER(HWCN)
    RETURN_DIM_ORDER(TNC)
    RETURN_DIM_ORDER(NTC)
    RETURN_DIM_ORDER(NONE)
#undef RETURN_DIM_ORDER
    default:
      LOG(ERROR) << "Unsupported dim order";
      return DimOrder::INVALID;
  }
}
#endif

// shape corresponding to src_data
bool CastDataType(void *src_data, void *dst_data, DataType src_dtype, DataType dst_dtype, const Shape &shape);

// shape corresponding to src_data
bool TransLayout(void* src_data, void* dst_data, DataLayout src_layout, DataLayout dst_layout, const Shape& shape);

}  // namespace detail

template <typename dtype>
inline std::vector<dtype> DimNHWC2NCHW(const std::vector<dtype>& dim) {
  switch (dim.size()) {
    case 1:
      return dim;
    case 2:
      return dim;
    case 3:
      return std::vector<dtype>({dim[0], dim[2], dim[1]});
    case 4:
      return std::vector<dtype>({dim[0], dim[3], dim[1], dim[2]});
    case 5:
      return std::vector<dtype>({dim[0], dim[4], dim[1], dim[2], dim[3]});
    default:
      CHECK(0) << "unsupport dimension";
  }
  return {};
}

template <typename dtype>
inline std::vector<dtype> DimNCHW2NHWC(const std::vector<dtype>& dim) {
  switch (dim.size()) {
    case 1:
      return dim;
    case 2:
      return dim;
    case 3:
      return std::vector<dtype>({dim[0], dim[2], dim[1]});
    case 4:
      return std::vector<dtype>({dim[0], dim[2], dim[3], dim[1]});
    case 5:
      return std::vector<dtype>({dim[0], dim[2], dim[3], dim[4], dim[1]});
    default:
      CHECK(0) << "unsupport dimension";
  }
  return {};
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v_t) {
  os << "vec {";
  for (size_t i = 0; i < v_t.size() - 1; ++i) {
    os << v_t[i] << ", ";
  }
  if (v_t.size() > 0) os << v_t[v_t.size() - 1];
  os << "}";
  return os;
}
}  // namespace infer_server

#endif  // INFER_SERVER_CORE_DATATYPE_H_
