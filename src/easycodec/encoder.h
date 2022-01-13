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

#include "easycodec/easy_encode.h"
#include "easycodec/vformat.h"

namespace edk {

class Encoder {
 public:
  explicit Encoder(const EasyEncode::Attr& attr) : attr_(attr) {}
  virtual ~Encoder() = default;
  virtual void AbortEncoder() = 0;
  virtual bool FeedData(const CnFrame& frame) noexcept(false) = 0;
  virtual bool FeedEos() noexcept(false) = 0;
  virtual bool RequestFrame(CnFrame* frame) = 0;
  virtual bool ReleaseBuffer(uint64_t buf_id) = 0;

  EasyEncode::Attr GetAttr() const { return attr_; }

 protected:
  EasyEncode::Attr attr_;
};

}  // namespace edk
