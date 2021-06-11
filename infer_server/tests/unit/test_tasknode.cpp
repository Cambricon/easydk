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

#include "core/engine.h"
#include "core/request_ctrl.h"
#include "infer_server.h"
#include "processor.h"
#include "test_base.h"

namespace infer_server {

class TestProcessor : public ProcessorForkable<TestProcessor> {
 public:
  TestProcessor() noexcept : ProcessorForkable("TestProcessor") { std::cout << "[TestProcessor] Construct\n"; }

  ~TestProcessor() { std::cout << "[TestProcessor] Destruct\n"; }

  Status Process(PackagePtr data) noexcept override {
    std::cout << "[TestProcessor] Process\n";
    if (!initialized_) return Status::ERROR_BACKEND;
    return Status::SUCCESS;
  }

  Status Init() noexcept override {
    std::cout << "[TestProcessor] Init\n";
    initialized_ = true;
    return Status::SUCCESS;
  }

 private:
  bool initialized_{false};
};

TEST(InferServerCore, TaskNode) {
  std::shared_ptr<Processor> proc = std::make_shared<TestProcessor>();
  proc->Init();

  std::promise<void> tasknode_notify_flag;
  auto empty_response_func = [](Status, PackagePtr) {};
  auto empty_notifier_func = [](const RequestControl*) {};
  std::unique_ptr<RequestControl> ctrl(new RequestControl(empty_response_func, empty_notifier_func, "", 1, 2));
  PriorityThreadPool tp(nullptr, 2);

  TaskNode task_node(proc, []() {}, &tp);
  auto end_node = task_node.Fork([&tasknode_notify_flag]() { tasknode_notify_flag.set_value(); });

  task_node.Link(&end_node);

  // TaskNode operator()
  auto input = Package::Create(2);
  input->data[0]->ctrl = ctrl.get();
  input->data[0]->index = 0;
  input->data[1]->ctrl = ctrl.get();
  input->data[1]->index = 1;
  ASSERT_NO_THROW(task_node(input));

  auto tasknode_notify_ret = tasknode_notify_flag.get_future().wait_for(std::chrono::seconds(1));
  ASSERT_EQ(std::future_status::ready, tasknode_notify_ret);
}

}  // namespace infer_server
