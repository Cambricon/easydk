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

#include <memory>
#include <string>

#include "easy_pipeline.hpp"

#include "glog/logging.h"

EasyPipeline::~EasyPipeline() {
  for (size_t i = 0; i < sources_.size(); ++i) {
    if (sources_[i]->threads.size() && sources_[i]->threads[0].joinable()) {
      sources_[i]->threads[0].join();
    }
  }

  for (size_t i = 0; i < sources_.size(); ++i) {
    std::shared_ptr<NodeContext> temp_node = sources_[i]->next;
    while (temp_node) {
      if (temp_node->threads.size()) {
        for (std::thread& it : temp_node->threads) {
          if (it.joinable()) it.join();
        }
        temp_node->threads.clear();
        temp_node->module->Close();  // close module
      }
      temp_node = temp_node->next;
    }
  }
  sources_.clear();

  // clear all sink module
  sinks_.clear();

  // clear all frame queue
  for (size_t i = 0; i < nodes_.size(); ++i) {
    for (size_t j = 0; j < nodes_[i]->input_queues.size(); ++j) {
      delete nodes_[i]->input_queues[j];
    }
    nodes_[i]->input_queues.clear();
  }
  nodes_.clear();
}

int EasyPipeline::AddSource(std::shared_ptr<EasyModule> module) {
  if (!source_added_) {
    source_added_ = true;
  }
  std::shared_ptr<NodeContext> node = std::make_shared<NodeContext>();
  node->module = module;
  node->module_name = module->GetModuleName();
  node->stream_process_map[0] = true;
  sources_.push_back(node);
  return 0;
}

int EasyPipeline::AddModule(std::shared_ptr<EasyModule> module) {
  std::shared_ptr<NodeContext> node = FindNodeByName(module->GetModuleName());
  if (node != nullptr) {
    std::cout << "module is exist" << std::endl;
    return -1;
  }

  node = std::make_shared<NodeContext>();
  node->module = module;
  node->input_queues.reserve(module->GetParallelism());
  for (int i = 0; i < module->GetParallelism(); ++i) {
    FrameQueue* frame_queue = new ThreadSafeQueue<std::shared_ptr<EdkFrame>>();
    node->input_queues.push_back(frame_queue);
  }

  node->module_name = module->GetModuleName();
  nodes_.push_back(node);   // put node to node list
  return 0;
}

int EasyPipeline::AddLink(std::string current, std::string next) {
  bool source_find = false;
  std::shared_ptr<NodeContext> next_node = FindNodeByName(next);

  for (auto& source_iter : sources_) {
    if (source_iter->module_name == current) {
      source_iter->next = next_node;
      source_find = true;
    }
  }
  if (source_find) return 0;

  std::shared_ptr<NodeContext> current_node = FindNodeByName(current);;
  if (current_node && next_node) {
    current_node->next = next_node;
  } else {
    return -1;
  }
  return 0;
}

int EasyPipeline::Start() {
  if (!source_added_) {
    LOG(ERROR) << "[EasyDK Sample] [EasyPipeline] source is not add";
    return -1;
  }
  running_ = true;
  int ret = BuildEasyPipeline();
  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Sample] [EasyPipeline] Build EasyPipeline failed";
    running_ = false;
    return ret;
  }
  pipe_started_ = true;
  return 0;
}

void EasyPipeline::Stop() {
  if (running_ == true) {
    for (size_t i = 0; i < sources_.size(); ++i) {
      sources_[i]->module->Close();  // close module
    }
  }
}

void EasyPipeline::WaitForStop() {
  bool exit = false;
  while (running_) {
    if (exit) break;
    std::unique_lock<std::mutex> lk(wakener_mutex_);
    wakener_.wait_for(lk, std::chrono::milliseconds(1000), [this, &exit]() {
        if (!pipe_started_) {
          exit = true;
          return false;
        }
        for (const auto& source_iter : sources_) {   // check source status
          if (source_iter->stream_process_map[0] == true) {
            return false;
          }
        }
        for (auto& module_iter : sinks_) {
          for (const auto& map_iter : module_iter->stream_process_map) {  // other module as sink module
            if (map_iter.second == true) {
              return false;
            }
          }
        }
        exit = true;
        return true;
      });
    lk.unlock();
  }
  running_ = false;
}

