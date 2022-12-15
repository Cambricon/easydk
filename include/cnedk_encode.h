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

#ifndef CNEDK_ENCODE_H_
#define CNEDK_ENCODE_H_

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
  CNEDK_VENC_TYPE_INVALID,
  /** Specifies H264 codec type */
  CNEDK_VENC_TYPE_H264,
  /** Specifies H265/HEVC codec type */
  CNEDK_VENC_TYPE_H265,
  /** Specifies JPEG codec type */
  CNEDK_VENC_TYPE_JPEG,
  /** Specifies the number of codec types */
  CNEDK_VENC_TYPE_NUM
} CnedkVencType;
/**
 * Specifies package types.
 */
typedef enum {
  /** Specifies sps package type. */
  CNEDK_VENC_PACKAGE_TYPE_SPS = 0,
  /** Specifies pps package type. */
  CNEDK_VENC_PACKAGE_TYPE_PPS,
  /** Specifies key frame package type. */
  CNEDK_VENC_PACKAGE_TYPE_KEY_FRAME,
  /** Specifies frame package type. */
  CNEDK_VENC_PACKAGE_TYPE_FRAME,
  /** Specifies sps and pps package type. */
  CNEDK_VENC_PACKAGE_TYPE_SPS_PPS,
  /** Specifies the number of package types */
  CNEDK_VENC_PACKAGE_TYPE_NUM,
} CnedkVencPakageType;

/**
 * Holds the video frame.
 */
typedef struct CnedkVEncFrameBits {
  /** The data of the video frame. */
  unsigned char *bits;
  /** The length of the video frame. */
  int len;
  /** The presentation timestamp of the video frame. */
  uint64_t pts;
  /** The package type of the video frame. */
  CnedkVencPakageType pkt_type;  // nal-type
} CnedkVEncFrameBits;

/**
 * Holds the parameters for creating video encoder.
 */
typedef struct CnedkVencCreateParams {
  /** The id of device where the encoder will be created. */
  int device_id;
  /** The number of input frame buffers that the encoder will allocated. */
  int input_buf_num;
  /** The gop size. Only valid when encoding videos */
  int gop_size;
  /** The frame rate. Only valid when encoding videos */
  double frame_rate;
  /** The color format of the input frame sent to encoder. */
  CnedkBufSurfaceColorFormat color_format;
  /** The codec type of the encoder. */
  CnedkVencType type;
  /** The width of the input frame sent to encoder. */
  uint32_t width;
  /** The height of the input frame sent to encoder. */
  uint32_t height;
  /** The bit rate. Only valid when encoding videos */
  uint32_t bitrate;
  /** Not used */
  uint32_t key_interval;
  /** Jpeg quality. The range is [1, 100]. Higher the value higher the Jpeg quality. */
  uint32_t jpeg_quality;
  /** The OnFrameBits callback function.*/
  int (*OnFrameBits)(CnedkVEncFrameBits *framebits, void *userdata);
  /** The OnEos callback function. */
  int (*OnEos)(void *userdata);
  /** The OnError callback function. */
  int (*OnError)(int errcode, void *userdata);
  /** The user data. */
  void *userdata;
} CnedkVencCreateParams;

/**
 * @brief Creates a video encoder with the given parameters.
 *
 * @param[out] venc A pointer points to the pointer of a video encoder.
 * @param[in] params The parameters for creating video encoder.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVencCreate(void **venc, CnedkVencCreateParams *params);
/**
 * @brief Destroys a video encoder.
 *
 * @param[in] venc A pointer of a video encoder.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVencDestroy(void *venc);
/**
 * @brief Sends video frame to a video encoder.
 *
 * @param[in] venc A pointer of a video encoder.
 * @param[in] surf The video frame.
 * @param[in] timeout_ms The timeout in milliseconds.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkVencSendFrame(void *venc, CnedkBufSurface *surf, int timeout_ms);

#ifdef __cplusplus
};
#endif

#endif  // CNEDK_ENCODE_H_
