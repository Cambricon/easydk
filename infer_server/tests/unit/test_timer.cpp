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

#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "util/timer.h"

TEST(InferServerUtil, Timer) {
  infer_server::Timer t1;
  auto task = [](const std::string& msg) { std::cout << msg << "\n"; };
  EXPECT_TRUE(t1.NotifyEvery(10, task, "timer1: print every 10ms"));
  EXPECT_FALSE(t1.NotifyAfter(10, task, "timer1: this line should not be printed"));
  infer_server::Timer t2;
  EXPECT_TRUE(t2.NotifyEvery(30, task, "timer2: print every 30ms"));
  infer_server::Timer t3;
  std::promise<void> pro1;
  auto start = std::chrono::steady_clock::now();
  int wait_time = 100;
  EXPECT_TRUE(t3.NotifyAfter(wait_time, [&pro1]() {
    std::cout << "timer3: after 100 ms" << std::endl;
    pro1.set_value();
  }));
  pro1.get_future().get();
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> dura = end - start;
  EXPECT_GE(dura.count(), wait_time);
  EXPECT_NEAR(dura.count(), wait_time, 1);
  std::cout << "cancel timer1 and timer2\n";
  t1.Cancel();
  t2.Cancel();

  std::cout << "timer1: will say 'I'm back' after 5ms\n";
  std::promise<void> pro2;
  wait_time = 5;
  start = std::chrono::steady_clock::now();
  EXPECT_TRUE(t1.NotifyAfter(wait_time, [&pro2]() {
    std::cout << "timer1: I'm back\n";
    pro2.set_value();
  }));
  EXPECT_TRUE(t2.NotifyAfter(wait_time - 1, task, "timer2: this line should not be printed"));
  t2.Cancel();
  EXPECT_TRUE(t2.NotifyAfter(0, task, "timer2: speak at once"));
  pro2.get_future().get();
  end = std::chrono::steady_clock::now();
  dura = end - start;
  EXPECT_GE(dura.count(), wait_time);
  EXPECT_NEAR(dura.count(), wait_time, 1);

  std::promise<void> pro3;
  wait_time = 26;
  start = std::chrono::steady_clock::now();
  EXPECT_TRUE(t1.NotifyAfter(wait_time, [&pro3]() {
    std::cout << "timer1: final print after 26ms" << std::endl;
    pro3.set_value();
  }));
  EXPECT_TRUE(t2.NotifyEvery(5, task, "timer2: print every 5ms"));
  pro3.get_future().get();
  end = std::chrono::steady_clock::now();
  dura = end - start;
  EXPECT_GE(dura.count(), wait_time);
  EXPECT_NEAR(dura.count(), wait_time, 1);
}
