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
#include <atomic>

#include "easycodec/easy_decode.h"
#include "easycodec/vformat.h"

namespace edk {

namespace detail {

int CheckProgressiveMode(uint8_t* data, uint64_t length);

}  // namespace detail

class Decoder {
 public:
  explicit Decoder(const EasyDecode::Attr& attr) : attr_(attr) {}
  virtual ~Decoder() = default;
  virtual void AbortDecoder() = 0;
  virtual bool FeedData(const CnPacket& packet) noexcept(false) = 0;
  virtual bool FeedEos() noexcept(false) = 0;
  virtual bool ReleaseBuffer(uint64_t buf_id) = 0;

  EasyDecode::Attr GetAttr() const { return attr_; }
  EasyDecode::Status GetStatus() const { return status_.load(); }
  int GetMinimumOutputBufferCount() const { return minimum_buf_cnt_.load(); }

  bool Pause() {
    EasyDecode::Status expected = EasyDecode::Status::RUNNING;
    if (status_.compare_exchange_strong(expected, EasyDecode::Status::PAUSED)) {
      return true;
    }
    return false;
  }
  bool Resume() {
    EasyDecode::Status expected = EasyDecode::Status::PAUSED;
    if (status_.compare_exchange_strong(expected, EasyDecode::Status::RUNNING)) {
      return true;
    }
    return false;
  }


 protected:
  EasyDecode::Attr attr_;
  std::atomic<EasyDecode::Status> status_{EasyDecode::Status::RUNNING};
  std::atomic<int> minimum_buf_cnt_{0};
};

}  // namespace edk
