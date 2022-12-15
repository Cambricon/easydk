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

#include <string>

#include "cnis/infer_server.h"
#include "cnis/processor.h"

namespace infer_server {

TEST(InferServer, DataTypeSize) {
  EXPECT_EQ(GetTypeSize(DataType::UINT8), 1u);
  EXPECT_EQ(GetTypeSize(DataType::FLOAT16), 2u);
  EXPECT_EQ(GetTypeSize(DataType::FLOAT32), 4u);
  EXPECT_EQ(GetTypeSize(DataType::INT32), 4u);
  EXPECT_EQ(GetTypeSize(DataType::INT16), 2u);
}

TEST(InferServer, PredictorBackend) { EXPECT_EQ(Predictor::Backend(), std::string("magicmind")); }

}  // namespace infer_server
