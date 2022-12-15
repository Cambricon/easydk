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
#include "pybind11/pybind11.h"

#include "cnis/infer_server.h"
#include "edk_config.h"

namespace py = pybind11;

namespace infer_server {

void InferServerWrapper(const py::module &m);
void PackageWrapper(const py::module &m);
void StatusWrapper(const py::module &m);
void DataLayoutWrapper(py::module *m);
void BatchStrategyWrapper(const py::module &m);
void DeviceWrapper(py::module *m);

void ShapeWrapper(const py::module &m);
void SessionDescWrapper(const py::module &m);
void ObserverWrapper(const py::module &m);
void ModelInfoWrapper(const py::module &m);
void ProcessorWrapper(py::module *m);
void BufferSurfaceWrapper(py::module *m);

void PerfWrapper(py::module *m);
void SampleWrapper(const py::module &m);
void PlatformWrapper(py::module *m);

PYBIND11_MODULE(cnis, m) {
  m.doc() = "Cambricon Infer Server python api";
  m.def("version", &cnedk::Version);
  InferServerWrapper(m);
  PackageWrapper(m);
  StatusWrapper(m);
  DataLayoutWrapper(&m);
  BatchStrategyWrapper(m);
  DeviceWrapper(&m);

  ShapeWrapper(m);
  SessionDescWrapper(m);
  ObserverWrapper(m);
  ModelInfoWrapper(m);
  ProcessorWrapper(&m);
  BufferSurfaceWrapper(&m);

  PerfWrapper(&m);
  SampleWrapper(m);
  PlatformWrapper(&m);
}

}  // namespace infer_server
