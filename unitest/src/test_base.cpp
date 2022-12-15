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

#include "test_base.h"

#include <unistd.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

#define PATH_MAX_LENGTH 1024

std::string GetExePath() {
  char path[PATH_MAX_LENGTH];
  int cnt = readlink("/proc/self/exe", path, PATH_MAX_LENGTH);
  if (cnt < 1 || cnt >= PATH_MAX_LENGTH) {
    return "";
  }
  if (path[cnt - 1] == '/') {
    path[cnt - 1] = '\0';
  } else {
    path[cnt] = '\0';
  }
  std::string result(path);
  return std::string(path).substr(0, result.find_last_of('/') + 1);
}

int InitPlatform(bool enable_vin, bool enable_vout) {
  CnedkSensorParams sensor_params[4];
  memset(sensor_params, 0, sizeof(CnedkSensorParams) * 4);

  CnedkPlatformConfig config;
  memset(&config, 0, sizeof(config));

  config.codec_id_start = 0;

  CnedkVoutParams vout_params;
  memset(&vout_params, 0, sizeof(CnedkVoutParams));

  if (enable_vout) {
    config.vout_params = &vout_params;
    vout_params.max_input_width = 1920;
    vout_params.max_input_height = 1080;
    vout_params.input_format = 0;  // not used at the moment
  }

  if (enable_vin) {
    config.sensor_num = 1;
    config.sensor_params = sensor_params;
    sensor_params[0].sensor_type = 6;
    sensor_params[0].mipi_dev = 1;
    sensor_params[0].bus_id = 0;
    sensor_params[0].sns_clk_id = 1;
    sensor_params[0].out_width = 1920;
    sensor_params[0].out_height = 1080;
    sensor_params[0].output_format = 0;  // not used at the moment
  }
  CnedkPlatformInit(&config);
  return 0;
}

static int g_device_id = 0;

static std::string MMVersionForCe3226() {
  return "v0.13.0";
}
static std::string MMVersionForMlu370() {
  return "v0.13.0";
}
static std::string MMVersionForMlu590() {
  return "v0.14.0";
}

const std::unordered_map<std::string, std::pair<std::string, std::string>> g_model_info = {
    {"resnet50_CE3226",
        {"resnet50_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForCe3226() +
         "/resnet50_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind"}
    },
    {"resnet50_MLU370",
        {"resnet50_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu370() +
         "/resnet50_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind"}
    },
    {"resnet50_MLU590",
        {"resnet50_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu590() +
         "/resnet50_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind"}
    },
    {"feature_extract_CE3226",
        {"feature_extract_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForCe3226() +
         "/feature_extract_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind"}
    },
    {"feature_extract_MLU370",
        {"feature_extract_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu370() +
         "/feature_extract_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind"}
    },
    {"feature_extract_MLU590",
        {"feature_extract_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu590() +
         "/feature_extract_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind"}
    },
    {"yolov3_CE3226",
        {"yolov3_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForCe3226() +
         "/yolov3_" + MMVersionForCe3226() + "_4b_rgb_uint8.magicmind"}
    },
    {"yolov3_MLU370",
        {"yolov3_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu370() +
         "/yolov3_" + MMVersionForMlu370() + "_4b_rgb_uint8.magicmind"}
    },
    {"yolov3_MLU590",
        {"yolov3_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind",
         "http://video.cambricon.com/models/magicmind/" + MMVersionForMlu590() +
         "/yolov3_" + MMVersionForMlu590() + "_4b_rgb_uint8.magicmind"}
    }
};

std::string GetModelInfoStr(std::string model_name, std::string info_type) {
  CnedkPlatformInfo platform_info;
  CnedkPlatformGetInfo(g_device_id, &platform_info);
  std::string platform_name(platform_info.name);
  std::string model_key;
  if (platform_name.rfind("MLU5", 0) == 0) {
    model_key = model_name + "_MLU590";
  } else {
    model_key = model_name + "_" + platform_name;
  }
  if (g_model_info.find(model_key) != g_model_info.end()) {
    if (info_type == "name") {
      return g_model_info.at(model_key).first;
    } else {
      return g_model_info.at(model_key).second;
    }
  }
  return "";
}
