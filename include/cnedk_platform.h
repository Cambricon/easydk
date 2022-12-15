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

#ifndef CNEDK_PLATFORM_H_
#define CNEDK_PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Holds the parameters of a sensor.
 */
typedef struct CnedkSensorParams {
  /** The sensor type */
  int sensor_type;
  /** The mipi device */
  int mipi_dev;
  /** The bus id */
  int bus_id;
  /** The sns clock id */
  int sns_clk_id;
  /** The width of the output */
  int out_width;
  /** The height of the output */
  int out_height;
  /** Not used (NV12 by default). The color format of the output. */
  int output_format;
} CnedkSensorParams;

/**
 * Holds the parameters of vout.
 */
typedef struct CnedkVoutParams {
  /** The max width of the input */
  int max_input_width;
  /** The max height of the input */
  int max_input_height;
  /** Not used (NV12 by default). The color format of the input. */
  int input_format;
} CnedkVoutParams;

/**
 * Holds the configurations of the platform.
 */
typedef struct CnedkPlatformConfig {
  /** The number of sensors. Only Valid on CE3226 platform */
  int sensor_num;
  /** The parameters of sensors. Only Valid on CE3226 platform */
  CnedkSensorParams *sensor_params;
  /** The parameters of vout. Only Valid on CE3226 platform */
  CnedkVoutParams *vout_params;
  /** The starting codec id. Only Valid on CE3226 platform */
  int codec_id_start;
} CnedkPlatformConfig;

#define CNEDK_PLATFORM_NAME_LEN  128
/**
 * Holds the Information of the platform.
 */
typedef struct CnedkPlatformInfo {
  /** The name of the platform */
  char name[CNEDK_PLATFORM_NAME_LEN];
  /** Whether supporting unified address. 1 means supporting, and 0 means not supporting*/
  int support_unified_addr;
  /** Whether supporting map host memory. 1 means supporting, and 0 means not supporting*/
  int can_map_host_memory;
} CnedkPlatformInfo;

/**
 * @brief Initializes the platform. On CE3226 platform, vin and vout will be initailzed.
 *
 * @param[in] config The configurations of the platform.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkPlatformInit(CnedkPlatformConfig *config);
/**
 * @brief UnInitializes the platform. On CE3226 platform, vin and vout will be uninitailzed.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkPlatformUninit();
/**
 * @brief Gets the information of the platform.
 *
 * @param[in] device_id Specified the device id.
 * @param[out] info The information of the platform, including the name of the platfrom and so on.
 *
 * @return Returns 0 if this function has run successfully. Otherwise returns -1.
 */
int CnedkPlatformGetInfo(int device_id, CnedkPlatformInfo *info);

#ifdef __cplusplus
}
#endif

#endif  // CNEDK_PLATFORM_H_
