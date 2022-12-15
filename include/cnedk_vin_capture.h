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

#ifndef CNEDK_VIN_CAPTURE_H_
#define CNEDK_VIN_CAPTURE_H_

#include <stdint.h>
#include <stdbool.h>
#include "cnedk_buf_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Holds the parameters of creating a vin capture.
 */
typedef struct CnedkVinCaptureCreateParams {
  /** Holds the sensor id */
  int sensor_id;
  /** Holds the OnFrame callback function */
  int (*OnFrame)(CnedkBufSurface *surf, void *userdata);
  /** Holds the OnError callback function */
  int (*OnError)(int errcode, void *userdata);
  /** Holds the GetBufSurf callback function */
  int (*GetBufSurf)(CnedkBufSurface **surf, int timeout_ms, void *userdata) = 0;
  /** Holds the timeout in milliseconds */
  int surf_timeout_ms;
  /** Holds the user data */
  void *userdata;
} CnedkVinCaptureCreateParams;

/**
 * @brief Creates a video input capture with the given parameters.
 *
 * @param[out] vin_capture A pointer points to the pointer of a video input capture.
 * @param[in] params The parameters for creating video input capture.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVinCaptureCreate(void **vin_capture, CnedkVinCaptureCreateParams *params);
/**
 * @brief Destroys a video input capture.
 *
 * @param[in] vin_capture A pointer of a video input capture.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVinCaptureDestroy(void *vin_capture);
/**
 * @brief Captures video input.
 *
 * @param[in] vin_capture A pointer of a video input capture.
 * @param[in] timeout_ms The timeout in milliseconds.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVinCapture(void *vin_capture, int timeout_ms);

#ifdef __cplusplus
};
#endif

#endif  // CNEDK_VIN_CAPTURE_H_
