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
#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "cnis/infer_server.h"
#include "cnrt.h"
#include "fixture.h"
#include "model/model.h"

namespace infer_server {
namespace {

TEST_F(InferServerTestAPI, ModelManager) {
  char env[] = "CNIS_MODEL_CACHE_LIMIT=2";
  putenv(env);
  InferServer::ClearModelCache();
  auto m = server_->LoadModel(GetModelInfoStr("resnet50", "url"));
  ASSERT_TRUE(m);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  auto n = server_->LoadModel(GetModelInfoStr("resnet50", "url"));
  ASSERT_TRUE(n);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  auto l = server_->LoadModel(GetModelInfoStr("yolov3", "url"));
  ASSERT_TRUE(l);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 2);
  /************************************************************************************/
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 2);
  ASSERT_TRUE(server_->UnloadModel(m));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  ASSERT_FALSE(server_->UnloadModel(n));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  ASSERT_TRUE(server_->UnloadModel(l));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 0);
}

}  // namespace
}  // namespace infer_server
