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

#include "../test_base.h"
#include "cnis_test_base.h"
#include "core/data_type.h"
#include "model/model.h"

#define CHECK_CNRT_RET(ret, msg)                                       \
  do {                                                                 \
    EXPECT_EQ(ret, CNRT_RET_SUCCESS) << msg << " error code: " << ret; \
  } while (0)

namespace infer_server {

TEST_F(InferServerTest, Model) {
  auto m = std::make_shared<Model>();

  // download model
  auto tmp = InferServer::LoadModel(GetModelInfoStr("resnet50", "url"));

  InferServer::UnloadModel(tmp);
  tmp.reset();

  ASSERT_TRUE(m->Init(GetModelInfoStr("resnet50", "name")));

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
}

}  // namespace infer_server
