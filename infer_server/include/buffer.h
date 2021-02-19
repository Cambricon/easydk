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

/**
 * @file buffer.h
 *
 * This file contains a declaration of the Buffer class.
 */

#ifndef INFER_SERVER_BUFFER_H_
#define INFER_SERVER_BUFFER_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace infer_server {

/**
 * @brief Enumerator of memory type
 */
enum class MemoryType {
  CPU = 0,  ///< memory on CPU
  MLU = 1,  ///< memory on MLU
};

namespace detail {
struct CpuMemory;
struct MluMemory;
}  // namespace detail

class Buffer {
 public:
  /// callback function to deallocate memory on MLU
  using MluMemoryDeallocator = std::function<void(void *memory, int device_id)>;
  /// callback function to deallocate memory on CPU
  using CpuMemoryDeallocator = std::function<void(void *memory)>;

  /**
   * @brief Construct a new Buffer object contained CPU memory
   *
   * @param memory_size Memory size in bytes
   */
  explicit Buffer(size_t memory_size);

  /**
   * @brief Construct a new Buffer object contained MLU memory
   *
   * @param memory_size Memory size in bytes
   * @param device_id memory on which device
   */
  explicit Buffer(size_t memory_size, int device_id);

  /**
   * @brief Construct a new Buffer object with raw MLU memory
   *
   * @param mlu_memory raw pointer
   * @param memory_size Memory size in bytes
   * @param d A function to handle memory when destruct
   * @param device_id memory on which device
   */
  Buffer(void *mlu_memory, size_t memory_size, MluMemoryDeallocator d, int device_id);

  /**
   * @brief Construct a new Buffer object with raw CPU memory
   *
   * @param cpu_memory raw pointer
   * @param memory_size Memory size in bytes
   * @param d A function to handle memory when destruct
   */
  Buffer(void *cpu_memory, size_t memory_size, CpuMemoryDeallocator d);

  /**
   * @brief default constructor
   *
   * @warning generated Buffer cannot be used until assigned
   */
  Buffer() = default;

  /**
   * @brief default copy constructor (shallow)
   */
  Buffer(const Buffer &another) = default;

  /**
   * @brief default copy assign (shallow)
   */
  Buffer &operator=(const Buffer &another) = default;

  /**
   * @brief default move construct
   */
  Buffer(Buffer &&another) = default;

  /**
   * @brief default move assign
   */
  Buffer &operator=(Buffer &&another) = default;

  /**
   * @brief Get a shallow copy of buffer by offset
   *
   * @param offset offset
   * @return copied buffer
   */
  Buffer operator()(size_t offset) const;

  /**
   * @brief Get mutable raw pointer
   *
   * @return raw pointer
   */
  void *MutableData();

  /**
   * @brief Get const raw pointer
   *
   * @return raw pointer
   */
  const void *Data() const;

  /**
   * @brief Get size of MLU memory
   *
   * @return memory size in bytes
   */
  size_t MemorySize() const noexcept { return memory_size_ - offset_; }

  /**
   * @brief Get device id
   *
   * @return device id
   */
  int DeviceId() const noexcept { return device_id_; }

  /**
   * @brief Get memory type
   *
   * @return memory type
   */
  MemoryType Type() const noexcept { return type_; }

  /**
   * @brief Query whether memory is on MLU
   *
   * @retval true memory on MLU
   * @retval false memory on CPU
   */
  bool OnMlu() const noexcept { return type_ == MemoryType::MLU; }

  /**
   * @brief query whether Buffer own memory
   *
   * @retval true own memory
   * @retval false not own memory
   */
  bool OwnMemory() const noexcept;

  /**
   * @brief Copy data from raw CPU memory
   *
   * @param cpu_src Copy source, data on CPU
   * @param copy_size Memory size in bytes
   */
  void CopyFrom(void *cpu_src, size_t copy_size);

  /**
   * @brief Copy data from another buffer
   *
   * @param src Copy source
   * @param copy_size Memory size in bytes
   */
  void CopyFrom(const Buffer &src, size_t copy_size);

  /**
   * @brief Copy data to raw CPU memory
   *
   * @param cpu_dst Copy destination, memory on CPU
   * @param copy_size Memory size in bytes
   */
  void CopyTo(void *cpu_dst, size_t copy_size) const;

  /**
   * @brief Copy data to another buffer
   *
   * @param dst Copy source
   * @param copy_size Memory size in bytes
   */
  void CopyTo(Buffer *dst, size_t copy_size) const;

 private:
  void LazyMalloc();
  std::shared_ptr<detail::CpuMemory> cpu_{nullptr};
  std::shared_ptr<detail::MluMemory> mlu_{nullptr};

  MemoryType type_{MemoryType::CPU};
  size_t memory_size_{0};
  size_t offset_{0};

  int device_id_{-1};
};

/**
 * @brief MluMemoryPool is a MLU memory helper class.
 *
 * @note It provides a easy way to manage memory on MLU.
 */
class MluMemoryPool {
 public:
  /**
   * @brief Construct a new Mlu Memory Pool object
   *
   * @param memory_size Memory size in bytes
   * @param max_buffer_num max number of memory cached in pool
   * @param device_id memory on which device
   */
  MluMemoryPool(size_t memory_size, size_t max_buffer_num, int device_id = 0);

  /**
   * @brief A destructor
   * @note wait until all MluMemory requested is released
   */
  ~MluMemoryPool();

  /**
   * @brief Request Buffer from pool, wait for timeout_ms if pool is empty
   *
   * @param timeout_ms wait timeout in milliseconds
   * @return a Buffer
   */
  Buffer Request(int timeout_ms = -1);

  /**
   * @brief Get size of MLU memory
   *
   * @return memory size in bytes
   */
  size_t MemorySize() const noexcept { return memory_size_; }

  /**
   * @brief Get how many pieces of MLU memory cached
   *
   * @return number of memory cached
   */
  size_t BufferNum() const noexcept { return buffer_num_; }

 private:
  std::queue<void *> cache_;
  std::mutex q_mutex_;
  std::condition_variable empty_cond_;
  size_t memory_size_;
  size_t max_buffer_num_;
  size_t buffer_num_;
  int device_id_;
  std::atomic<bool> running_{false};
};

}  // namespace infer_server

#endif  // INFER_SERVER_BUFFER_H_
