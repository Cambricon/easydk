/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
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

#ifndef SAMPLE_EASY_PIPELINE_HPP_
#define SAMPLE_EASY_PIPELINE_HPP_

#include <atomic>
#include <chrono>
#include <algorithm>
#include <functional>
#include <memory>
#include <list>
#include <set>

#include <iostream>
#include <thread>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "easy_module.hpp"
#include "threadsafe_queue.h"


// template<typename T>
using FrameQueue = ThreadSafeQueue<std::shared_ptr<EdkFrame>>;
struct NodeContext {
  std::string module_name;
  std::shared_ptr<EasyModule> module;
  std::vector<FrameQueue*> input_queues = {};
  std::shared_ptr<NodeContext> next = nullptr;
  std::vector<std::thread> threads;
  std::map<int, bool> stream_process_map;
};


class EasyPipeline {
 public:
  EasyPipeline() = default;
  ~EasyPipeline();
  int AddSource(std::shared_ptr<EasyModule> module);
  int AddModule(std::shared_ptr<EasyModule> module);
  int AddLink(std::string current, std::string next);
  int Start();
  void Stop();
  void WaitForStop();

 private:
  int BuildEasyPipeline();
  int ProcessFrameEos(std::shared_ptr<NodeContext> node, std::shared_ptr<EdkFrame> frame);
  void Taskloop(std::shared_ptr<NodeContext> node, int num);
  std::shared_ptr<NodeContext> FindNodeByName(std::string name);

 private:
  std::condition_variable wakener_;
  std::mutex wakener_mutex_;
  std::atomic<bool> pipe_started_{false};
  std::atomic<bool> running_{false};
  std::vector<std::shared_ptr<NodeContext>> sinks_{};
  std::vector<std::shared_ptr<NodeContext>> sources_{};
  std::atomic<bool> source_added_{false};
  std::vector<std::shared_ptr<NodeContext>> nodes_{};
};


#endif
