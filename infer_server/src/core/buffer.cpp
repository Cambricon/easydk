/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#include "cnis/buffer.h"

#include <cnrt.h>
#include <glog/logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "cnis/infer_server.h"
#include "cxxutil/exception.h"

using edk::Exception;

namespace infer_server {

#define CHECK_CNRT_RET(err_code, msg)                                                                  \
  if (CNRT_RET_SUCCESS != err_code) {                                                                  \
    THROW_EXCEPTION(Exception::MEMORY, std::string(msg) + " error code: " + std::to_string(err_code)); \
  }

/* -------- Buffer -----------*/
static inline void* DataOffset(void* data, size_t offset) noexcept {
  return reinterpret_cast<void*>(reinterpret_cast<int64_t>(data) + offset);
}

static inline std::string TransDirectionStr(cnrtMemTransDir_t direction) {
  switch (direction) {
    case CNRT_MEM_TRANS_DIR_HOST2DEV:
      return "from host to device";
    case CNRT_MEM_TRANS_DIR_DEV2HOST:
      return "from device to host";
    case CNRT_MEM_TRANS_DIR_DEV2DEV:
      return "from device to device";
    default:
      CHECK(false) << "invalid direction";
      return "";
  }
}

static inline void MemcpyMLU(void* dst, const void* src, size_t size, cnrtMemTransDir_t direction) {
  cnrtRet_t error_code;
  VLOG(6) << "copy memory, " << TransDirectionStr(direction) << ", size " << size << ", src: " << src
          << ", dst: " << src;
  error_code = cnrtMemcpy(dst, const_cast<void*>(src), size, direction);
  CHECK_CNRT_RET(error_code, "Memcpy " + TransDirectionStr(direction) + " failed.");
}

namespace detail {
struct Memory {
  Memory(void* _data, int _device_id, Buffer::MemoryDeallocator&& _deallocator)
      : data(_data), deallocator(std::forward<Buffer::MemoryDeallocator>(_deallocator)), device_id(_device_id) {}
  ~Memory() {
    if (data && deallocator) {
      deallocator(data, device_id);
    }
  }
  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;
  void* data{nullptr};
  Buffer::MemoryDeallocator deallocator{nullptr};
  int device_id{-1};
};
}  // namespace detail

Buffer::Buffer(size_t memory_size, int device_id) : memory_size_(memory_size) {
  if (!memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  if (!CheckDevice(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }
  data_ = std::make_shared<detail::Memory>(nullptr, device_id, [](void* memory, int device_id) {
    if (!SetCurrentDevice(device_id)) return;
    VLOG(5) << "Free memory on MLU. " << memory;
    cnrtRet_t ret = cnrtFree(memory);
    if (CNRT_RET_SUCCESS != ret) {
      LOG(ERROR) << "free memory failed, error code: " << ret;
    }
  });
  type_ = MemoryType::MLU;
}

Buffer::Buffer(size_t memory_size) : memory_size_(memory_size) {
  if (!memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  data_ = std::make_shared<detail::Memory>(nullptr, -1, [](void* memory, int /*unused*/) {
    VLOG(5) << "Free memory on CPU. " << memory;
    free(memory);
  });
  type_ = MemoryType::CPU;
}

Buffer::Buffer(void* mlu_memory, size_t memory_size, MemoryDeallocator d, int device_id) : memory_size_(memory_size) {
  if (!mlu_memory || !memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  if (!CheckDevice(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }
  data_ = std::make_shared<detail::Memory>(mlu_memory, device_id, std::move(d));
  type_ = MemoryType::MLU;
}

Buffer::Buffer(void* cpu_memory, size_t memory_size, MemoryDeallocator d) : memory_size_(memory_size) {
  if (!cpu_memory || !memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  data_ = std::make_shared<detail::Memory>(cpu_memory, -1, std::move(d));
  type_ = MemoryType::CPU;
}

int Buffer::DeviceId() const noexcept {
  assert(data_);
  return data_->device_id;
}

void Buffer::LazyMalloc() {
  assert(memory_size_);
  assert(data_);
  if (!data_->data) {
    if (type_ == MemoryType::CPU) {
      data_->data = malloc(memory_size_);
      VLOG(5) << "Alloc memory on CPU in " << memory_size_ << " bytes. " << data_->data;
      if (!data_->data) THROW_EXCEPTION(Exception::MEMORY, "malloc failed");
    } else if (type_ == MemoryType::MLU) {
      cnrtRet_t error_code;
      SetCurrentDevice(data_->device_id);
      error_code = cnrtMalloc(&data_->data, memory_size_);
      VLOG(5) << "Alloc memory on MLU in " << memory_size_ << " bytes. " << data_->data;
      CHECK_CNRT_RET(error_code, "Mlu malloc failed.");
    }
  }
}

Buffer Buffer::operator()(size_t offset) const {
  if (offset + this->offset_ >= memory_size_) THROW_EXCEPTION(Exception::INVALID_ARG, "Offset out of range");
  Buffer buf;
  buf.data_ = this->data_;
  buf.type_ = this->type_;
  buf.memory_size_ = this->memory_size_;
  buf.offset_ = this->offset_ + offset;
  return buf;
}

void* Buffer::MutableData() {
  LazyMalloc();
  return DataOffset(data_->data, offset_);
}

const void* Buffer::Data() const {
  if (!data_ || !data_->data) THROW_EXCEPTION(Exception::MEMORY, "buffer not initialized");
  return DataOffset(data_->data, offset_);
}
bool Buffer::OwnMemory() const noexcept {
  return data_ && data_->data;
}

void Buffer::CopyFrom(const void* cpu_src, size_t copy_size) {
  if (this->MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: dst size less than copy size");
  }
  if (!cpu_src) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: cpu src is null!");
  }

  LazyMalloc();

  if (type_ == MemoryType::CPU) {
    memcpy(DataOffset(data_->data, offset_), cpu_src, copy_size);
  } else if (type_ == MemoryType::MLU) {
    MemcpyMLU(DataOffset(data_->data, offset_), cpu_src, copy_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  }
}

void Buffer::CopyTo(void* cpu_dst, size_t copy_size) const {
  if (this->MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: src size less than copy size");
  }
  if (!cpu_dst) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: cpu dst is null!");
  }
  if (!data_->data) {
    THROW_EXCEPTION(Exception::MEMORY, "copy: buffer donot own data");
  }

  if (type_ == MemoryType::CPU) {
    memcpy(cpu_dst, DataOffset(data_->data, offset_), copy_size);
  } else if (type_ == MemoryType::MLU) {
    MemcpyMLU(cpu_dst, DataOffset(data_->data, offset_), copy_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  }
}

void Buffer::CopyFrom(const Buffer& src, size_t copy_size) {
  if (src.MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: src size less than copy size");
  }
  if (this->MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: dst size less than copy size");
  }

  LazyMalloc();

  if (this->type_ == MemoryType::CPU && src.Type() == MemoryType::CPU) {
    memcpy(DataOffset(data_->data, offset_), src.Data(), copy_size);
  } else if (this->type_ == MemoryType::CPU && src.Type() == MemoryType::MLU) {
    MemcpyMLU(DataOffset(data_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  } else if (this->type_ == MemoryType::MLU && src.Type() == MemoryType::CPU) {
    MemcpyMLU(DataOffset(data_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  } else if (this->type_ == MemoryType::MLU && src.Type() == MemoryType::MLU) {
    MemcpyMLU(DataOffset(data_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_DEV2DEV);
  } else {
    CHECK(false) << "unknown copy direction";
  }
}

void Buffer::CopyTo(Buffer* dst, size_t copy_size) const { dst->CopyFrom(*this, copy_size); }

/* -------- Buffer END -----------*/

/* -------- MluMemoryPool -----------*/
MluMemoryPool::MluMemoryPool(size_t memory_size, size_t max_buffer_num, int device_id)
    : memory_size_(memory_size), max_buffer_num_(max_buffer_num), buffer_num_(0), device_id_(device_id) {
  VLOG(3) << "Init a MLU memory pool";
  if (!memory_size || !max_buffer_num) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory size or max buffer number is 0!");
  }
  if (!CheckDevice(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }

  running_.store(true);
}

MluMemoryPool::~MluMemoryPool() {
  VLOG(3) << "Destroy MLU memory pool";
  running_.store(false);
  size_t remain_memory = buffer_num_;
  if (!SetCurrentDevice(device_id_)) return;
  std::unique_lock<std::mutex> lk(q_mutex_);
  while (remain_memory) {
    if (cache_.empty()) {
      VLOG(5) << "wait for memory released";
      empty_cond_.wait(lk, [this]() { return !cache_.empty(); });
    }

    VLOG(5) << "Free memory on MLU " << cache_.front() << ", size = " << memory_size_;
    cnrtRet_t ret = cnrtFree(cache_.front());
    cache_.pop();
    if (CNRT_RET_SUCCESS != ret) {
      LOG(ERROR) << "free memory failed, error code: " << ret;
    }
    --remain_memory;
  }
}

Buffer MluMemoryPool::Request(int timeout_ms) {
  VLOG(6) << "request a piece of MLU memory";
  if (!running_.load()) {
    LOG(WARNING) << "pool is not running";
    THROW_EXCEPTION(Exception::UNAVAILABLE, "pool is not running");
  }

  std::unique_lock<std::mutex> lk(q_mutex_);
  if (cache_.empty()) {
    if (buffer_num_ < max_buffer_num_) {
      if (!SetCurrentDevice(device_id_)) THROW_EXCEPTION(Exception::INIT_FAILED, "Set device failed");
      cnrtRet_t error_code;
      void* data{nullptr};
      error_code = cnrtMalloc(&data, memory_size_);
      VLOG(5) << "Alloc memory on MLU in " << memory_size_ << " bytes. " << data;
      CHECK_CNRT_RET(error_code, "MLU malloc failed");
      cache_.push(data);
      ++buffer_num_;
    } else {
      auto not_empty = [this]() { return !cache_.empty(); };
      if (timeout_ms >= 0) {
        VLOG(6) << "wait for idle memory, " << timeout_ms << " ms";
        empty_cond_.wait_for(lk, std::chrono::milliseconds(timeout_ms), not_empty);
      } else {
        VLOG(6) << "wait for idle memory, endlessly";
        empty_cond_.wait(lk, not_empty);
      }
    }
  }

  if (cache_.empty()) {
    LOG_EVERY_N(INFO, 100) << "RequestMemory timeout, to reduce timeout:\n"
                              "     1. enlarge max_buffer_num of pool;\n"
                              "     2. release Buffer as soon as possible;\n"
                              "     3. increase timeout threshold.";
    THROW_EXCEPTION(Exception::TIMEOUT, "request memory timeout");
  }

  void* m = cache_.front();
  cache_.pop();
  return Buffer(m, memory_size_,
                [this](void* m, int /*unused*/) {
                  VLOG(6) << "release memory";
                  std::unique_lock<std::mutex> lk(q_mutex_);
                  cache_.push(m);
                  empty_cond_.notify_one();
                },
                device_id_);
}
/* -------- MluMemoryPool END -----------*/

}  // namespace infer_server
