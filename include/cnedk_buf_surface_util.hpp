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

#ifndef CNEDK_BUF_SURFACE_UTIL_HPP_
#define CNEDK_BUF_SURFACE_UTIL_HPP_


#include <cstring>  // for memset
#include <memory>
#include <mutex>
#include <utility>

#include "cnrt.h"

#include "cnedk_buf_surface.h"

namespace cnedk {

/**
 * @class IBufDeleter
 *
 * @brief IBufDeleter is a class, which is used to support use-defined buffer deleter.
 */
class IBufDeleter {
 public:
 /**
   * @brief A destructor to destruct an IBufDeleter object.
   *
   * @return No return value.
   */
  virtual ~IBufDeleter() {}
};

/**
 * @class BufSurfaceWrapper
 *
 * @brief BufSurfaceWrapper is a class, which provides a wrapper around CnedkBufSurface.
 */
class BufSurfaceWrapper {
 public:
 /**
   * @brief A constructor to construct a BufSurfaceWrapper object.
   *
   * @param[in] surf The pointer of a CnedkBufSurface object.
   * @param[in] owner Whether to transfer ownership of CnedkBufSurface to wrapper.
   *
   * @return No return value.
   */
  explicit BufSurfaceWrapper(CnedkBufSurface *surf, bool owner = true) : surf_(surf), owner_(owner) {}
  /**
   * @brief A destructor to destruct a BufSurfaceWrapper object.
   *
   * @return No return value.
   */
  ~BufSurfaceWrapper() {
    std::unique_lock<std::mutex> lk(mutex_);
    if (deleter_) {
      delete deleter_, deleter_ = nullptr;
      return;
    }
    if (owner_ && surf_) CnedkBufSurfaceDestroy(surf_), surf_ = nullptr;
  }
  /**
   * @brief Gets the pointer of the CnedkBufSurface object.
   *
   * @return Returns the pointer of the CnedkBufSurface object.
   */
  CnedkBufSurface *GetBufSurface() const;
  /**
   * @brief The wrapper no longer holds a pointer to the CnedkBufSurface object and returns it.
   *
   * @return Returns the pointer of the CnedkBufSurface object.
   */
  CnedkBufSurface *BufSurfaceChown();
  /**
   * @brief Gets parameters of one buffer of the batched buffers in CnedkBufSurface.
   *
   * @param[in] batch_idx The batch index, indicates where the buffer is located in the batch.
   *
   * @return Returns the pointer of the CnedkBufSurfaceParams object.
   */
  CnedkBufSurfaceParams *GetSurfaceParams(uint32_t batch_idx = 0) const;
  /**
   * @brief Gets the number of filled buffers in CnedkBufSurface.
   *
   * @return Returns the number of filled buffers.
   */
  uint32_t GetNumFilled() const;
  /**
   * @brief Gets the color format of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the color format.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  CnedkBufSurfaceColorFormat GetColorFormat() const;
  /**
   * @brief Gets the width of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the width.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  uint32_t GetWidth() const;
  /**
   * @brief Gets the height of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the height.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  uint32_t GetHeight() const;
  /**
   * @brief Gets the stride of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the stride.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  uint32_t GetStride(uint32_t i) const;
  /**
   * @brief Gets the plane number of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the plane number.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  uint32_t GetPlaneNum() const;
  /**
   * @brief Gets the plane bytes of the batched buffers in CnedkBufSurface.
   *
   * @return Returns the plane bytes.
   *
   * @note the batched buffers share the same color_format, width, height and stride.
   */
  uint32_t GetPlaneBytes(uint32_t i) const;
  /**
   * @brief Gets the id of the device where the batched buffers is stored.
   *
   * @return Returns the device id.
   */
  int GetDeviceId() const;
  /**
   * @brief Gets the type of the memory where the batched buffers is stored.
   *
   * @return Returns the memory type.
   */
  CnedkBufSurfaceMemType GetMemType() const;
  /**
   * @brief Gets the plane data of one buffer of the batched buffers in CnedkBufSurface.
   *
   * @param[in] plane_idx The plane index, indicates the data of which plane will be returned.
   * @param[in] batch_idx The batch index, indicates where the buffer is located in the batch. Defaults 0.
   *
   * @return Returns the pointer of the data.
   */
  void *GetData(uint32_t plane_idx, uint32_t batch_idx = 0);
  /**
   * @brief Gets the mapped data of one buffer of the batched buffers in CnedkBufSurface.
   *
   * @param[in] plane_idx The plane index, indicates the mapped data of which plane will be returned.
   * @param[in] batch_idx The batch index, indicates where the buffer is located in the batch. Defaults 0.
   *
   * @return Returns the pointer of the mapped data.
   */
  void *GetMappedData(uint32_t plane_idx, uint32_t batch_idx = 0);
  /**
   * @brief Gets the presentation timestamp of the CnedkBufSurface object.
   *
   * @return Returns the presentation timestamp.
   *
   * @note The presentation timestamp is valid when the batch size of the is CnedkBufSurface object 1.
   */
  uint64_t GetPts() const;
  /**
   * @brief Sets the presentation timestamp of the CnedkBufSurface object.
   *
   * @param[in] pts The presentation timestamp.
   *
   * @return No return value.
   *
   * @note The presentation timestamp is valid when the batch size of the is CnedkBufSurface object 1.
   */
  void SetPts(uint64_t pts);
  /**
   * @brief Gets the host data of one buffer of the batched buffers in CnedkBufSurface.
   *        The host data is stored in wrapper.
   *
   * @param[in] plane_idx The plane index, indicates the host data of which plane will be returned.
   * @param[in] batch_idx The batch index, indicates where the buffer is located in the batch. Defaults 0.
   *
   * @return Returns the pointer of the host data.
   *
   * @note For memory with type CNEDK_BUF_MEM_DEVICE, set cpu memory as faked mappedData for convenience.
   */
  void *GetHostData(uint32_t plane_idx, uint32_t batch_idx = 0);
  /**
   * @brief Synchronizes the host data to device.
   *
   * @param[in] plane_idx The plane index, indicates which plane data will be synchronized.
   *                      Defaults -1, means all planes.
   * @param[in] batch_idx The batch index, indicates where the buffer is located in the batch.
   *                      Defaults -1, means the whole batched buffers.
   *
   * @return No return value.
   */
  void SyncHostToDevice(uint32_t plane_idx = -1, uint32_t batch_idx = -1);

