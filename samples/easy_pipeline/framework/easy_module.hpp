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

#ifndef SAMPLE_EASY_MODULE_HPP_
#define SAMPLE_EASY_MODULE_HPP_

#include <string>
#include <functional>
#include <memory>

#include "edk_frame.hpp"


class EasyModule {
 public:
  EasyModule(std::string name, int parallelism) : module_name_(name), parallelism_(parallelism) {
  }

  ~EasyModule() = default;

  void SetProcessDoneCallback(std::function<int(std::shared_ptr<EdkFrame>)> callback) {
    callback_ = callback;
  }

  virtual int Transmit(std::shared_ptr<EdkFrame> frame) final {  // NOLINT
    if (callback_) return callback_(frame);
    return 0;
  }

  int GetParallelism() {
    return parallelism_;
  }

  std::string GetModuleName() {
    return module_name_;
  }

 public:
  virtual int Open() = 0;
  virtual int Process(std::shared_ptr<EdkFrame> frame) = 0;
  virtual int Close() = 0;

 private:
  std::string module_name_ = "";
  int parallelism_ = 1;
  std::function<int(std::shared_ptr<EdkFrame>)> callback_;
  std::shared_ptr<EasyModule> next_module_ = nullptr;
};

#endif
