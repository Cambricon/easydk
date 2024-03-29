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

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "cnis/infer_server.h"
#include "cnrt.h"
#include "core/data_type.h"
#include "half.h"

using infer_server::DataLayout;
using infer_server::DataType;
using infer_server::DimOrder;
using infer_server::Shape;
using infer_server::detail::CastDataType;
using infer_server::detail::DataTypeStr;

namespace {

TEST(InferServerCore, CastDataType) {
#define TEST_CAST_DATATYPE(type)                          \
  do {                                                    \
    EXPECT_EQ(CastDataType(DataType::type), CNRT_##type); \
    EXPECT_EQ(CastDataType(CNRT_##type), DataType::type); \
  } while (0)

  TEST_CAST_DATATYPE(UINT8);
  TEST_CAST_DATATYPE(FLOAT16);
  TEST_CAST_DATATYPE(FLOAT32);
  TEST_CAST_DATATYPE(INT16);
  TEST_CAST_DATATYPE(INT32);
#undef TEST_CAST_DATATYPE
}

TEST(InferServerCore, DataTypeStr) {
#define TEST_DATATYPE_STR(type) EXPECT_EQ(DataTypeStr(DataType::type), std::string(#type))

  TEST_DATATYPE_STR(UINT8);
  TEST_DATATYPE_STR(FLOAT16);
  TEST_DATATYPE_STR(FLOAT32);
  TEST_DATATYPE_STR(INT16);
  TEST_DATATYPE_STR(INT32);
#undef TEST_DATATYPE_STR
}

template <typename dtype>
void Transpose(dtype* input_data, dtype* output_data, const std::vector<Shape::value_type>& input_shape,
               const std::vector<int>& axis) {
  int old_index = -1;
  int new_index = -1;
  const std::vector<Shape::value_type>& s = input_shape;

  if (input_shape.size() != 4 || axis.size() != 4) {
    LOG(ERROR) << "[EasyDK Tests] [InferServer] Only support 4 dimension";
    std::terminate();
  }

  int dim[4] = {0};
  for (dim[0] = 0; dim[0] < s[0]; dim[0]++) {
    for (dim[1] = 0; dim[1] < s[1]; dim[1]++) {
      for (dim[2] = 0; dim[2] < s[2]; dim[2]++) {
        for (dim[3] = 0; dim[3] < s[3]; dim[3]++) {
          old_index = dim[0] * s[1] * s[2] * s[3] + dim[1] * s[2] * s[3] + dim[2] * s[3] + dim[3];
          new_index = dim[axis[0]] * s[axis[1]] * s[axis[2]] * s[axis[3]] + dim[axis[1]] * s[axis[2]] * s[axis[3]] +
                      dim[axis[2]] * s[axis[3]] + dim[axis[3]];
          output_data[new_index] = input_data[old_index];
        }
      }
    }
  }
}

template <typename src_dtype, typename dst_dtype>
void Cast(src_dtype* src, dst_dtype* dst, size_t len) {
  std::transform(src, src + len, dst, [](src_dtype src) { return static_cast<dst_dtype>(src); });
}

template <typename dtype>
bool CompareData(dtype* d1, dtype* d2, size_t len, float threshold) {
  float diff = 0.0;
  float mae = 0.0;
  float mse = 0.0;
  float ma = 0.0;
  float ms = 0.0;

  for (size_t idx = 0; idx < len; ++idx) {
    diff = static_cast<float>(d2[idx]) - static_cast<float>(d1[idx]);
    ma += std::abs(static_cast<float>(d1[idx]));
    ms += static_cast<float>(d1[idx]) * static_cast<float>(d1[idx]);
    mae += std::abs(diff);
    mse += diff * diff;
  }

  mae /= ma;
  mse = std::sqrt(mse);
  ms = std::sqrt(ms);
  mse /= ms;

  VLOG(5) << "[EasyDK Tests] [InferServer] Compare data, MSE: " << mse << ", MAE: " << mae;
  return !(mse > threshold || mae > threshold);
}

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int> shape_dis(1, 608);
std::uniform_real_distribution<float> data_dis(-128, 128);
std::uniform_real_distribution<float> u8_data_dis(0, 255);
const std::vector<int> nhwc2nchw_axis = {0, 3, 1, 2};
const std::vector<int> nchw2nhwc_axis = {0, 2, 3, 1};
constexpr DataLayout nchw_u8{DataType::UINT8, DimOrder::NCHW};
constexpr DataLayout nchw_f32{DataType::FLOAT32, DimOrder::NCHW};
constexpr DataLayout nchw_f16{DataType::FLOAT16, DimOrder::NCHW};
constexpr DataLayout nhwc_u8{DataType::UINT8, DimOrder::NHWC};
constexpr DataLayout nhwc_f32{DataType::FLOAT32, DimOrder::NHWC};
constexpr DataLayout nhwc_f16{DataType::FLOAT16, DimOrder::NHWC};

constexpr size_t repeat_times = 3;

inline Shape GenShape() {
  Shape s({shape_dis(gen) % 16 + 1, shape_dis(gen), shape_dis(gen), shape_dis(gen) % 4 + 1});
  VLOG(5) << "[EasyDK Tests] [InferServer] Test shape: " << s;
  return s;
}

}  // namespace
