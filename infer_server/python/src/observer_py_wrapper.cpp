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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>

#include "cnis/infer_server.h"

namespace py = pybind11;

namespace infer_server {

class PyObserver : public Observer {
 public:
  void Response(Status status, PackagePtr data, any user_data) noexcept override {
    PYBIND11_OVERRIDE_PURE(void, Observer, Response, status, data, user_data);
  }
};  // class PyObserver
class CustomObserver : public Observer {
 public:
  virtual void ResponseFunc(Status status, PackagePtr data, py::dict user_data) = 0;
  void Response(Status status, PackagePtr data, any user_data) noexcept override {
    py::dict dict = *(any_cast<std::shared_ptr<py::dict>>(user_data));
    ResponseFunc(status, data, dict);
  }
};  // CustomObserver

class PyCustomObserver : public CustomObserver {
 public:
  using CustomObserver::CustomObserver;
  void ResponseFunc(Status status, PackagePtr data, py::dict user_data) noexcept override {
    PYBIND11_OVERRIDE_PURE(void, CustomObserver, response_func, status, data, user_data);
  }
  void Response(Status status, PackagePtr data, any user_data) noexcept override {
    PYBIND11_OVERRIDE(void, CustomObserver, Response, status, data, user_data);
  }
};  // class PyCustomObserver

void ObserverWrapper(const py::module &m) {
  py::class_<Observer, PyObserver, std::shared_ptr<Observer>>(m, "ObserverBase").def(py::init<>());
  py::class_<CustomObserver, PyCustomObserver, Observer, std::shared_ptr<CustomObserver>>(m, "Observer")
      .def(py::init<>())
      .def("response", &CustomObserver::Response);
}

}  //  namespace infer_server