 public:
  /**
   * @brief A constructor to construct a BufSurfaceWrapper object.
   *
   * @param[in] data The pointer holds the tensor data.
   * @param[in] len The length of the tensor data.
   * @param[in] mem_type The memory type of the tensor data.
   * @param[in] device_id The id of the device Where the tensor data is stored.
   * @param[in] deleter The user-defined buffer deleter.
   *
   * @return No return value.
   *
   * @note This function is used by infer server only, for mutable-output case, memory will be allocated by magicmind.
   */
  BufSurfaceWrapper(void *data, size_t len, CnedkBufSurfaceMemType mem_type, int device_id, IBufDeleter *deleter) {
    surf_ = &surface_;
    deleter_ = deleter;
    memset(surf_, 0, sizeof(CnedkBufSurface));
    memset(&surface_list_, 0, sizeof(CnedkBufSurfaceParams));
    surf_->surface_list = &surface_list_;
    surf_->mem_type = mem_type;
    surf_->batch_size = 1;
    surf_->device_id = device_id;
    surf_->surface_list[0].color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
    surf_->surface_list[0].data_ptr = data;
    surf_->surface_list[0].data_size = len;
  }

 private:
  CnedkBufSurfaceParams *GetSurfaceParamsPriv(uint32_t batch_idx = 0) const { return &surf_->surface_list[batch_idx]; }

 private:
  mutable std::mutex mutex_;
  CnedkBufSurface *surf_ = nullptr;
  bool owner_ = true;
  std::unique_ptr<unsigned char[]> host_data_[128]{{nullptr}};

  IBufDeleter *deleter_ = nullptr;
  CnedkBufSurface surface_;
  CnedkBufSurfaceParams surface_list_;  // for mutable output case
  uint64_t pts_ = ~(static_cast<uint64_t>(0));  // Only valid when surf_ is nullptr
};

using BufSurfWrapperPtr = std::shared_ptr<BufSurfaceWrapper>;

/**
 * @class BufPool
 *
 * @brief BufPool is a class, which is used for buffer wrapper management.
 */
class BufPool {
 public:
 /**
   * @brief A constructor to construct a BufPool object.
   *
   * @return No return value.
   */
  BufPool() {}
  /**
   * @brief A destructor to destruct a BufPool object.
   *
   * @return No return value.
   */
  ~BufPool() { DestroyPool(5000); }
  /**
   * @brief Creates pool.
   *
   * @param[in] params The parameters for creating CnedkBufSurface.
   * @param[in] block_count The capacity of the pool.
   *
   * @return Returns 0 if this function has run successfully. Otherwise returns -1.
   */
  int CreatePool(CnedkBufSurfaceCreateParams *params, uint32_t block_count);
  /**
   * @brief Destroys pool.
   *
   * @return No return value.
   */
  void DestroyPool(int timeout_ms = 0);
  /**
   * @brief Gets BufSurfacewrapper from pool.
   *
   * @param[in] timeout_ms The timeout in milliseconds. Defaults 0.
   *
   * @return Returns BufSurfacewrapper if this function has run successfully. Otherwise returns nullptr.
   */
  BufSurfWrapperPtr GetBufSurfaceWrapper(int timeout_ms = 0);

 private:
  BufPool(const BufPool &) = delete;
  BufPool(BufPool &&) = delete;
  BufPool &operator=(const BufPool &) = delete;
  BufPool &operator=(BufPool &&) = delete;

 private:
  std::mutex mutex_;
  void *pool_ = nullptr;
  bool stopped_ = false;
};

}  // namespace cnedk

#endif  // CNEDK_BUF_SURFACE_UTIL_HPP_
