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

#ifdef _MSC_VER
#include "windows.h"
#else
#include "sys/sysinfo.h"
#endif
#include <cstdlib>

#include "util/env.h"

namespace infer_server {

TEST(InferServerUtil, CpuCoreNumber) {
  int core_number = GetCpuCoreNumber();
  EXPECT_EQ(get_nprocs(), core_number);
  EXPECT_EQ(sysconf(_SC_NPROCESSORS_ONLN), core_number);
}

TEST(InferServerUtil, GetBoolFromEnv) {
  const std::string env_str = "CNIS_TEST_ENV_BOOL";
  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);

  // get default value if env is not set
  EXPECT_FALSE(GetBoolFromEnv(env_str));
  EXPECT_FALSE(GetBoolFromEnv(env_str, false));
  EXPECT_TRUE(GetBoolFromEnv(env_str, true));

  // only false and 0 means false
  ASSERT_EQ(setenv(env_str.c_str(), "false", 1), 0);
  EXPECT_FALSE(GetBoolFromEnv(env_str, true));
  ASSERT_EQ(setenv(env_str.c_str(), "0", 1), 0);
  EXPECT_FALSE(GetBoolFromEnv(env_str, true));

  // otherwise means true
  ASSERT_EQ(setenv(env_str.c_str(), "true", 1), 0);
  EXPECT_TRUE(GetBoolFromEnv(env_str, false));
  ASSERT_EQ(setenv(env_str.c_str(), "True", 1), 0);
  EXPECT_TRUE(GetBoolFromEnv(env_str, false));
  ASSERT_EQ(setenv(env_str.c_str(), "1", 1), 0);
  EXPECT_TRUE(GetBoolFromEnv(env_str, false));
  ASSERT_EQ(setenv(env_str.c_str(), "ON", 1), 0);
  EXPECT_TRUE(GetBoolFromEnv(env_str, false));
  ASSERT_EQ(setenv(env_str.c_str(), "asguwieb", 1), 0);
  EXPECT_TRUE(GetBoolFromEnv(env_str, false));

  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);
}

TEST(InferServerUtil, GetIntFromEnv) {
  const std::string env_str = "CNIS_TEST_ENV_INT";
  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);

  // get default value if env is not set
  EXPECT_EQ(GetIntFromEnv(env_str), 0);
  EXPECT_EQ(GetIntFromEnv(env_str, 1), 1);
  EXPECT_EQ(GetIntFromEnv(env_str, -2), -2);
  EXPECT_EQ(GetIntFromEnv(env_str, 4), 4);

  ASSERT_EQ(setenv(env_str.c_str(), "2", 1), 0);
  EXPECT_EQ(GetIntFromEnv(env_str), 2);
  ASSERT_EQ(setenv(env_str.c_str(), "-17", 1), 0);
  EXPECT_EQ(GetIntFromEnv(env_str), -17);
  ASSERT_EQ(setenv(env_str.c_str(), "124", 1), 0);
  EXPECT_EQ(GetIntFromEnv(env_str), 124);

  ASSERT_EQ(setenv(env_str.c_str(), "vbuiwe", 1), 0);
  EXPECT_THROW(GetIntFromEnv(env_str), std::invalid_argument);
  ASSERT_EQ(setenv(env_str.c_str(), "213546189236846283746182768323", 1), 0);
  EXPECT_THROW(GetIntFromEnv(env_str), std::out_of_range);

  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);
}

TEST(InferServerUtil, GetUlongFromEnv) {
  const std::string env_str = "CNIS_TEST_ENV_ULONG";
  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);

  // get default value if env is not set
  EXPECT_EQ(GetUlongFromEnv(env_str), 0u);
  EXPECT_EQ(GetUlongFromEnv(env_str, 1), 1u);
  EXPECT_EQ(GetUlongFromEnv(env_str, 2), 2u);
  EXPECT_EQ(GetUlongFromEnv(env_str, 4), 4u);

  ASSERT_EQ(setenv(env_str.c_str(), "2", 1), 0);
  EXPECT_EQ(GetUlongFromEnv(env_str), 2u);
  ASSERT_EQ(setenv(env_str.c_str(), "1732", 1), 0);
  EXPECT_EQ(GetUlongFromEnv(env_str), 1732u);
  ASSERT_EQ(setenv(env_str.c_str(), "124", 1), 0);
  EXPECT_EQ(GetUlongFromEnv(env_str), 124u);

  ASSERT_EQ(setenv(env_str.c_str(), "gwgsdawe", 1), 0);
  EXPECT_THROW(GetUlongFromEnv(env_str), std::invalid_argument);
  ASSERT_EQ(setenv(env_str.c_str(), "214235234612353546189324234236846283746182768323", 1), 0);
  EXPECT_THROW(GetUlongFromEnv(env_str), std::out_of_range);

  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);
}

TEST(InferServerUtil, GetStringFromEnv) {
  const std::string env_str = "CNIS_TEST_ENV_STRING";
  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);

  std::string str_value = "some str";
  // get default value if env is not set
  EXPECT_EQ(GetStringFromEnv(env_str), "");
  EXPECT_EQ(GetStringFromEnv(env_str, str_value), str_value);

  str_value = "gbawrebawe";
  ASSERT_EQ(setenv(env_str.c_str(), str_value.c_str(), 1), 0);
  EXPECT_EQ(GetStringFromEnv(env_str), str_value);
  str_value = "aweuggaibefiu";
  ASSERT_EQ(setenv(env_str.c_str(), str_value.c_str(), 1), 0);
  EXPECT_EQ(GetStringFromEnv(env_str), str_value);
  str_value = "aeui12529hgdkd29";
  ASSERT_EQ(setenv(env_str.c_str(), str_value.c_str(), 1), 0);
  EXPECT_EQ(GetStringFromEnv(env_str), str_value);

  // clear env
  ASSERT_EQ(unsetenv(env_str.c_str()), 0);
}

}  // namespace infer_server
