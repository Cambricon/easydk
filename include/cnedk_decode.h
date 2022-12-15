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

#ifndef CNEDK_DECODE_H_
#define CNEDK_DECODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "cnedk_buf_surface.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Specifies codec types.
 */
typedef enum {
  /** Specifies an invalid video codec type. */
  CNEDK_VDEC_TYPE_INVALID,
  /** Specifies H264 codec type */
  CNEDK_VDEC_TYPE_H264,
  /** Specifies H265/HEVC codec type */
  CNEDK_VDEC_TYPE_H265,
  /** Specifies JPEG codec type */
  CNEDK_VDEC_TYPE_JPEG,
  /** Specifies the number of codec types */
  CNEDK_VDEC_TYPE_NUM
} CnedkVdecType;

/**
 * Holds the parameters for creating video decoder.
 */
typedef struct CnedkVdecCreateParams {
  /** The id of device where the decoder will be created. */
  int device_id;
  /** The video codec type. */
  CnedkVdecType type;
  /** The max width of the frame that the decoder could handle. */
  uint32_t max_width;
  /** The max height of the frame that the decoder could handle. */
  uint32_t max_height;
  /** The number of frame buffers that the decoder will allocated. Only valid on CE3226 platform */
  uint32_t frame_buf_num;
  /** The color format of the frame after decoding. */
  CnedkBufSurfaceColorFormat color_format;

  // When a decoded picture got, the below steps will be performed
  //  (1)  GetBufSurf
  //  (2)  decoded picture to buf_surf (csc/deepcopy)
  //  (3)  OnFrame
  /** The OnFrame callback function.*/
  int (*OnFrame)(CnedkBufSurface *surf, void *userdata);
  /** The OnEos callback function. */
  int (*OnEos)(void *userdata);
  /** The OnError callback function. */
  int (*OnError)(int errcode, void *userdata);
  /** The GetBufSurf callback function. */
  int (*GetBufSurf)(CnedkBufSurface **surf,
                  int width, int height, CnedkBufSurfaceColorFormat fmt,
                  int timeout_ms, void *userdata) = 0;
  /** The timeout in milliseconds. */
  int surf_timeout_ms;
  /** The user data. */
  void *userdata;
} CnedkVdecCreateParams;

/**
 * Holds the video stream.
 */
typedef struct CnedkVdecStream {
  /** The data of the video stream. */
  uint8_t *bits;
  /** The length of the video stream. */
  uint32_t len;
  /** The flags of the video stream. Not valid on CE3226 platform */
  uint32_t flags;
  /** The presentation timestamp of the video stream. */
  uint64_t pts;
} CnedkVdecStream;

/**
 * @brief Creates a video decoder with the given parameters.
 *
 * @param[out] vdec A pointer points to the pointer of a video decoder.
 * @param[in] params The parameters for creating video decoder.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVdecCreate(void **vdec, CnedkVdecCreateParams *params);
/**
 * @brief Destroys a video decoder.
 *
 * @param[in] vdec A pointer of a video decoder.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVdecDestroy(void *vdec);
/**
 * @brief Sends video stream to a video decoder.
 *
 * @param[in] vdec A pointer of a video decoder.
 * @param[in] stream The video stream.
 * @param[in] timeout_ms The timeout in milliseconds.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVdecSendStream(void *vdec, const CnedkVdecStream *stream, int timeout_ms);

#ifdef __cplusplus
};
#endif

#endif  // CNEDK_DECODE_H_
