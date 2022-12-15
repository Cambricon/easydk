/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <unistd.h>

#include <csignal>
#include <future>
#include <iostream>
#include <memory>
#include <utility>

#include "detection_runner.h"
#include "video_decoder.h"

#include "cnedk_vin_capture.h"
#include "cnedk_vout_display.h"
#include "cnedk_platform.h"

DEFINE_bool(show, false, "show image");
DEFINE_bool(save_video, true, "save output to local video file");
DEFINE_int32(repeat_time, 0, "process repeat time");
DEFINE_string(data_path, "", "video path");
DEFINE_string(model_path, "", "infer offline model path");
DEFINE_string(label_path, "", "label path");
DEFINE_int32(wait_time, 0, "time of one test case");
DEFINE_int32(dev_id, 0, "run sample on which device");
DEFINE_string(decode_type, "mlu", "decode type, choose from mlu/ffmpeg/ffmpeg-mlu.");
DEFINE_bool(enable_vin, false, "enable_vin");  // not support vin enable
DEFINE_bool(enable_vout, false, "enable_vout");  // not support vout enable
DEFINE_int32(codec_id_start, 0, "vdec/venc first id, for CE3226 only");

std::shared_ptr<StreamRunner> g_runner;
bool g_exit = false;

void HandleSignal(int sig) {
  g_runner->Stop();
  g_exit = true;
  LOG(INFO) << "[EasyDK Samples] [Detection] Got INT signal, ready to exit!";
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_stderrthreshold = google::INFO;
  FLAGS_colorlogtostderr = true;

  // check params
  CHECK(FLAGS_data_path. size() != 0u) << "[EasyDK Samples] [Detection] data path is empty";  // NOLINT
  CHECK(FLAGS_model_path.size() != 0u) << "[EasyDK Samples] [Detection] model path is empty";  // NOLINT
  CHECK(FLAGS_label_path.size() != 0u) << "[EasyDK Samples] [Detection] label path is empty";  // NOLINT
  CHECK(FLAGS_wait_time >= 0) << "[EasyDK Samples] [Detection] wait time should be >= 0";    // NOLINT
  CHECK(FLAGS_repeat_time >= 0) << "[EasyDK Samples] [Detection] repeat time should be >= 0";  // NOLINT
  CHECK(FLAGS_dev_id >= 0) << "[EasyDK Samples] [Detection] device id should be >= 0";       // NOLINT
  CHECK(FLAGS_codec_id_start >= 0) "[EasyDK Samples] [Detection] codec start id should be >= 0"; // NOLINT


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
    LOG(ERROR) << "[EasyDK Samples] [Detection] Init platform failed";
    return -1;
  }

  VideoDecoder::DecoderType decode_type = VideoDecoder::EASY_DECODE;
  if (FLAGS_decode_type == "ffmpeg" || FLAGS_decode_type == "FFmpeg") {
    decode_type = VideoDecoder::FFMPEG;
  } else if (FLAGS_decode_type == "ffmpeg_mlu" || FLAGS_decode_type == "ffmpeg-mlu") {
    decode_type = VideoDecoder::FFMPEG_MLU;
  }
  try {
    g_runner = std::make_shared<DetectionRunner>(decode_type, FLAGS_dev_id,
                                                 FLAGS_model_path, FLAGS_label_path,
                                                 FLAGS_data_path, FLAGS_show, FLAGS_save_video);
  } catch (...) {
    LOG(ERROR) << "[EasyDK Samples] [Detection] Create stream runner failed";
    return -1;
  }

  std::future<bool> process_loop_return = std::async(std::launch::async, &StreamRunner::RunLoop, g_runner.get());

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }

  signal(SIGALRM, HandleSignal);

  g_runner->DemuxLoop(FLAGS_repeat_time);

  process_loop_return.wait();
  g_runner.reset();

  if (!process_loop_return.get()) {
    return 1;
  }

  LOG(INFO) << "[EasyDK Samples] [Detection] Run SUCCEED!!!";
  google::ShutdownGoogleLogging();
  return 0;
}
