/*************************************************************************
 * Copyright (C) 2020 by Cambricon, Inc. All rights reserved
 *
 * This source code is licensed under the Apache-2.0 license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * A part of this source code is referenced from ctpl project.
 * https://github.com/vit-vit/CTPL/blob/master/ctpl_stl.h
 *
 * Copyright (C) 2014 by Vitaliy Vitsentiy
 *
 * This source code is licensed under the Apache-2.0 license found in the
 * LICENSE file in the root directory of this source tree.
 *
 *************************************************************************/

#include "util/thread_pool.h"

#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace infer_server {

/* ----------------- Implement --------------------- */
template <typename Q, typename T>
void ThreadPool<Q, T>::Resize(size_t n_threads) noexcept {
  if (!is_stop_ && !is_done_) {
    size_t old_n_threads = threads_.size();
    if (old_n_threads <= n_threads) {
      // if the number of threads is increased
      VLOG(3) << "add " << n_threads - old_n_threads << " threads into threadpool";
      threads_.resize(n_threads);
      flags_.resize(n_threads);

      for (size_t i = old_n_threads; i < n_threads; ++i) {
        flags_[i] = std::make_shared<std::atomic<bool>>(false);
        SetThread(i);
      }
    } else {
      // the number of threads is decreased
      VLOG(3) << "stop " << old_n_threads - n_threads << " threads in threadpool, remain " << n_threads << " threads";
      for (size_t i = old_n_threads - 1; i >= n_threads; --i) {
        // this thread will finish
        flags_[i]->store(true);
        threads_[i]->detach();
      }

      {
        // stop the detached threads that were waiting
        cv_.notify_all();
      }

      // safe to delete because the threads are detached
      threads_.resize(n_threads);
      // safe to delete because the threads have copies of shared_ptr of the flags, not originals
      flags_.resize(n_threads);
    }
  }
}

template <typename Q, typename T>
void ThreadPool<Q, T>::Stop(bool wait_all_task_done) noexcept {
  if (!wait_all_task_done) {
    if (is_stop_) return;
    VLOG(3) << "stop all the thread without waiting for remained task done";
    is_stop_ = true;
    for (size_t i = 0, n = this->Size(); i < n; ++i) {
      // command the threads to stop
      flags_[i]->store(true);
    }

    // empty the queue
    this->ClearQueue();
  } else {
    if (is_done_ || is_stop_) return;
    VLOG(3) << "waiting for remained task done before stop all the thread";
    // give the waiting threads a command to finish
    is_done_ = true;
  }

  cv_.notify_all();  // stop all waiting threads

  // wait for the computing threads to finish
  for (size_t i = 0; i < threads_.size(); ++i) {
    if (threads_[i]->joinable()) threads_[i]->join();
  }

  // if there were no threads in the pool but some functors in the queue, the functors are not deleted by the threads
  // therefore delete them here
  this->ClearQueue();
  threads_.clear();
  flags_.clear();
}

template <typename Q, typename T>
void ThreadPool<Q, T>::SetThread(int i) noexcept {
  std::shared_ptr<std::atomic<bool>> tmp(flags_[i]);
  auto f = [this, i, tmp]() {
    std::atomic<bool> &flag = *tmp;
    // init params that bind with thread
    if (thread_init_func_) {
      if (thread_init_func_()) {
        VLOG(5) << "Init thread context success, thread index: " << i;
      } else {
        LOG(ERROR) << "Init thread context failed, but program will continue. "
                      "Program cannot work correctly maybe.";
      }
    }
    task_type t;
    bool have_task = task_q_.TryPop(t);
    while (true) {
      // if there is anything in the queue
      while (have_task) {
        t();
        // params encapsulated in std::function need destruct at once
        t.func = nullptr;
        if (flag.load()) {
          // the thread is wanted to stop, return even if the queue is not empty yet
          return;
        } else {
          have_task = task_q_.TryPop(t);
        }
      }

      // the queue is empty here, wait for the next command
      std::unique_lock<std::mutex> lock(mutex_);
      ++n_waiting_;
      cv_.wait(lock, [this, &t, &have_task, &flag]() {
        have_task = task_q_.TryPop(t);
        return have_task || is_done_ || flag.load();
      });
      --n_waiting_;
      lock.unlock();

      // if the queue is empty and is_done_ == true or *flag then return
      if (!have_task) return;
    }
  };

  threads_[i].reset(new std::thread(f));
}
/* ----------------- Implement END --------------------- */

// instantiate thread pool
template class ThreadPool<TSQueue<Task>>;
template class ThreadPool<ThreadSafeQueue<Task, std::priority_queue<Task, std::vector<Task>, Task::Compare>>>;

}  // namespace infer_server
