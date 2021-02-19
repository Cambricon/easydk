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

#ifndef INFER_SERVER_CORE_ENGINE_H_
#define INFER_SERVER_CORE_ENGINE_H_

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "infer_server.h"
#include "util/thread_pool.h"

namespace infer_server {

class Engine;
class TaskNode {
 public:
  using Notifier = std::function<void()>;
  TaskNode(std::shared_ptr<Processor> processor, Notifier&& done_notifier, PriorityThreadPool* tp) noexcept
      : processor_(processor), done_notifier_(std::forward<Notifier>(done_notifier)), tp_(tp) {}

  TaskNode Fork(Notifier&& done_notifier) {
    auto fork_proc = processor_->Fork();
    if (!fork_proc) throw std::runtime_error("Fork processor failed: " + processor_->TypeName());
    return TaskNode(std::move(fork_proc), std::forward<Notifier>(done_notifier), tp_);
  }

  void operator()(PackagePtr pack) noexcept;

  void Transmit(PackagePtr&& data) noexcept;

  void Link(TaskNode* node) noexcept { downnode_ = node; }

 private:
  TaskNode() = delete;
  std::shared_ptr<Processor> processor_;
  Notifier done_notifier_;
  PriorityThreadPool* tp_;
  TaskNode* downnode_{nullptr};
};  // struct TaskNode

class Engine {
 public:
  using NotifyDoneFunc = std::function<void(Engine*)>;
  Engine() = default;
  Engine(std::vector<std::shared_ptr<Processor>> processors, const NotifyDoneFunc& done_func, PriorityThreadPool* tp);
  ~Engine() {
    while (task_num_.load()) {
      // wait for all task done
    }
  }

  std::unique_ptr<Engine> Fork();

  void Run(PackagePtr&& package) noexcept {
    ++task_num_;
    tp_->VoidPush(package->priority, nodes_[0], std::forward<PackagePtr>(package));
  }

  bool IsIdle() noexcept { return task_num_.load() < nodes_.size(); }

  size_t MaxLoad() noexcept { return nodes_.size(); }

 private:
  std::vector<TaskNode> nodes_;
  NotifyDoneFunc done_notifier_;
  PriorityThreadPool* tp_;
  std::atomic<uint32_t> task_num_{0};
};  // class Engine

}  // namespace infer_server

#endif  // INFER_SERVER_CORE_ENGINE_H_
