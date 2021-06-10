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

#include "infer_server.h"

#include <glog/logging.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "device/mlu_context.h"
#include "model/model.h"
#include "processor.h"
#include "session.h"
#include "util/any.h"
#include "util/env.h"
#include "util/thread_pool.h"

namespace infer_server {

class InferServerPrivate {
 public:
  static InferServerPrivate* Instance(int device_id) {
    // each device has a singleton instance
    static std::mutex map_mutex;
    std::unique_lock<std::mutex> lk(map_mutex);
    static std::unordered_map<int, std::unique_ptr<InferServerPrivate>> server_map;
    if (server_map.find(device_id) == server_map.end()) {
      if (!edk::MluContext().CheckDeviceId(device_id)) {
        return nullptr;
      }
      server_map.emplace(device_id, std::unique_ptr<InferServerPrivate>(new InferServerPrivate(device_id)));
    }
    return server_map[device_id].get();
  }

  ~InferServerPrivate() {}

  bool ExistExecutor(Executor_t executor) noexcept {
    std::unique_lock<std::mutex> lk(executor_map_mutex_);
    return executor_map_.count(executor->GetName());
  }

  Executor_t CreateExecutor(const SessionDesc& desc) noexcept {
    std::ostringstream ss;
    ss << desc.model->Path() << "_" << desc.model->FunctionName() << "_" << desc.preproc->TypeName() << "_"
       << desc.postproc->TypeName();
    std::string executor_name = ss.str();
    std::unique_lock<std::mutex> lk(executor_map_mutex_);
    if (executor_map_.count(executor_name)) {
      VLOG(3) << "executor already exist: " << executor_name;
      return executor_map_[executor_name].get();
    }
    VLOG(3) << "create executor: " << executor_name;
    try {
      SessionDesc executor_desc = desc;
      executor_desc.name = executor_name;
      std::unique_ptr<Executor> executor_up{new Executor(std::move(executor_desc), tp_.get(), device_id_)};
      Executor_t executor = executor_up.get();
      /* executor_map_.insert({executor_name, std::move(executor_up)}); */
      executor_map_[executor_name].swap(executor_up);
      lk.unlock();
      std::unique_lock<std::mutex> tp_lk(tp_mutex_);
      size_t thread_num = tp_->Size();
      static size_t max_thread_num = 3 * GetCpuCoreNumber();
      if (thread_num < max_thread_num) {
        tp_->Resize(std::min(thread_num + 3 * desc.engine_num, max_thread_num));
      }
      tp_lk.unlock();
      return executor;
    } catch (std::runtime_error& e) {
      LOG(ERROR) << e.what();
      return nullptr;
    }
  }

  void CheckAndDestroyExecutor(Session_t session, Executor_t executor) noexcept {
    CHECK(executor) << "Executor is null!";
    CHECK(session) << "Session is null!";
    std::unique_lock<std::mutex> lk(executor_map_mutex_);
    executor->Unlink(session);
    delete session;

    // delete executor while there's no session linked to it
    if (!executor->GetSessionNum()) {
      auto name = executor->GetName();
      if (executor_map_.count(name)) {
        auto th_num = 3 * executor->GetEngineNum();
        VLOG(3) << "destroy executor: " << name;
        executor_map_.erase(name);
        lk.unlock();
        // shrink to fit task load
        std::unique_lock<std::mutex> tp_lk(tp_mutex_);
        if (tp_->IdleNumber() > th_num) {
          VLOG(3) << "Reduce thread in pool after destroy executor";
          tp_->Resize(tp_->Size() - th_num);
        }
        tp_lk.unlock();
      } else {
        CHECK(false) << "executor does not belong to this InferServer";
      }
    }
  }

  PriorityThreadPool* GetThreadPool() noexcept { return tp_.get(); }
  int GetDeviceId() const noexcept { return device_id_; }

 private:
  explicit InferServerPrivate(int device_id) noexcept : device_id_(device_id) {
    tp_.reset(new PriorityThreadPool([device_id]() -> bool {
      try {
        edk::MluContext ctx;
        ctx.SetDeviceId(device_id);
        ctx.BindDevice();
        return true;
      } catch (edk::Exception& e) {
        LOG(ERROR) << "Init thread context failed, error: " << e.what();
        return false;
      }
    }));
  }
  InferServerPrivate(const InferServerPrivate&) = delete;
  const InferServerPrivate& operator=(const InferServerPrivate&) = delete;

