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

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "cnedk_platform.h"

namespace py = pybind11;

namespace infer_server {

void PlatformWrapper(py::module *m) {
  m->def("get_platfrom_name", [](int dev_id) -> std::string {
    CnedkPlatformInfo info;
    if (CnedkPlatformGetInfo(dev_id, &info) != 0) {
      return "";
    }
    std::string name(info.name);
    return name;
  }, py::arg("dev_id") = 0);

  m->def("is_edge_platfrom", [](int dev_id) {
    CnedkPlatformInfo info;
    if (CnedkPlatformGetInfo(dev_id, &info) != 0) {
      return false;
    }
    std::string name(info.name);
    if (name.rfind("CE", 0) == 0) {
      return true;
    }
    return false;
  }, py::arg("dev_id") = 0);

  m->def("is_cloud_platfrom", [](int dev_id) {
    CnedkPlatformInfo info;
    if (CnedkPlatformGetInfo(dev_id, &info) != 0) {
      return false;
    }
    std::string name(info.name);
    if (name.rfind("MLU", 0) == 0) {
      return true;
    }
    return false;
  }, py::arg("dev_id") = 0);
}

}  //  namespace infer_server
