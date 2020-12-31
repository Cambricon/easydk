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

#ifndef INFER_SERVER_TEST_BASE_H_
#define INFER_SERVER_TEST_BASE_H_

#include <gtest/gtest.h>
#include <unistd.h>

#include <string>

#include "device/mlu_context.h"

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
  void SetMluContext() {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
    version_ = context.GetCoreVersion();
  }
  void SetUp() override { SetMluContext(); }
  void TearDown() override {}
  edk::CoreVersion version_;
  static constexpr int device_id_ = 0;
};

#endif  // INFER_SERVER_TEST_BASE_H_
