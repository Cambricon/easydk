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

#include "buffer.h"

#include <cnrt.h>
#include <glog/logging.h>

#include <memory>
#include <string>
#include <utility>

#include "device/mlu_context.h"

using edk::Exception;
using edk::MluContext;

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
  VLOG(5) << "copy memory, " << TransDirectionStr(direction) << ", size " << size << ", src: " << src
          << ", dst: " << src;
  error_code = cnrtMemcpy(dst, const_cast<void*>(src), size, direction);
  CHECK_CNRT_RET(error_code, "Memcpy " + TransDirectionStr(direction) + " failed.");
}

namespace detail {
struct CpuMemory {
  ~CpuMemory() {
    if (data && deallocator) {
      deallocator(data);
    }
  }
  void* data{nullptr};
  Buffer::CpuMemoryDeallocator deallocator{nullptr};
};

struct MluMemory {
  ~MluMemory() {
    if (data && deallocator) {
      deallocator(data, device_id);
    }
  }
  void* data{nullptr};
  int device_id{0};
  Buffer::MluMemoryDeallocator deallocator{nullptr};
};
}  // namespace detail

Buffer::Buffer(size_t memory_size, int device_id) : memory_size_(memory_size), device_id_(device_id) {
  if (!memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  MluContext ctx;
  if (!ctx.CheckDeviceId(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }
  mlu_ = std::make_shared<detail::MluMemory>();
  mlu_->device_id = device_id;
  type_ = MemoryType::MLU;
}

Buffer::Buffer(size_t memory_size) : memory_size_(memory_size) {
  cpu_ = std::make_shared<detail::CpuMemory>();
  type_ = MemoryType::CPU;
}

Buffer::Buffer(void* mlu_memory, size_t memory_size, MluMemoryDeallocator d, int device_id)
    : memory_size_(memory_size), device_id_(device_id) {
  if (!mlu_memory || !memory_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "memory cannot be empty");
  }
  MluContext ctx;
  if (!ctx.CheckDeviceId(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }
  mlu_ = std::make_shared<detail::MluMemory>();
  mlu_->data = mlu_memory;
  mlu_->device_id = device_id;
  mlu_->deallocator = std::move(d);
  type_ = MemoryType::MLU;
}

Buffer::Buffer(void* cpu_memory, size_t memory_size, CpuMemoryDeallocator d) : memory_size_(memory_size) {
  cpu_ = std::make_shared<detail::CpuMemory>();
  cpu_->data = cpu_memory;
  cpu_->deallocator = std::move(d);
  type_ = MemoryType::CPU;
}

void Buffer::LazyMalloc() {
  CHECK(memory_size_) << "memory size is 0";
  if (type_ == MemoryType::CPU && !cpu_->data) {
    VLOG(4) << "Alloc memory on CPU in " << memory_size_ << " bytes";
    cpu_->data = malloc(memory_size_);
    if (!cpu_->data) THROW_EXCEPTION(Exception::MEMORY, "malloc failed");
    cpu_->deallocator = [](void* memory) { free(memory); };
  } else if (type_ == MemoryType::MLU && !mlu_->data) {
    cnrtRet_t error_code;
    VLOG(4) << "Alloc memory on MLU in " << memory_size_ << " bytes";
    error_code = cnrtMalloc(&mlu_->data, memory_size_);
    CHECK_CNRT_RET(error_code, "Mlu malloc failed.");
    mlu_->deallocator = [](void* memory, int device_id) {
      try {
        MluContext ctx;
        ctx.SetDeviceId(device_id);
        ctx.BindDevice();
      } catch (Exception& e) {
        LOG(ERROR) << e.what();
        return;
      }
      VLOG(4) << "Free memory on MLU";
      cnrtRet_t ret = cnrtFree(memory);
      if (CNRT_RET_SUCCESS != ret) {
        LOG(ERROR) << "free memory failed, error code: " << ret;
      }
    };
  }
}

Buffer Buffer::operator()(size_t offset) const {
  CHECK_LT(offset + this->offset_, memory_size_);
  Buffer buf;
  buf.cpu_ = this->cpu_;
  buf.mlu_ = this->mlu_;
  buf.type_ = this->type_;
  buf.memory_size_ = this->memory_size_;
  buf.offset_ = this->offset_ + offset;
  return buf;
}

void* Buffer::MutableData() {
  if (type_ == MemoryType::CPU) {
    LazyMalloc();
    return DataOffset(cpu_->data, offset_);
  } else if (type_ == MemoryType::MLU) {
    LazyMalloc();
    return DataOffset(mlu_->data, offset_);
  } else {
    CHECK(false) << "unsupport memory type";
    return nullptr;
  }
}

const void* Buffer::Data() const {
  if (type_ == MemoryType::CPU) {
    if (!cpu_ && !cpu_->data) THROW_EXCEPTION(Exception::MEMORY, "buffer not initialized");
    return DataOffset(cpu_->data, offset_);
  } else if (type_ == MemoryType::MLU) {
    if (!mlu_ && !mlu_->data) THROW_EXCEPTION(Exception::MEMORY, "buffer not initialized");
    return DataOffset(mlu_->data, offset_);
  } else {
    CHECK(false) << "unsupport memory type";
    return nullptr;
  }
}
bool Buffer::OwnMemory() const noexcept {
  if (type_ == MemoryType::CPU) {
    return cpu_ && cpu_->data;
  } else if (type_ == MemoryType::MLU) {
    return mlu_ && mlu_->data;
  } else {
    CHECK(false) << "unsupport memory type";
    return false;
  }
}

void Buffer::CopyFrom(void* cpu_src, size_t copy_size) {
  if (this->MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: dst size less than copy size");
  }
  if (!cpu_src) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: cpu src is null!");
  }

  LazyMalloc();

  if (type_ == MemoryType::CPU) {
    memcpy(DataOffset(cpu_->data, offset_), cpu_src, copy_size);
  } else if (type_ == MemoryType::MLU) {
    MemcpyMLU(DataOffset(mlu_->data, offset_), cpu_src, copy_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  }
}

void Buffer::CopyTo(void* cpu_dst, size_t copy_size) const {
  if (this->MemorySize() < copy_size) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: src size less than copy size");
  }
  if (!cpu_dst) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "copy: cpu dst is null!");
  }

  if (type_ == MemoryType::CPU) {
    if (!cpu_->data) {
      THROW_EXCEPTION(Exception::MEMORY, "copy: buffer donot own data");
    }
    memcpy(cpu_dst, DataOffset(cpu_->data, offset_), copy_size);
  } else if (type_ == MemoryType::MLU) {
    if (!mlu_->data) {
      THROW_EXCEPTION(Exception::MEMORY, "copy: buffer donot own data");
    }
    MemcpyMLU(cpu_dst, DataOffset(mlu_->data, offset_), copy_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
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
    memcpy(DataOffset(cpu_->data, offset_), src.Data(), copy_size);
  } else if (this->type_ == MemoryType::CPU && src.Type() == MemoryType::MLU) {
    MemcpyMLU(DataOffset(cpu_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_DEV2HOST);
  } else if (this->type_ == MemoryType::MLU && src.Type() == MemoryType::CPU) {
    MemcpyMLU(DataOffset(mlu_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_HOST2DEV);
  } else if (this->type_ == MemoryType::MLU && src.Type() == MemoryType::MLU) {
    MemcpyMLU(DataOffset(mlu_->data, offset_), src.Data(), copy_size, CNRT_MEM_TRANS_DIR_DEV2DEV);
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
  MluContext ctx;
  if (!ctx.CheckDeviceId(device_id)) {
    THROW_EXCEPTION(Exception::UNAVAILABLE, std::string("no such device: ") + std::to_string(device_id));
  }

  running_.store(true);
}

MluMemoryPool::~MluMemoryPool() {
  VLOG(3) << "Destroy MLU memory pool";
  try {
    running_.store(false);
    size_t remain_memory = buffer_num_;
    MluContext ctx;
    ctx.SetDeviceId(device_id_);
    ctx.BindDevice();
    std::unique_lock<std::mutex> lk(q_mutex_);
    while (remain_memory) {
      if (cache_.empty()) {
        VLOG(5) << "wait for memory released";
        empty_cond_.wait(lk, [this]() { return !cache_.empty(); });
      }

      VLOG(4) << "Free memory on MLU " << cache_.front() << ", size = " << memory_size_;
      cnrtRet_t ret = cnrtFree(cache_.front());
      cache_.pop();
      if (CNRT_RET_SUCCESS != ret) {
        LOG(ERROR) << "free memory failed, error code: " << ret;
      }
      --remain_memory;
    }
  } catch (std::exception& e) {
    LOG(ERROR) << e.what();
  }
}

Buffer MluMemoryPool::Request(int timeout_ms) {
  VLOG(5) << "request a piece of MLU memory";
  if (!running_.load()) {
    LOG(WARNING) << "pool is not running";
    THROW_EXCEPTION(Exception::UNAVAILABLE, "pool is not running");
  }

  std::unique_lock<std::mutex> lk(q_mutex_);
  if (cache_.empty()) {
    if (buffer_num_ < max_buffer_num_) {
      MluContext ctx;
      ctx.SetDeviceId(device_id_);
      ctx.BindDevice();
      cnrtRet_t error_code;
      VLOG(4) << "Alloc memory on MLU in " << memory_size_ << " bytes";
      void* data{nullptr};
      error_code = cnrtMalloc(&data, memory_size_);
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
                [this](void* m, int) {
                  VLOG(5) << "release memory";
                  std::unique_lock<std::mutex> lk(q_mutex_);
                  cache_.push(m);
                  empty_cond_.notify_one();
                },
                device_id_);
}
/* -------- MluMemoryPool END -----------*/

}  // namespace infer_server
