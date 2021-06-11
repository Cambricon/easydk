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

#include "session.h"

#include <list>
#include <utility>

#include "engine.h"
#include "infer_server.h"
#include "processor.h"
#include "profile.h"

namespace infer_server {

Executor::Executor(SessionDesc desc, PriorityThreadPool* tp, int device_id)
    : desc_(desc), tp_(tp), device_id_(device_id) {
  CHECK(tp);
  CHECK_GE(device_id, 0);
  CHECK_GT(desc.engine_num, 0u) << "engine number cannot be 0";
  CHECK(desc.preproc) << "preprocess cannot be null";

  // init processors
  auto predictor = std::make_shared<Predictor>();
  desc.preproc->SetParams("model_info", desc.model, "device_id", device_id_, "host_input_layout",
                          desc.host_input_layout);
  if (desc.preproc->Init() != Status::SUCCESS)
    throw std::runtime_error(desc.preproc->TypeName() + "] Init processors failed");

  predictor->SetParams("model_info", desc.model, "device_id", device_id_);
  if (predictor->Init() != Status::SUCCESS)
    throw std::runtime_error(predictor->TypeName() + "] Init processors failed");

  desc.postproc->SetParams("model_info", desc.model, "device_id", device_id_, "host_output_layout",
                           desc.host_output_layout);
  if (desc.postproc->Init() != Status::SUCCESS)
    throw std::runtime_error(desc.postproc->TypeName() + "] Init processors failed");

  // init engines
  auto notify_done_func = [this](Engine* idle) {
    idle_.store(idle);
    dispatch_cond_.notify_one();
  };
  engines_.reserve(desc.engine_num);
  engines_.emplace_back(new Engine({desc.preproc, predictor, desc.postproc}, std::move(notify_done_func), tp_));
  for (size_t e_idx = 1; e_idx < desc.engine_num; ++e_idx) {
    engines_.emplace_back(engines_[0]->Fork());
  }
  idle_.store(engines_[0].get());

  // TODO(dmh): 3 is number of processors, refactor to adjustable
  max_processing_num_ = 2 * desc.engine_num * 3 * desc.model->BatchSize();
  // init cache
  if (desc.strategy == BatchStrategy::DYNAMIC) {
    cache_.reset(
        new CacheDynamic(desc_.model->BatchSize(), Priority(desc.priority), desc_.batch_timeout));
  } else if (desc.strategy == BatchStrategy::STATIC) {
    cache_.reset(new CacheStatic(desc_.model->BatchSize(), Priority(desc.priority)));
  } else {
    CHECK(false) << "Unsupport BatchStraregy";
  }
  cache_->Start();

  dispatch_thread_ = std::thread(&Executor::DispatchLoop, this);
}

Executor::~Executor() {
  std::unique_lock<std::mutex> lk(link_mutex_);
  for (auto& session : link_set_) {
    delete session;
  }
  link_set_.clear();
  lk.unlock();
  cache_->Stop();
  VLOG(3) << desc_.name << "] Processed Task:\n\t"
          << " | total " << static_cast<uint32_t>(batch_record_.total) << " | batch number " << batch_record_.unit_cnt
          << " | average tasks per batch " << batch_record_.total / batch_record_.unit_cnt;
  // dispatch thread won't quit until cache is empty
  dispatch_thread_.join();
  cache_.reset();
  CHECK(link_set_.empty()) << "Executor should not have any session in destructor";
  idle_.store(nullptr);
  engines_.clear();
}

void Executor::DispatchLoop() noexcept {
  std::unique_lock<std::mutex> dispatch_lk(dispatch_mutex_, std::defer_lock);
  bool waited = false;
  while (true) {
    // get package from cache
    PackagePtr pack = cache_->Pop();
    if (!pack) {
      if (!cache_->Running()) break;
      continue;
    }
    size_t batch_size = pack->data.size();
    batch_record_.unit_cnt += 1;
    batch_record_.total += batch_size;

    // dispatch to engine
    if (!idle_) {
      waited = true;
      dispatch_lk.lock();
      dispatch_cond_.wait(dispatch_lk, [this]() -> bool { return idle_; });
      dispatch_lk.unlock();
    }
    VLOG(4) << desc_.name << "] dispatch to engine " << idle_;
    idle_.load()->Run(std::move(pack));
    idle_.store(nullptr);

    // find idle engine
    if (!waited) {
      for (auto& it : engines_) {
        if (it->IsIdle()) {
          idle_.store(it.get());
          break;
        }
      }
    }
  }
}

// constexpr is not inline in C++11
constexpr uint32_t Profiler::period_interval_;

RequestControl* Session::Send(PackagePtr&& pack, std::function<void(Status, PackagePtr)>&& response) noexcept {
  if (!running_.load()) {
    LOG(ERROR) << "Session not running [" << name_;
    return nullptr;
  }

  if (pack->predict_io && pack->predict_io->HasValue()) {
    if (executor_->GetDesc().strategy != BatchStrategy::STATIC) {
      LOG(ERROR) << "Input continuous data to skip preprocess is only supported under BatchStrategy::STATIC";
      return nullptr;
    }
    if (pack->data.size() > executor_->GetModel()->BatchSize()) {
      LOG(ERROR) << "Input continuous data to skip preprocess is only supported when data number <= model batch size";
      return nullptr;
    }
  }
  // since cannot classify data size from continuous data,
  // we use batch_size set in package instead of size of pack->data
  size_t data_size = pack->data.size();

#ifdef CNIS_RECORD_PERF
  profiler_.RequestStart(pack->tag);
#endif
  std::unique_lock<std::mutex> lk(request_mutex_);
  RequestControl* ctrl =
      new RequestControl(std::move(response), std::bind(&Session::CheckAndResponse, this, std::placeholders::_1),
                         pack->tag, request_id_++, data_size);
#ifdef CNIS_RECORD_PERF
  ctrl->BeginRecord();
#endif
  for (size_t index = 0; index < pack->data.size(); ++index) {
    pack->data[index]->ctrl = ctrl;
    pack->data[index]->index = index;
  }
  request_list_.push_back(ctrl);
  lk.unlock();

  if (data_size) {
    CHECK(executor_->Upload(std::move(pack))) << "Cache should be running";
  } else {
    VLOG(3) << "session: " << name_ << " | No data in package with tag [" << pack->tag << "]";
    CheckAndResponse(ctrl);
  }
  return ctrl;
}

void Session::CheckAndResponse(const RequestControl* caller) noexcept {
  std::unique_lock<std::mutex> lk(request_mutex_);
  RequestControl* ctrl;

  // check request finished processing
  if (request_list_.empty()) {
    VLOG(3) << "No request in this Session " << name_ << this;
    // notify blocked thread by destructor
    sync_cond_.notify_one();
    return;
  }
  ctrl = request_list_.front();
  if (caller != ctrl && !ctrl->IsProcessFinished()) {
    return;
  }

  bool expected = false;
  if (!in_response_.compare_exchange_strong(expected, true, std::memory_order_release, std::memory_order_relaxed)) {
    return;
  }
  request_list_.pop_front();
  lk.unlock();
  int64_t priority = Priority::Offset(executor_->GetPriority().Get(-ctrl->RequestId()), 5);
  executor_->GetThreadPool()->VoidPush(priority, [ctrl, this] {
    auto next = ctrl;
    do {
#ifdef CNIS_RECORD_PERF
      profiler_.RequestEnd(next->Tag(), next->DataNum());
#endif
      if (!next->IsDiscarded()) {
#ifdef CNIS_RECORD_PERF
        for (auto& it : next->Performance()) {
          recorder_.RecordPerformance(it.first, next->DataNum(), it.second);
        }
        recorder_.RecordPerformance("RequestLatency", 1, next->EndRecord());
#endif
        next->Response();
      }
      delete next;
      next = nullptr;

      std::unique_lock<std::mutex> lk(request_mutex_);
      if (request_list_.empty()) {
        in_response_.store(false);
        sync_cond_.notify_one();
        return;
      }
      next = request_list_.front();
      if (next->IsProcessFinished()) {
        request_list_.pop_front();
      } else {
        next = nullptr;
      }
    } while (next);
    in_response_.store(false);
  });
}

}  // namespace infer_server
