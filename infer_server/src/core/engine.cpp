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

#include "engine.h"

#include <glog/logging.h>

#include <map>
#include <utility>
#include <vector>

#include "profile.h"
#include "request_ctrl.h"
#include "session.h"

namespace infer_server {

void TaskNode::operator()(PackagePtr pack) noexcept {
  Status s;
#if defined(CNIS_RECORD_PERF) && (!defined(NDEBUG))
  auto before_lock = Clock::Now();
#endif
  std::unique_lock<std::mutex> lk = processor_->Lock();
#ifdef CNIS_RECORD_PERF
  auto start = Clock::Now();
#endif
  s = processor_->Process(pack);
  lk.unlock();
  const std::string& type_name = processor_->TypeName();
#ifdef CNIS_RECORD_PERF
  auto end = Clock::Now();
  pack->perf[type_name] = Clock::Duration(start, end);
#ifndef NDEBUG
  pack->perf["-WaitLock-" + type_name] = Clock::Duration(before_lock, start);
#endif
#endif
  if (s != Status::SUCCESS) {
    LOG(ERROR) << "[" << type_name << "] processor execute failed";
    for (auto& it : pack->data) {
      it->ctrl->ProcessFailed(s);
    }
    done_notifier_();
  } else {
    VLOG(6) << "Transmit data for " << type_name;
    Transmit(std::move(pack));
  }
}

void TaskNode::Transmit(PackagePtr&& pack) noexcept {
  if (downnode_) {
    // start next processor
    pack->priority = Priority::Next(pack->priority);
    // TODO(dmh): copy TaskNode for each task transmit?
    tp_->VoidPush(pack->priority, *downnode_, std::forward<PackagePtr>(pack));
  } else {
    std::map<std::string, float> perf{};
#ifdef CNIS_RECORD_PERF
    for (auto& it : pack->perf) {
      perf[it.first] = it.second / pack->data.size();
    }
#endif
    // tail of process, response to user
    for (auto& it : pack->data) {
      // SUCCESS flag won't cover errors happended before
      it->ctrl->ProcessDone(Status::SUCCESS, it, it->index, perf);
    }
    done_notifier_();
  }
}

Engine::Engine(std::vector<std::shared_ptr<Processor>> processors, const NotifyDoneFunc& done_func,
               PriorityThreadPool* tp)
    : done_notifier_(std::move(done_func)), tp_(tp) {
  nodes_.reserve(processors.size());
  for (size_t idx = 0; idx < processors.size(); ++idx) {
    nodes_.emplace_back(
        processors[idx],
        [this]() {
          --task_num_;
          done_notifier_(this);
        },
        tp_);
  }
  for (size_t idx = 0; idx < nodes_.size() - 1; ++idx) {
    nodes_[idx].Link(&nodes_[idx + 1]);
  }
}

std::unique_ptr<Engine> Engine::Fork() {
  auto fork_engine = new Engine;
  fork_engine->tp_ = tp_;
  fork_engine->done_notifier_ = done_notifier_;
  fork_engine->nodes_.reserve(nodes_.size());
  for (auto& it : nodes_) {
    fork_engine->nodes_.emplace_back(it.Fork([fork_engine]() {
      --fork_engine->task_num_;
      fork_engine->done_notifier_(fork_engine);
    }));
  }
  for (size_t idx = 0; idx < fork_engine->nodes_.size() - 1; ++idx) {
    fork_engine->nodes_[idx].Link(&fork_engine->nodes_[idx + 1]);
  }
  return std::unique_ptr<Engine>(fork_engine);
}

}  // namespace infer_server
