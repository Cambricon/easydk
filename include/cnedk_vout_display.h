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

#ifndef CNEDK_VOUT_DISPLAY_H_
#define CNEDK_VOUT_DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>
#include "cnedk_buf_surface.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Renders video output.
 *
 * @param[in] surf A pointer points to the video frame.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVoutRender(CnedkBufSurface *surf);

#ifdef __cplusplus
};
#endif

#endif  // CNEDK_VOUT_DISPLAY_H_
