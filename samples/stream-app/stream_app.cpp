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
#include <unistd.h>

#include <csignal>
#include <future>
#include <iostream>
#include <memory>
#include <utility>

#include "cxxutil/log.h"
#include "detection_runner.h"
#include "device/mlu_context.h"
#include "video_decoder.h"

DEFINE_bool(show, false, "show image");
DEFINE_bool(save_video, true, "save output to local video file");
DEFINE_int32(repeat_time, 0, "process repeat time");
DEFINE_string(data_path, "", "video path");
DEFINE_string(model_path, "", "infer offline model path");
DEFINE_string(label_path, "", "label path");
DEFINE_string(func_name, "subnet0", "model function name");
DEFINE_string(track_model_path, "", "track model path");
DEFINE_string(track_func_name, "subnet0", "track model function name");
DEFINE_int32(wait_time, 0, "time of one test case");
DEFINE_string(net_type, "", "neural network type, SSD or YOLOv3");
DEFINE_int32(dev_id, 0, "run sample on which device");
DEFINE_string(decode_type, "mlu", "decode type, choose from mlu/ffmpeg/ffmpeg-mlu.");

std::shared_ptr<StreamRunner> g_runner;
bool g_exit = false;

void HandleSignal(int sig) {
  g_runner->Stop();
  g_exit = true;
  LOGI(SAMPLES) << "Got INT signal, ready to exit!";
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  edk::log::InitLogging(true, true);

  // check params
  CHECK(SAMPLES, FLAGS_data_path. size() != 0u);  // NOLINT
  CHECK(SAMPLES, FLAGS_model_path.size() != 0u);  // NOLINT
  CHECK(SAMPLES, FLAGS_func_name. size() != 0u);  // NOLINT
  CHECK(SAMPLES, FLAGS_label_path.size() != 0u);  // NOLINT
  CHECK(SAMPLES, FLAGS_net_type.  size() != 0u);  // NOLINT
  CHECK(SAMPLES, FLAGS_wait_time >= 0);    // NOLINT
  CHECK(SAMPLES, FLAGS_repeat_time >= 0);  // NOLINT
  CHECK(SAMPLES, FLAGS_dev_id >= 0);       // NOLINT

  VideoDecoder::DecoderType decode_type = VideoDecoder::EASY_DECODE;
  if (FLAGS_decode_type == "ffmpeg" || FLAGS_decode_type == "FFmpeg") {
    decode_type = VideoDecoder::FFMPEG;
  } else if (FLAGS_decode_type == "ffmpeg_mlu" || FLAGS_decode_type == "ffmpeg-mlu") {
    decode_type = VideoDecoder::FFMPEG_MLU;
  }
  try {
    g_runner = std::make_shared<DetectionRunner>(decode_type, FLAGS_dev_id,
                                                 FLAGS_model_path, FLAGS_func_name, FLAGS_label_path,
                                                 FLAGS_track_model_path, FLAGS_track_func_name,
                                                 FLAGS_data_path, FLAGS_net_type,
                                                 FLAGS_show, FLAGS_save_video);
  } catch (edk::Exception& e) {
    LOGE(SAMPLES) << "Create stream runner failed" << e.what();
    return -1;
  }

  std::future<bool> process_loop_return = std::async(std::launch::async, &StreamRunner::RunLoop, g_runner.get());

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }
  signal(SIGALRM, HandleSignal);

  // set mlu environment
  edk::MluContext context;
  context.SetDeviceId(FLAGS_dev_id);
  context.BindDevice();

  g_runner->DemuxLoop(FLAGS_repeat_time);

  process_loop_return.wait();
  g_runner.reset();

  if (!process_loop_return.get()) {
    return 1;
  }

  LOGI(SAMPLES) << "run stream app SUCCEED!!!" << std::endl;
  edk::log::ShutdownLogging();
  return 0;
}
