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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>

#include "device/mlu_context.h"

namespace py = pybind11;

namespace infer_server {

void MluContextWrapper(py::module m) {  // NOLINT
  py::enum_<edk::CoreVersion>(m, "CoreVersion")
      .value("INVALID", edk::CoreVersion::INVALID)
      .value("MLU220", edk::CoreVersion::MLU220)
      .value("MLU270", edk::CoreVersion::MLU270)
      .value("MLU370", edk::CoreVersion::MLU370)
      .value("CE3226", edk::CoreVersion::CE3226);
  m.def("get_device_core_version", [](int device_id) {
    edk::MluContext mlu_ctx(device_id);
    return mlu_ctx.GetCoreVersion();
  }, py::arg("dev_id") = 0);
}

}  //  namespace infer_server
