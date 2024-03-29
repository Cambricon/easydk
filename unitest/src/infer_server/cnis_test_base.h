/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#ifndef UNITEST_INFER_SERVER_TEST_BASE_H_
#define UNITEST_INFER_SERVER_TEST_BASE_H_

#include <unistd.h>

#include <iostream>
#include <string>

#include "glog/logging.h"
#include "gtest/gtest.h"

#include "cnedk_platform.h"
#include "cnis/infer_server.h"
#include "../test_base.h"

inline std::string GetExePath() {
  constexpr int PATH_MAX_LENGTH = 1024;
  char path[PATH_MAX_LENGTH];
  int cnt = readlink("/proc/self/exe", path, PATH_MAX_LENGTH);
  if (cnt < 0 || cnt >= PATH_MAX_LENGTH) {
    return "";
  }
  if (path[cnt - 1] == '/') {
    path[cnt - 1] = '\0';
  } else {
    path[cnt] = '\0';
  }
  std::string result(path);
  return std::string(path).substr(0, result.find_last_of('/') + 1);
}

class InferServerTest : public testing::Test {
 protected:
  void SetMluContext() { infer_server::SetCurrentDevice(device_id_); }
  void SetUp() override { SetMluContext(); }
  void TearDown() override {}
  static constexpr int device_id_ = 0;
};

class TestProcessor : public infer_server::ProcessorForkable<TestProcessor> {
 public:
  TestProcessor() noexcept : infer_server::ProcessorForkable<TestProcessor>("TestProcessor") {
    VLOG(1) << "[EasyDK Tests] [InferServer] TestProcessor Construct";
  }

  ~TestProcessor() { VLOG(1) << "[EasyDK Tests] [InferServer] TestProcessor Destruct"; }

  infer_server::Status Process(infer_server::PackagePtr data) noexcept override {
    VLOG(4) << "[EasyDK Tests] [InferServer] TestProcessor Process";
    if (!initialized_) return infer_server::Status::ERROR_BACKEND;
    return infer_server::Status::SUCCESS;
  }

  infer_server::Status Init() noexcept override {
    VLOG(1) << "[EasyDK Tests] [InferServer] TestProcessor Init";
    initialized_ = true;
    return infer_server::Status::SUCCESS;
  }

 private:
  bool initialized_{false};
};

#endif  // UNITEST_INFER_SERVER_TEST_BASE_H_
