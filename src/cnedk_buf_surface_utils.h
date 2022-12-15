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

#ifndef CNEDK_BUF_SURFACE_UTILS_H_
#define CNEDK_BUF_SURFACE_UTILS_H_

#include "cnedk_buf_surface.h"

namespace cnedk {

int GetColorFormatInfo(CnedkBufSurfaceColorFormat fmt, uint32_t width, uint32_t height,
                       uint32_t align_size_w, uint32_t align_size_h, CnedkBufSurfacePlaneParams *params);

int CheckParams(CnedkBufSurfaceCreateParams *params);

}  // namespace cnedk

#endif  // CNEDK_BUF_SURFACE_UTILS_H_
