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

#include "cnedk_platform.h"

#include <cstring>  // for memset
#include <map>
#include <mutex>
#include <string>

#include "glog/logging.h"
#include "cnrt.h"

#ifdef PLATFORM_CE3226
#include "ce3226/mps_service/mps_service.hpp"
#endif
#include "common/utils.hpp"

namespace cnedk {

struct DeviceInfo {
  std::string prop_name;
  bool support_unified_addr;
  bool can_map_host_memory;
};

static std::mutex gDeviceInfoMutex;
static std::map<int, DeviceInfo> gDeviceInfoMap;

static int GetDeviceInfo(int device_id, DeviceInfo *info) {
  std::unique_lock<std::mutex> lk(gDeviceInfoMutex);
  if (gDeviceInfoMap.count(device_id)) {
    *info = gDeviceInfoMap[device_id];
    return 0;
  }
  unsigned int count;
  CNRT_SAFECALL(cnrtGetDeviceCount(&count), "GetDeviceInfo(): failed", -1);

  if (device_id >= static_cast<int>(count) || device_id < 0) {
    LOG(ERROR) << "[EasyDK] GetDeviceInfo(): device id is invalid, device_id: " << device_id << ", total count: "
               << count;
    return -1;
  }
  cnrtSetDevice(device_id);

  DeviceInfo dev_info;
  cnrtDeviceProp_t prop;
  CNRT_SAFECALL(cnrtGetDeviceProperties(&prop, device_id), "GetDeviceInfo(): failed", -1);

  VLOG(2) << "[EasyDK] GetDeviceInfo(): device id: " << device_id << ", device name: " << prop.name;
  dev_info.prop_name = prop.name;

#ifdef PLATFORM_CE3226
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  int value;
  CNRT_SAFECALL(cnrtDeviceGetAttribute(&value, cnrtAttrSupportUnifiedAddr, device_id),
                "GetDeviceInfo(): failed", -1);

  dev_info.support_unified_addr = (value != 0);

  CNRT_SAFECALL(cnrtDeviceGetAttribute(&value, cnrtAttrCanMapHostMemory, device_id),
                "GetDeviceInfo(): failed", -1);
  dev_info.can_map_host_memory = (value != 0);
#else
  dev_info.can_map_host_memory = true;
#endif
#else
  dev_info.support_unified_addr = false;
  dev_info.can_map_host_memory = false;
#endif

  // FIXME, cnrtDeviceGetAttribute(cnrtAttrSupportUnifiedAddr) does not work
  if (dev_info.prop_name == "CE3226") dev_info.support_unified_addr = 1;

  *info = dev_info;
  gDeviceInfoMap[device_id] = dev_info;
  return 0;
}

#ifdef PLATFORM_CE3226
int PlatformInitCe3226(CnedkPlatformConfig *config) {
  MpsServiceConfig mps_config;
  if (config->sensor_num > 0) {
    for (int i = 0; i < config->sensor_num; i++) {
      CnedkSensorParams *params = &config->sensor_params[i];
      mps_config.vins[i] = VinParam(params->sensor_type, params->mipi_dev, params->bus_id, params->sns_clk_id,
                                    params->out_width, params->out_height);
      int blk_size = params->out_width * params->out_height * 2;
      blk_size = (blk_size + 1023) / 1024 * 1024;
      mps_config.vbs.push_back(VBInfo(blk_size, 6));
    }
  }

  if (config->vout_params) {
    mps_config.vout.enable = true;
    mps_config.vout.input_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
    mps_config.vout.max_input_width = config->vout_params->max_input_width;
    mps_config.vout.max_input_height = config->vout_params->max_input_height;
    int blk_size = mps_config.vout.max_input_height * mps_config.vout.max_input_width * 2;
    blk_size = (blk_size + 1023) / 1024 * 1024;
    mps_config.vbs.push_back(VBInfo(blk_size, 6));
  }

  // FIXME
  if (config->codec_id_start < 0 || config->codec_id_start >= 16) {
    return -1;
  }
  mps_config.codec_id_start = config->codec_id_start;

  return MpsService::Instance().Init(mps_config);
}
#endif

}  // namespace cnedk

#ifdef __cplusplus
extern "C" {
#endif

int CnedkPlatformInit(CnedkPlatformConfig *config) {
  // TODO(gaoyujia)
  unsigned int count;
  CNRT_SAFECALL(cnrtGetDeviceCount(&count), "CnedkPlatformInit(): failed", -1);

  for (int i = 0; i < static_cast<int>(count); i++) {
    cnedk::DeviceInfo dev_info;
    if (cnedk::GetDeviceInfo(i, &dev_info) < 0) {
      LOG(ERROR) << "[EasyDK] CnedkPlatformInit(): Get device information failed";
      return -1;
    }
  }

#ifdef PLATFORM_CE3226
  // FIXME
  if (count == 1) {
    cnedk::DeviceInfo dev_info;
    cnedk::GetDeviceInfo(0, &dev_info);

    if (dev_info.prop_name == "CE3226") {
      return cnedk::PlatformInitCe3226(config);
    }
  }
#endif

#if defined(PLATFORM_MLU370) || defined(PLATFORM_MLU590)
  return 0;
#endif
  return -1;
}

int CnedkPlatformUninit() {
#ifdef PLATFORM_CE3226
  cnedk::MpsService::Instance().Destroy();
#endif
  return 0;
}

int CnedkPlatformGetInfo(int device_id, CnedkPlatformInfo *info) {
  cnedk::DeviceInfo dev_info;
  if (cnedk::GetDeviceInfo(device_id, &dev_info) < 0) {
    return -1;
  }
  memset(info, 0, sizeof(CnedkPlatformInfo));
  snprintf(info->name, sizeof(info->name), "%s", dev_info.prop_name.c_str());
  info->can_map_host_memory = (dev_info.can_map_host_memory == true);
  info->support_unified_addr = (dev_info.support_unified_addr == true);
  return 0;
}

#ifdef __cplusplus
}
#endif
