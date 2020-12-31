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

#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "buffer.h"
#include "fixture.h"

TEST_F(InferServerTestAPI, Buffer) {
  int times = 100;
  char *raw_str = new char[20 * 100 * 16];
  char *raw_str_out = new char[20 * 100 * 16];
  infer_server::Buffer str(20 * 100 * 16);
  infer_server::Buffer str_out(20 * 100 * 16);
  EXPECT_FALSE(str.OnMlu());
  EXPECT_FALSE(str_out.OnMlu());
  while (--times) {
    try {
      size_t str_size = 170 * times;
      snprintf(raw_str, str_size, "test MluMemory, s: %lu", str_size);
      infer_server::Buffer mlu_src(str_size, device_id_);
      infer_server::Buffer mlu_dst(str_size, device_id_);
      EXPECT_FALSE(mlu_src.OwnMemory());
      EXPECT_FALSE(mlu_dst.OwnMemory());
      (void)mlu_src.MutableData();
      (void)mlu_dst.MutableData();
      EXPECT_TRUE(mlu_src.OwnMemory());
      EXPECT_TRUE(mlu_dst.OwnMemory());
      EXPECT_TRUE(mlu_src.OnMlu());
      EXPECT_TRUE(mlu_dst.OnMlu());
      str.CopyFrom(raw_str, str_size);
      mlu_src.CopyFrom(str, str_size);
      mlu_dst.CopyFrom(mlu_src, str_size);
      mlu_dst.CopyTo(&str_out, str_size);
      str_out.CopyTo(raw_str_out, str_size);
      EXPECT_STREQ(raw_str, raw_str_out);
    } catch (edk::Exception &err) {
      EXPECT_TRUE(false) << err.what();
    }
  }
  delete[] raw_str;
  delete[] raw_str_out;
}

TEST_F(InferServerTestAPI, MluMemoryPoolBuffer) {
  int times = 100;
  constexpr size_t kStrLength = 20 * 100 * 16;
  constexpr size_t kBufferNum = 6;
  char *str = new char[kStrLength];
  char *str_out = new char[kStrLength];

  infer_server::MluMemoryPool pool(kStrLength, kBufferNum, device_id_);

  {
    std::vector<infer_server::Buffer> cache;
    for (size_t i = 0; i < kBufferNum; ++i) {
      EXPECT_NO_THROW(cache.emplace_back(pool.Request()));
    }
    EXPECT_THROW(pool.Request(10), edk::Exception) << "pool should be empty";
    cache.clear();
    EXPECT_NO_THROW(pool.Request(10));
  }

  // test destruct
  std::future<void> res2;
  {
    infer_server::MluMemoryPool p(kStrLength, kBufferNum, device_id_);
    std::promise<void> flag;
    std::future<void> has_requested = flag.get_future();
    res2 = std::async(std::launch::async, [&p, &flag]() {
      infer_server::Buffer m;
      EXPECT_NO_THROW(m = p.Request());
      flag.set_value();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    has_requested.get();
  }
  res2.get();

  while (--times) {
    try {
      size_t a_number = 170 * times;
      snprintf(str, kStrLength, "test MluMemory, s: %lu", a_number);
      void *in = reinterpret_cast<void *>(str);
      void *out = reinterpret_cast<void *>(str_out);

      infer_server::Buffer mlu_src, mlu_dst;
      EXPECT_FALSE(mlu_src.OwnMemory());
      EXPECT_FALSE(mlu_dst.OwnMemory());
      EXPECT_NO_THROW(mlu_src = pool.Request(10));
      EXPECT_NO_THROW(mlu_dst = pool.Request(10));
      EXPECT_TRUE(mlu_src.OwnMemory());
      EXPECT_TRUE(mlu_dst.OwnMemory());
      mlu_src.CopyFrom(in, kStrLength);
      mlu_dst.CopyFrom(mlu_src, kStrLength);
      mlu_dst.CopyTo(out, kStrLength);
      EXPECT_STREQ(str, str_out);
    } catch (edk::Exception &err) {
      EXPECT_TRUE(false) << err.what();
    }
  }
  delete[] str;
  delete[] str_out;
}
