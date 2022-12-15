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

#include <signal.h>

#include <chrono>
#include <memory>

#include "gflags/gflags.h"

#include "cnedk_platform.h"

#include "easy_module.hpp"
#include "easy_pipeline.hpp"

#include "sample_decode.hpp"
#include "sample_encode.hpp"
#include "sample_sync_inference.hpp"
#include "sample_async_inference.hpp"
#include "sample_osd.hpp"

DEFINE_string(data_path, "", "video path");
DEFINE_string(model_name, "", "model name");
DEFINE_string(model_path, "", "infer offline model path");
DEFINE_string(label_path, "", "label path");
DEFINE_string(output_name, "", "encode output filename");
DEFINE_int32(input_number, 1, "input file number");
DEFINE_int32(device_id, 0, "run sample on which device");
DEFINE_bool(enable_vin, false, "enable_vin");  // not support vin enable
DEFINE_bool(enable_vout, false, "enable_vout");  // not support vout enable
DEFINE_int32(codec_id_start, 0, "vdec/venc first id, for CE3226 only");
DEFINE_int32(frame_rate, 0, "framerate for stream");

std::shared_ptr<EasyPipeline> g_easy_pipe;

void HandleSignal(int sig) {
  LOG(INFO) << "[EasyDK Samples] Got INT signal, ready to exit!";
  if (g_easy_pipe) g_easy_pipe->Stop();
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_stderrthreshold = google::INFO;
  FLAGS_colorlogtostderr = true;

  // check params
  CHECK(FLAGS_data_path. size() != 0u) << "[EasyDK Samples] [Detection] data path is empty";
  CHECK(FLAGS_model_name. size() != 0u) << "[EasyDK Samples] [Detection] model name is empty";
  CHECK(FLAGS_model_path.size() != 0u) << "[EasyDK Samples] [Detection] model path is empty";
  CHECK(FLAGS_label_path.size() != 0u) << "[EasyDK Samples] [Detection] label path is empty";
  CHECK(FLAGS_device_id >= 0) << "[EasyDK Samples] [Detection] device id should be >= 0";
  CHECK(FLAGS_codec_id_start >= 0) "[EasyDK Samples] [Detection] codec start id should be >= 0";
  CHECK(FLAGS_input_number >= 1) "[EasyDK Samples] [Detection] input number should be >= ";
  CHECK(FLAGS_frame_rate >= 1) "[EasyDK Samples] [Detection] input number should be >= ";

  CnedkSensorParams sensor_params[4];
  memset(sensor_params, 0, sizeof(CnedkSensorParams) * 4);
  CnedkVoutParams vout_params;
  memset(&vout_params, 0, sizeof(CnedkVoutParams));

  CnedkPlatformConfig config;
  memset(&config, 0, sizeof(config));
  if (FLAGS_codec_id_start) {
    config.codec_id_start = FLAGS_codec_id_start;
  }

  if (FLAGS_enable_vout) {
    config.vout_params = &vout_params;
    vout_params.max_input_width = 1920;
    vout_params.max_input_height = 1080;
    vout_params.input_format = 0;  // not used at the moment
  }

  if (FLAGS_enable_vin) {
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

  if (CnedkPlatformInit(&config) < 0) {
    LOG(ERROR) << "[EasyDK Samples] Init platform failed";
    return -1;
  }

  g_easy_pipe = std::make_shared<EasyPipeline>();

  int ret = 0;
  for (int i = 0; i < FLAGS_input_number; ++i) {
    std::shared_ptr<EasyModule> source =
        std::make_shared<SampleDecode>("source", 0, FLAGS_device_id, FLAGS_data_path, i, FLAGS_frame_rate);
    ret |= g_easy_pipe->AddSource(source);
  }

  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Samples] Add Source failed";
    return -1;
  }

  std::shared_ptr<EasyModule> infer =
      std::make_shared<SampleAsyncInference>("infer", 1, FLAGS_device_id, FLAGS_model_path, FLAGS_model_name);
  // std::shared_ptr<EasyModule> infer =
  //     std::make_shared<SampleSyncInference>("infer", 8, FLAGS_device_id, FLAGS_model_path, FLAGS_model_name);

  std::shared_ptr<EasyModule> osd = std::make_shared<SampleOsd>("osd", 1, FLAGS_label_path);
  std::shared_ptr<EasyModule> encode = std::make_shared<SampleEncode>("encode", 1, FLAGS_device_id, FLAGS_output_name);

  ret |= g_easy_pipe->AddModule(infer);
  ret |= g_easy_pipe->AddModule(osd);
  ret |= g_easy_pipe->AddModule(encode);

  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Samples] Add EasyModule failed";
    return -1;
  }

  g_easy_pipe->AddLink("source", "infer");
  g_easy_pipe->AddLink("infer", "osd");
  g_easy_pipe->AddLink("osd", "encode");

  signal(SIGINT, HandleSignal);

  // auto start = std::chrono::high_resolution_clock::now();
  ret = g_easy_pipe->Start();
  if (ret == 0) {
    g_easy_pipe->WaitForStop();
  } else {
    LOG(ERROR) << "[EasyDK Samples] Start pipe failed";
  }
  g_easy_pipe->Stop();
  infer.reset();
  g_easy_pipe.reset();
  g_easy_pipe = nullptr;

  // auto stop = std::chrono::high_resolution_clock::now();
  // auto dura = std::chrono::duration_cast<std::chrono::milliseconds>
  //                            (stop.time_since_epoch() - start.time_since_epoch()).count();

  // LOG(ERROR) << dura;
  CnedkPlatformUninit();
  google::ShutdownGoogleLogging();

  return 0;
}
