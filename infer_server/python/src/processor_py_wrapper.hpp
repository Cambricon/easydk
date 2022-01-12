/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#ifndef PROCESSOR_PY_WRAPPER_HPP
#define PROCESSOR_PY_WRAPPER_HPP

#include <pybind11/pybind11.h>

#include <memory>

#include "cnis/processor.h"

namespace py = pybind11;

namespace infer_server {

class PyProcessor : public Processor {
 public:
  using Processor::Processor;
  Status Init() noexcept override {
    PYBIND11_OVERRIDE_PURE(
        Status,
        Processor,
        Init);
  }
  Status Process(PackagePtr data) noexcept override {
    PYBIND11_OVERRIDE_PURE(
        Status,
        Processor,
        Process,
        data);
  }
  std::shared_ptr<Processor> Fork() override {
    PYBIND11_OVERRIDE_PURE(
        std::shared_ptr<Processor>,
        Processor,
        Fork);
  }
};  // class PyProcessor

}  //  namespace infer_server

#endif  // PROCESSOR_PY_WRAPPER_HPP
