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

#include "cnrt.h"
#include "infer_server.h"

namespace infer_server {
namespace detail {

inline std::string DataTypeStr(const DataType &type) noexcept {
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

bool TransLayout(void *src_data, void *dst_data, const DataLayout &src_layout, const DataLayout &dst_layout,
                 const Shape &shape);

}  // namespace detail
}  // namespace infer_server

#endif  // INFER_SERVER_CORE_DATATYPE_H_
