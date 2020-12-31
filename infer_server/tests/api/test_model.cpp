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

#include "cnrt.h"
#include "fixture.h"
#include "infer_server.h"
#include "model/model.h"

namespace infer_server {

TEST_F(InferServerTestAPI, ModelManager) {
  char env[] = "CNIS_MODEL_CACHE_LIMIT=3";
  putenv(env);
  InferServer::ClearModelCache();
  std::string model_file = GetExePath() + "../../../samples/data/models/MLU270/resnet50_offline.cambricon";
  auto m = server_->LoadModel(model_file, "subnet0");
  EXPECT_TRUE(m);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  auto n = server_->LoadModel(model_file, "subnet0");
  EXPECT_TRUE(n);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  auto l = server_->LoadModel("http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon",
                              "subnet0");
  EXPECT_TRUE(l);
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 2);
  server_->LoadModel(GetExePath() + "../../../samples/data/models/MLU270/resnet34_ssd.cambricon", "subnet0");
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 3);
  /************************************************************************************/
  std::ifstream infile(model_file, std::ios::binary);
  if (!infile.is_open()) {
    LOG(ERROR) << "file open failed";
  }
  std::filebuf* pbuf = infile.rdbuf();
  uint32_t filesize = static_cast<uint32_t>((pbuf->pubseekoff(0, std::ios::end, std::ios::in)));
  pbuf->pubseekpos(0, std::ios::in);
  char* modelptr = new char[filesize];
  pbuf->sgetn(modelptr, filesize);
  infile.close();
  server_->LoadModel(modelptr, "subnet0");
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 3);
  EXPECT_TRUE(server_->UnloadModel(m));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 2);
  EXPECT_FALSE(server_->UnloadModel(n));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 2);
  EXPECT_TRUE(server_->UnloadModel(l));
  EXPECT_EQ(ModelManager::Instance()->CacheSize(), 1);
  delete[] modelptr;
}

}  // namespace infer_server
