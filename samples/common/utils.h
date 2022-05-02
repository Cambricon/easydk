
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

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include "cnrt.h"


#ifdef HAVE_CNCV
#define CNCV_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cncvStatus_t ret = (func);                                    \
    if (ret != CNCV_STATUS_SUCCESS) {                             \
      LOG(ERROR) << "[EasyDK Samples] Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
#endif

#define CNRT_SAFE_CALL(func, val)                                 \
  do {                                                            \
    cnrtRet_t ret = (func);                                       \
    if (ret != CNRT_RET_SUCCESS) {                                \
      LOG(ERROR) << "[EasyDK Samples] Call " #func " failed. error code: " << ret; \
      return val;                                                 \
    }                                                             \
  } while (0)
