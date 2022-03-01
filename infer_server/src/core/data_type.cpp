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

#include "data_type.h"

#include <algorithm>
#include <vector>

#include "cnis/processor.h"

namespace infer_server {
namespace detail {

#define CHECK_CNRT_RET(ret, msg, val)              \
  do {                                             \
    if ((ret) != CNRT_RET_SUCCESS) {               \
      LOG(ERROR) << (msg) << " error code: " << (ret); \
      return val;                                  \
    }                                              \
  } while (0)

inline std::vector<int> GetTransOrderAxis(DimOrder src, DimOrder dst, size_t n_dims) {
  std::vector<int> axis;
  if (src == DimOrder::NCHW && dst == DimOrder::NHWC) {
    axis.resize(n_dims, 0);
    VLOG(5) << "transform NCHW to NHWC";
    for (size_t i = 1; i < n_dims - 1; ++i) {
      axis[i] = i + 1;
    }
    if (n_dims > 1) axis[n_dims - 1] = 1;
  } else if (src == DimOrder::NHWC && dst == DimOrder::NCHW) {
    axis.resize(n_dims, 0);
    VLOG(5) << "transform NHWC to NCHW";
    if (n_dims > 1) axis[1] = n_dims - 1;
    for (size_t i = 2; i < n_dims; ++i) {
      axis[i] = i - 1;
    }
  } else if (src == dst) {
    VLOG(5) << "do not transform order";
  } else {
    std::string msg = "Unsupport data order: (src) " + DimOrderStr(src) + ", (dst) " + DimOrderStr(dst);
    LOG(ERROR) << msg;
    throw std::runtime_error(msg);
  }
  return axis;
}

bool CastDataType(void *src_data, void *dst_data, DataType src_dtype, DataType dst_dtype, const Shape &shape) {
  if (src_dtype != dst_dtype) {
    int size = shape.BatchDataCount();
    cnrtRet_t error_code = CNRT_RET_SUCCESS;
    error_code = cnrtCastDataType(src_data, detail::CastDataType(src_dtype), dst_data,
                                  detail::CastDataType(dst_dtype), size, nullptr);
    CHECK_CNRT_RET(error_code, "Cast data type failed.", false);
  }
  return true;
}

bool TransLayout(void *src_data, void *dst_data, DataLayout src_layout, DataLayout dst_layout, const Shape &shape) {
  VLOG(5) << "[Translayout] src layout: " << DataTypeStr(src_layout.dtype) << " | " << DimOrderStr(src_layout.order);
  VLOG(5) << "              dst layout: " << DataTypeStr(dst_layout.dtype) << " | " << DimOrderStr(dst_layout.order);
  VLOG(5) << "              src shape: " << shape;
  if (src_layout.order != DimOrder::NHWC && src_layout.order != DimOrder::NCHW) {
    LOG(ERROR) << "TransLayout: Unsupport data order.";
    return false;
  }

  char bits = 0;
  if (src_layout.dtype != dst_layout.dtype) {
    bits |= 1 << 0;
    VLOG(5) << "cast " << DataTypeStr(src_layout.dtype) << " to " << DataTypeStr(dst_layout.dtype);
  }

  size_t n_dims = shape.Size();
  std::vector<int> axis = GetTransOrderAxis(src_layout.order, dst_layout.order, n_dims);
  if (!axis.empty()) {
    bits |= 1 << 1;
  }

  cnrtRet_t error_code = CNRT_RET_SUCCESS;
  int size = shape.BatchDataCount();
  auto shape_vec_i64 = shape.Vectorize();
  std::vector<int> shape_vec(shape_vec_i64.begin(), shape_vec_i64.end());
  int *dim_values = shape_vec.data();

  switch (bits) {
    case 1 << 0:
      error_code = cnrtCastDataType(src_data, detail::CastDataType(src_layout.dtype), dst_data,
                                    detail::CastDataType(dst_layout.dtype), size, nullptr);
      CHECK_CNRT_RET(error_code, "Cast data type failed.", false);
      break;
    case 1 << 1:
      error_code = cnrtTransDataOrder(src_data, detail::CastDataType(src_layout.dtype), dst_data, n_dims, dim_values,
                                      axis.data());
      CHECK_CNRT_RET(error_code, "Trans data order failed.", false);
      break;
    case 1 << 0 | 1 << 1:
      error_code =
          cnrtTransOrderAndCast(src_data, detail::CastDataType(src_layout.dtype), dst_data,
                                detail::CastDataType(dst_layout.dtype), nullptr, n_dims, dim_values, axis.data());
      CHECK_CNRT_RET(error_code, "Trans data order and cast data type failed.", false);
      break;
    default:
      size_t mem_size = size * GetTypeSize(src_layout.dtype);
      memcpy(dst_data, src_data, mem_size);
      break;
  }
  return true;
}

}  // namespace detail

size_t GetTypeSize(DataType type) noexcept {
  switch (type) {
    case DataType::UINT8:
      return sizeof(uint8_t);
    case DataType::FLOAT16:
      return sizeof(int16_t);
    case DataType::FLOAT32:
      return sizeof(float);
    case DataType::INT32:
      return sizeof(int32_t);
    case DataType::INT16:
      return sizeof(int16_t);
    default:
      LOG(ERROR) << "Unsupported data type";
      return 0;
  }
}

}  // namespace infer_server