int EasyPipeline::BuildEasyPipeline() {
  int ret;
  for (size_t i = 0; i < sources_.size(); ++i) {
    ret = sources_[i]->module->Open();
    if (ret != 0) {
      LOG(ERROR) << "[EasyDK Sample] [EasyPipeline] Open [" << sources_[i]->module_name << "] failed";
      return -1;
    }
    std::shared_ptr<NodeContext> temp_node = sources_[i]->next;

    while (temp_node) {
      if (temp_node->threads.size()) {
        break;
      } else {
        ret = temp_node->module->Open();
        if (ret != 0) {
          LOG(ERROR) << "[EasyDK Sample] [EasyPipeline] Open [" << temp_node->module_name << "] failed";
          return -1;
        }

        for (int j = 0; j < temp_node->module->GetParallelism(); ++j) {
          temp_node->threads.push_back(std::thread(&EasyPipeline::Taskloop, this, temp_node, j));
        }
        if (temp_node->next == nullptr) {  // save all sink modules
          sinks_.push_back(temp_node);
        }
        temp_node = temp_node->next;
      }
    }
  }
  for (size_t i = 0; i < sources_.size(); ++i) {
    sources_[i]->threads.push_back(std::thread(&EasyPipeline::Taskloop, this, sources_[i], 0));
  }
  return 0;
}

int EasyPipeline::ProcessFrameEos(std::shared_ptr<NodeContext> node, std::shared_ptr<EdkFrame> frame) {
  if (frame->is_eos) {
    if (node->stream_process_map.find(frame->stream_id) != node->stream_process_map.end()) {
      node->stream_process_map[frame->stream_id] = false;
      wakener_.notify_all();
    }
  } else {
    if (node->stream_process_map.find(frame->stream_id) == node->stream_process_map.end()) {
      node->stream_process_map[frame->stream_id] = true;
    }
  }
  return 0;
}

std::shared_ptr<NodeContext> EasyPipeline::FindNodeByName(std::string name) {
  auto node_iter = std::find_if(nodes_.begin(), nodes_.end(),
                                [name](std::shared_ptr<NodeContext> node) { return node->module_name == name;});

  return node_iter == nodes_.end() ? nullptr : *node_iter;
}

void EasyPipeline::Taskloop(std::shared_ptr<NodeContext> node, int num) {
  // std::cout << node->module->GetModuleName() << ", " << node->module->GetParallelism() << std::endl;
  auto send_data = [node, this](std::shared_ptr<EdkFrame> frame) -> int {
    if (node->next) {
      node->next->input_queues[frame->stream_id % node->next->module->GetParallelism()]->Push(frame);
    }
    return 0;
  };

  auto source_send_data = [node, this](std::shared_ptr<EdkFrame> frame) -> int {
    if (node->next) {
      node->next->input_queues[frame->stream_id % node->next->module->GetParallelism()]->Push(frame);
    }

    if (frame->is_eos) {   // source process
      node->stream_process_map[0] = false;
    }
    return 0;
  };

  auto node_iter = std::find(sources_.begin(), sources_.end(), node);

  if (node_iter != sources_.end()) {
    node->module->SetProcessDoneCallback(source_send_data);
  } else {
    node->module->SetProcessDoneCallback(send_data);
  }

  if (node_iter != sources_.end()) {   // source process
    while (running_) {
      node->module->Process(nullptr);
    }
  } else {    // orther node process
    while (running_) {
      std::shared_ptr<EdkFrame> frame = nullptr;
      if (node->input_queues[num]->WaitAndTryPop(frame, std::chrono::microseconds(200))) {
        node->module->Process(frame);
        ProcessFrameEos(node, frame);
      }
    }
  }
}

