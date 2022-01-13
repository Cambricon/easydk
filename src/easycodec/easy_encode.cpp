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

#include "easycodec/easy_encode.h"

#include <cstring>
#include <memory>
#include <string>

#include "cxxutil/log.h"
#include "device/mlu_context.h"
#include "encoder.h"

using std::to_string;
#define ALIGN(size, alignment) (((uint32_t)(size) + (alignment)-1) & ~((alignment)-1))

namespace edk {

extern Encoder* CreateMlu200Encoder(const EasyEncode::Attr& attr);
extern Encoder* CreateMlu300Encoder(const EasyEncode::Attr& attr);

std::unique_ptr<EasyEncode> EasyEncode::New(const Attr& attr) {
  return std::unique_ptr<EasyEncode>(new EasyEncode(attr));
}


EasyEncode::EasyEncode(const Attr& attr) {
  MluContext ctx;
  CoreVersion core_version = ctx.GetCoreVersion();

  try {
    if (core_version == CoreVersion::MLU370) {
      handler_ = CreateMlu300Encoder(attr);
    } else if (core_version == CoreVersion::MLU270 || core_version == CoreVersion::MLU220) {
      handler_ = CreateMlu200Encoder(attr);
    } else {
      THROW_EXCEPTION(Exception::INIT_FAILED,
                      "Device not supported yet, core version: " + std::to_string(static_cast<int>(core_version)));
    }
    if (nullptr == handler_) {
      THROW_EXCEPTION(Exception::INIT_FAILED,
                      "New decoder failed, core version: " + std::to_string(static_cast<int>(core_version)));
    }
  } catch (...) {
    delete handler_;
    handler_ = nullptr;
    throw;
  }
}

EasyEncode::~EasyEncode() {
  if (handler_) {
    delete handler_;
    handler_ = nullptr;
  }
}

void EasyEncode::AbortEncoder() { handler_->AbortEncoder(); }

bool EasyEncode::FeedData(const CnFrame& frame) { return handler_->FeedData(frame); }

bool EasyEncode::FeedEos() { return handler_->FeedEos(); }

EasyEncode::Attr EasyEncode::GetAttr() const { return handler_->GetAttr(); }

bool EasyEncode::RequestFrame(CnFrame* frame) { return handler_->RequestFrame(frame); }

void EasyEncode::ReleaseBuffer(uint64_t buf_id) { handler_->ReleaseBuffer(buf_id); }

bool EasyEncode::SendDataCPU(const CnFrame &frame, bool eos) {
  if (!handler_) {
    LOGE(ENCODE) << "Encoder has not been init";
    return false;
  }
  if (frame.device_id >= 0) {
    LOGW(ENCODE) << "frame data on cpu, but device id is not negative";
    return false;
  }

  if (eos) {
    if (frame.width && frame.height && (!handler_->FeedData(frame))) {
      return false;
    }
    return handler_->FeedEos();
  } else {
    return handler_->FeedData(frame);
  }
}

}  // namespace edk
