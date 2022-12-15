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

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "cnrt.h"

#include "cnedk_platform.h"

#include "test_base.h"

static const size_t device_id = 0;

TEST(Platform, Init) {
  CnedkSensorParams sensor_params[4];
  memset(sensor_params, 0, sizeof(CnedkSensorParams) * 4);
  CnedkVoutParams vout_params;
  memset(&vout_params, 0, sizeof(CnedkVoutParams));

  CnedkPlatformConfig config;
  memset(&config, 0, sizeof(config));

  config.codec_id_start = 0;

  // config.vout_params = &vout_params;
  // vout_params.max_input_width = 1920;
  // vout_params.max_input_height = 1080;
  // vout_params.input_format = 0; // not used at the moment

  // config.sensor_num = 1;
  // config.sensor_params = sensor_params;
  // sensor_params[0].sensor_type = 6;
  // sensor_params[0].mipi_dev = 1;
  // sensor_params[0].bus_id = 0;
  // sensor_params[0].sns_clk_id = 1;
  // sensor_params[0].out_width = 1920;
  // sensor_params[0].out_height = 1080;
  // sensor_params[0].output_format = 0; // not used at the moment

  ASSERT_EQ(CnedkPlatformInit(&config), 0);
  ASSERT_EQ(CnedkPlatformUninit(), 0);
}

TEST(Platform, GetInfo) {
  CnedkPlatformInfo platform_info;
  ASSERT_EQ(CnedkPlatformGetInfo(device_id, &platform_info), 0);
}
