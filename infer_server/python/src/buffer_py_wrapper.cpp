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
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <vector>

#include "cnis/buffer.h"
#include "cnis/infer_server.h"
#include "cnis/shape.h"

#include "common_wrapper.hpp"

namespace py = pybind11;

namespace infer_server {

void BufferWrapper(const py::module& m) {
  py::class_<Buffer, std::shared_ptr<Buffer>>(m, "Buffer")
      .def(py::init<size_t>(), py::arg("size"))
      .def(py::init<size_t, int>(), py::arg("size"), py::arg("dev_id"))
      .def(py::init<void*, size_t, Buffer::MemoryDeallocator, int>(), py::arg("src"), py::arg("size"),
           py::arg("deallocator"), py::arg("dev_id"))
      .def(py::init<void*, size_t, Buffer::MemoryDeallocator>(), py::arg("src"), py::arg("size"),
           py::arg("deallocator"))
      .def("data",
           [](Buffer* buf, const Shape& shape, const DataLayout& layout) {
             return PointerToArray(buf->MutableData(), shape, layout);
           },
           py::arg("shape"), py::arg("layout"))
      .def("data",
           [](Buffer* buf, const std::vector<size_t>& shape, const DataType& dtype) {
             if (shape.empty()) {
               size_t data_num = static_cast<size_t>(buf->MemorySize() / GetTypeSize(dtype));
               return PointerToArray(buf->MutableData(), {data_num}, dtype);
             } else {
               return PointerToArray(buf->MutableData(), shape, dtype);
             }
           },
           py::arg("shape") = std::vector<size_t>{}, py::arg("dtype") = DataType::UINT8)
      .def("data",
           [](Buffer* buf, std::vector<size_t> shape, const py::dtype& dtype) {
             if (shape.empty()) {
               size_t data_num = static_cast<size_t>(buf->MemorySize() / dtype.itemsize());
               return PointerToArray(buf->MutableData(), {data_num}, dtype);
             } else {
               return PointerToArray(buf->MutableData(), shape, dtype);
             }
           },
           py::arg("shape") = std::vector<size_t>{}, py::arg("dtype") = py::dtype::of<uint8_t>())
      .def("memory_size", &Buffer::MemorySize)
      .def("dev_id", &Buffer::DeviceId)
      .def("type", &Buffer::Type)
      .def("on_mlu", &Buffer::OnMlu)
      .def("own_memory", &Buffer::OwnMemory)
      .def("copy_from", [](Buffer* buffer, void* cpu_src, size_t copy_size) { buffer->CopyFrom(cpu_src, copy_size); },
           py::arg("src"), py::arg("size"))
      .def("copy_from",
           [](Buffer* buffer, std::reference_wrapper<Buffer> src, size_t copy_size) {
             if (!copy_size) copy_size = buffer->MemorySize();
             buffer->CopyFrom(src.get(), copy_size);
           },
           py::arg("src"), py::arg("size") = 0)
      .def("copy_from",
           [](Buffer* buffer, py::array buf_array, size_t copy_size) {
             if (!copy_size) copy_size = GetTotalSize(buf_array);
             void* cpu_src = const_cast<void*>(buf_array.data());
             buffer->CopyFrom(cpu_src, copy_size);
           },
           py::arg("src"), py::arg("size") = 0)
      .def("copy_to", [](Buffer* buffer, void* cpu_dst, size_t copy_size) { buffer->CopyTo(cpu_dst, copy_size); },
           py::arg("dst"), py::arg("size"))
      .def("copy_to",
           [](Buffer* buffer, Buffer* dst, size_t copy_size) {
             if (!copy_size) copy_size = buffer->MemorySize();
             buffer->CopyTo(dst, copy_size);
           },
           py::arg("dst"), py::arg("size") = 0)
      .def("copy_to",
           [](Buffer* buffer, py::array* buf_array, size_t copy_size) {
             if (!copy_size) copy_size = GetTotalSize(*buf_array);
             void* dst = const_cast<void*>(buf_array->data());
             buffer->CopyTo(dst, copy_size);
           },
           py::arg("dst"), py::arg("size") = 0);

  py::enum_<MemoryType>(m, "MemoryType").value("CPU", MemoryType::CPU).value("MLU", MemoryType::MLU);
}

}  //  namespace infer_server
