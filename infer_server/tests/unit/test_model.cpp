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

#include <memory>
#include <string>
#include <vector>

#include "cnrt.h"
#include "core/data_type.h"
#include "model/model.h"
#include "test_base.h"

#define CHECK_CNRT_RET(ret, msg)                                       \
  do {                                                                 \
    EXPECT_EQ(ret, CNRT_RET_SUCCESS) << msg << " error code: " << ret; \
  } while (0)

namespace infer_server {

TEST_F(InferServerTest, Model) {
  auto m = std::make_shared<Model>();
#ifdef CNIS_USE_MAGICMIND
  std::string model_uri = "http://video.cambricon.com/models/MLU370/resnet50_nhwc_tfu_0.8.2_uint8_int8_fp16.model";
  // download model
  auto tmp = InferServer::LoadModel(model_uri);
  InferServer::UnloadModel(tmp);
  tmp.reset();

  ASSERT_TRUE(m->Init("resnet50_nhwc_tfu_0.8.2_uint8_int8_fp16.model"));
  auto* model = m->GetModel();

  size_t i_num = model->GetInputNum();
  size_t o_num = model->GetOutputNum();
  ASSERT_EQ(i_num, m->InputNum());
  ASSERT_EQ(o_num, m->OutputNum());
  std::vector<mm::Dims> in_dims = model->GetInputDimensions();
  std::vector<mm::Dims> out_dims = model->GetOutputDimensions();
  std::vector<mm::DataType> i_dtypes = model->GetInputDataTypes();
  std::vector<mm::DataType> o_dtypes = model->GetOutputDataTypes();

  // TODO(dmh): test layout after read layout from model supported by mm
  for (size_t idx = 0; idx < i_num; ++idx) {
    EXPECT_EQ(detail::CastDataType(i_dtypes[idx]), m->InputLayout(idx).dtype);
    EXPECT_EQ(Shape(in_dims[idx].GetDims()), m->InputShape(idx));
  }
  for (size_t idx = 0; idx < o_num; ++idx) {
    EXPECT_EQ(detail::CastDataType(o_dtypes[idx]), m->OutputLayout(idx).dtype);
    EXPECT_EQ(Shape(out_dims[idx].GetDims()), m->OutputShape(idx));
  }
  EXPECT_EQ(in_dims[0].GetDimValue(0), m->BatchSize());
#else
  std::string model_path = GetExePath() + "../../../tests/data/resnet50_270.cambricon";
  ASSERT_TRUE(m->Init(model_path, "subnet0"));
  cnrtRet_t error_code;
  auto function = m->GetFunction();
  auto model = m->GetModel();
  int batch_size;
  error_code = cnrtQueryModelParallelism(model, &batch_size);
  CHECK_CNRT_RET(error_code, "Query Model Parallelism failed.");
  EXPECT_GE(batch_size, 0);
  EXPECT_EQ(static_cast<uint32_t>(batch_size), m->BatchSize());

  int64_t* input_sizes = nullptr;
  int64_t* output_sizes = nullptr;
  int input_num = 0, output_num = 0;
  error_code = cnrtGetInputDataSize(&input_sizes, &input_num, function);
  CHECK_CNRT_RET(error_code, "Get input data size failed.");
  EXPECT_EQ(m->InputNum(), static_cast<uint32_t>(input_num));
  error_code = cnrtGetOutputDataSize(&output_sizes, &output_num, function);
  CHECK_CNRT_RET(error_code, "Get output data size failed.");
  EXPECT_EQ(m->OutputNum(), static_cast<uint32_t>(output_num));
  // get io shapes
  int* input_dim_values = nullptr;
  int* output_dim_values = nullptr;
  int dim_num = 0;
  for (int i = 0; i < input_num; ++i) {
    error_code = cnrtGetInputDataShape(&input_dim_values, &dim_num, i, function);
    CHECK_CNRT_RET(error_code, "Get input data size failed.");
    // nhwc shape
    for (int j = 0; j < dim_num; ++j) {
      EXPECT_EQ(m->InputShape(i)[j], input_dim_values[j]);
    }
    free(input_dim_values);
  }

  for (int i = 0; i < output_num; ++i) {
    error_code = cnrtGetOutputDataShape(&output_dim_values, &dim_num, i, function);
    CHECK_CNRT_RET(error_code, "Get output data shape failed.");
    // nhwc shape
    for (int j = 0; j < dim_num; ++j) {
      EXPECT_EQ(m->OutputShape(i)[j], output_dim_values[j]);
    }
    free(output_dim_values);
  }

  EXPECT_EQ(m->GetKey().compare(model_path + "_" + "subnet0"), 0);
#endif
}

}  // namespace infer_server
