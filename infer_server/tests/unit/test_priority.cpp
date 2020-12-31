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

#include <random>

#include "core/priority.h"

using infer_server::Priority;

TEST(InferServerCore, Priority) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> base_dis(-100, 100);
  std::uniform_int_distribution<int> offset_dis(0, 9);
  std::uniform_int_distribution<int64_t> bias_dis(std::numeric_limits<int64_t>::min() / 1e7,
                                                  std::numeric_limits<int64_t>::max() / 1e7);

  for (int i = 0; i < 1000; ++i) {
    int base = base_dis(gen);
    int64_t bias = bias_dis(gen);
    int offset = offset_dis(gen);
    Priority p(base);
    int64_t major = Priority::ShiftMajor(Priority::BaseToMajor(base));
    ASSERT_EQ(Priority::BaseToMajor(base), 10 * std::min(std::max(base, 0), 9));
    ASSERT_EQ(Priority::ShiftMajor(offset * 10), static_cast<int64_t>(offset * 10) << 56);
    ASSERT_EQ(p.Get(0), major);
    ASSERT_EQ(p.Get(bias), major + bias);
    ASSERT_EQ(Priority::Offset(major, offset), major + Priority::ShiftMajor(offset));
    ASSERT_EQ(Priority::Next(major), Priority::Offset(major, 1));
    ASSERT_EQ(p, Priority(base));
    if (base > 0 && base < 10) {
      ASSERT_GT(p, Priority(base - 1));
    } else {
      ASSERT_EQ(p, Priority(base - 1));
    }
    if (base > -1 && base < 9) {
      ASSERT_LT(p, Priority(base + 1));
    } else {
      ASSERT_EQ(p, Priority(base + 1));
    }
  }
}

TEST(InferServerCore, ConstexprPriority) {
  constexpr int base = 6;
  constexpr int offset = 2;
  constexpr int64_t bias = -23521;
  constexpr int64_t major = Priority::ShiftMajor(Priority::BaseToMajor(base));
  constexpr Priority c_p(base);
  ASSERT_EQ(c_p.Get(0), major);
  ASSERT_EQ(c_p.Get(bias), major + bias);
  ASSERT_EQ(Priority::Offset(major, offset), major + Priority::ShiftMajor(offset));
  ASSERT_EQ(Priority::Next(major), Priority::Offset(major, 1));
  ASSERT_EQ(c_p, Priority(base));
  if (base > 0 && base < 10) {
    ASSERT_GT(c_p, Priority(base - 1));
  } else {
    ASSERT_EQ(c_p, Priority(base - 1));
  }
  if (base > -1 && base < 9) {
    ASSERT_LT(c_p, Priority(base + 1));
  } else {
    ASSERT_EQ(c_p, Priority(base + 1));
  }
}