  std::map<std::string, std::unique_ptr<Executor>> executor_map_;
  std::mutex executor_map_mutex_;
  std::mutex tp_mutex_;
  std::unique_ptr<PriorityThreadPool> tp_{nullptr};
  int device_id_;
};  // class InferServerPrivate

std::string ToString(BatchStrategy s) noexcept {
  switch (s) {
    case BatchStrategy::DYNAMIC:
      return "BatchStrategy::DYNAMIC";
    case BatchStrategy::STATIC:
      return "BatchStrategy::STATIC";
    case BatchStrategy::SEQUENCE:
      return "BatchStrategy::SEQUENCE";
    case BatchStrategy::STRATEGY_COUNT:
      return "BatchStrategy::STRATEGY_COUNT";
    default:
      return "Unknown";
  }
}

InferServer::InferServer(int device_id) noexcept { priv_ = InferServerPrivate::Instance(device_id); }

Session_t InferServer::CreateSession(SessionDesc desc, std::shared_ptr<Observer> observer) noexcept {
  CHECK(desc.model) << "model is null!";
  CHECK(desc.preproc) << "preproc is null!";

  // won't check postproc, use empty postproc function and output ModelIO by default
  if (!desc.postproc) {
    LOG(WARNING) << "Postprocessor not set, use empty postprocessor by default";
    desc.postproc = std::make_shared<Postprocessor>();
  }

  Executor_t executor = priv_->CreateExecutor(desc);
  if (!executor) return nullptr;

  auto session = new Session(desc.name, executor, !(observer), desc.show_perf);
  if (observer) {
    // async link
    session->SetObserver(std::move(observer));
  }
  executor->Link(session);
  return session;
}

bool InferServer::DestroySession(Session_t session) noexcept {
  CHECK(session) << "Session is null!";
  Executor_t executor = session->GetExecutor();
  if (!priv_->ExistExecutor(executor)) {
    LOG(WARNING) << "session does not belong to this InferServer";
    return false;
  }

  priv_->CheckAndDestroyExecutor(session, executor);
  return true;
}

bool InferServer::Request(Session_t session, PackagePtr input, any user_data, int timeout) noexcept {
  CHECK(session) << "Session is null!";
  CHECK(input) << "input is null!";
  if (session->IsSyncLink()) {
    LOG(ERROR) << "sync LinkHandle cannot be invoked with async api";
    return false;
  }
  if (!input->data.empty() && !session->GetExecutor()->WaitIfCacheFull(timeout)) {
    LOG(WARNING) << session->GetName() << "] Session is busy, request timeout";
    return false;
  }

  return session->Send(std::move(input), std::bind(&Observer::Response, session->GetRawObserver(),
                                                   std::placeholders::_1, std::placeholders::_2, std::move(user_data)));
}

bool InferServer::RequestSync(Session_t session, PackagePtr input, Status* status, PackagePtr output,
                              int timeout) noexcept {
  CHECK(session) << "Session is null!";
  CHECK(input) << "input is null!";
  CHECK(output) << "output is null!";
  CHECK(status) << "status is null!";
  if (!session->IsSyncLink()) {
    LOG(ERROR) << "async Session cannot be invoked with sync api";
    return false;
  }
  if (input->data.empty()) {
    LOG(ERROR) << "Sync request do not support empty package";
    *status = Status::INVALID_PARAM;
    return false;
  }

  std::promise<void> done;
  std::future<void> flag = done.get_future();

  auto wait_start = std::chrono::steady_clock::now();
  if (!session->GetExecutor()->WaitIfCacheFull(timeout)) {
    LOG(WARNING) << session->GetName() << "] Session is busy, request timeout";
    *status = Status::TIMEOUT;
    return false;
  }

  if (timeout > 0) {
    std::chrono::duration<double, std::milli> wait_time = std::chrono::steady_clock::now() - wait_start;
    timeout = timeout - wait_time.count();
    if (timeout < 1) {
      LOG(WARNING) << session->GetName() << "] Session is busy, request timeout";
      *status = Status::TIMEOUT;
      return false;
    }
  }

  // FIXME(dmh): maybe data race here
  // thread1: timeout ->          -> discard -> status and output deleted in user space
  // thread2:         -> response                                                       -> *output = *data -> boom
  RequestControl* ctrl = session->Send(std::move(input), [&output, status, &done](Status s, PackagePtr data) {
    *status = s;
    *output = *data;
    done.set_value();
  });
  if (!ctrl) return false;
  if (timeout > 0) {
    if (flag.wait_for(std::chrono::milliseconds(timeout)) == std::future_status::timeout) {
      ctrl->Discard();
      *status = Status::TIMEOUT;
      LOG(WARNING) << "InferServer process timeout, discard this request";
    }
  } else {
    flag.wait();
  }
  return true;
}

ModelPtr InferServer::GetModel(Session_t session) noexcept {
  CHECK(session) << "Session is null!";
  return session->GetExecutor()->GetModel();
}

void InferServer::WaitTaskDone(Session_t session, const std::string& tag) noexcept {
  CHECK(session) << "Session is null!";
  session->WaitTaskDone(tag);
}

void InferServer::DiscardTask(Session_t session, const std::string& tag) noexcept {
  CHECK(session) << "Session is null!";
  session->DiscardTask(tag);
}

bool InferServer::SetModelDir(const std::string& model_dir) noexcept {
  // check whether model dir exist
  if (access(model_dir.c_str(), F_OK) == 0) {
    ModelManager::Instance()->SetModelDir(model_dir);
    return true;
  }
  return false;
}

ModelPtr InferServer::LoadModel(const std::string& uri, const std::string& func_name) noexcept {
  return ModelManager::Instance()->Load(uri, func_name);
}
ModelPtr InferServer::LoadModel(void* mem_cache, const std::string& func_name) noexcept {
  return ModelManager::Instance()->Load(mem_cache, func_name);
}
bool InferServer::UnloadModel(ModelPtr model) noexcept { return ModelManager::Instance()->Unload(model); }

void InferServer::ClearModelCache() noexcept { ModelManager::Instance()->ClearCache(); }

#ifdef CNIS_RECORD_PERF
std::map<std::string, LatencyStatistic> InferServer::GetLatency(Session_t session) const noexcept {
  return session->GetPerformance();
}

ThroughoutStatistic InferServer::GetThroughout(Session_t session) const noexcept { return session->GetThroughout(); }

ThroughoutStatistic InferServer::GetThroughout(Session_t session, const std::string& tag) const noexcept {
  return session->GetThroughout(tag);
}
#else
std::map<std::string, LatencyStatistic> InferServer::GetLatency(Session_t session) const noexcept { return {}; }
ThroughoutStatistic InferServer::GetThroughout(Session_t session) const noexcept { return {}; }
ThroughoutStatistic InferServer::GetThroughout(Session_t session, const std::string& tag) const noexcept { return {}; }
#endif

}  // namespace infer_server
