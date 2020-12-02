#include <gflags/gflags.h>
#include <glog/logging.h>
#include <opencv2/opencv.hpp>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>

#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cnosd.h"
#include "cnpostproc.h"
#include "device/mlu_context.h"
#include "easybang/resize_and_colorcvt.h"
#include "easycodec/easy_decode.h"
#include "easyinfer/easy_infer.h"
#include "easyinfer/mlu_memory_op.h"
#include "easyinfer/model_loader.h"
#include "easytrack/easy_track.h"
#include "feature_extractor.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#define FFMPEG_VERSION_3_1 AV_VERSION_INT(57, 40, 100)

DEFINE_bool(show, true, "show image");
DEFINE_int32(repeat_time, 0, "process repeat time");
DEFINE_string(data_path, "", "video path");
DEFINE_string(model_path, "", "infer offline model path");
DEFINE_string(label_path, "", "label path");
DEFINE_string(func_name, "subnet0", "model function name");
DEFINE_string(track_model_path, "", "track model path");
DEFINE_string(track_func_name, "subnet0", "track model function name");
DEFINE_int32(wait_time, 0, "time of one test case");

using edk::Shape;
using edk::CnPostproc;
using edk::MluResizeConvertOp;

// send frame queue
static std::queue<edk::CnFrame> g_frames;
static std::mutex g_mut;
static std::condition_variable g_cond;

// params for ffmpeg unpack
static AVFormatContext *g_p_format_ctx;
static AVBitStreamFilterContext *g_p_bsfc;
static AVPacket g_packet;
static AVDictionary *g_options = NULL;
static int32_t g_video_index;
static const char *g_url = "";
static uint64_t g_frame_index;
static bool g_running = false;
static bool g_exit = false;
static bool g_receive_eos = false;
static edk::EasyDecode *g_decode;

bool prepare_video_resource() {
  // init ffmpeg
  avcodec_register_all();
  av_register_all();
  avformat_network_init();
  // format context
  g_p_format_ctx = avformat_alloc_context();
  // g_options
  av_dict_set(&g_options, "buffer_size", "1024000", 0);
  av_dict_set(&g_options, "stimeout", "200000", 0);
  // open input
  int ret_code = avformat_open_input(&g_p_format_ctx, g_url, NULL, &g_options);
  if (0 != ret_code) {
    LOG(ERROR) << "couldn't open input stream.";
    return false;
  }
  // find video stream information
  ret_code = avformat_find_stream_info(g_p_format_ctx, NULL);
  if (ret_code < 0) {
    LOG(ERROR) << "couldn't find stream information.";
    return false;
  }
  g_video_index = -1;
  AVStream *vstream = nullptr;
  for (uint32_t iloop = 0; iloop < g_p_format_ctx->nb_streams; iloop++) {
    vstream = g_p_format_ctx->streams[iloop];
    if (vstream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      g_video_index = iloop;
      break;
    }
  }
  if (g_video_index == -1) {
    LOG(ERROR) << "didn't find a video stream.";
    return false;
  }
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  auto codec_id = vstream->codecpar->codec_id;
#else
  auto codec_id = vstream->codec->codec_id;
#endif
  // bitstream filter
  g_p_bsfc = nullptr;
  if (strstr(g_p_format_ctx->iformat->name, "mp4") || strstr(g_p_format_ctx->iformat->name, "flv") ||
      strstr(g_p_format_ctx->iformat->name, "matroska") || strstr(g_p_format_ctx->iformat->name, "rtsp")) {
    if (AV_CODEC_ID_H264 == codec_id) {
      g_p_bsfc = av_bitstream_filter_init("h264_mp4toannexb");
    } else if (AV_CODEC_ID_HEVC == codec_id) {
      g_p_bsfc = av_bitstream_filter_init("hevc_mp4toannexb");
    } else {
      LOG(ERROR) << "nonsupport codec id.";
      return false;
    }
  }
  return true;
}

void ClearResources() {
  std::cout << "Clear ffmpeg resources" << std::endl;
  if (g_p_format_ctx) {
    avformat_close_input(&g_p_format_ctx);
    avformat_free_context(g_p_format_ctx);
    av_dict_free(&g_options);
    g_p_format_ctx = nullptr;
    g_options = nullptr;
  }
  if (g_p_bsfc) {
    av_bitstream_filter_close(g_p_bsfc);
    g_p_bsfc = nullptr;
  }
}

bool unpack_data(edk::CnPacket *frame) {
  static bool init = false;
  static bool first_frame = true;
  if (!init) {
    if (!prepare_video_resource()) {
      LOG(ERROR) << "open video file error";
      return false;
    } else {
      init = true;
    }
  }
#ifdef DUMP_STREAM
  static std::ofstream of("cars.h264");
  if (!of.is_open()) {
    exit(-1);
  }
#endif
  if (av_read_frame(g_p_format_ctx, &g_packet) >= 0) {
    if (g_packet.stream_index == g_video_index) {
      auto vstream = g_p_format_ctx->streams[g_video_index];
       if (first_frame) {
         if (g_packet.flags & AV_PKT_FLAG_KEY) first_frame = false;
       }
      if (!first_frame) {
        if (g_p_bsfc) {
          av_bitstream_filter_filter(g_p_bsfc, vstream->codec, NULL, reinterpret_cast<uint8_t **>(&frame->data),
                                     reinterpret_cast<int *>(&frame->length), g_packet.data, g_packet.size, 0);
        } else {
          frame->data = g_packet.data;
          frame->length = g_packet.size;
        }
        frame->pts = g_frame_index++;
#ifdef DUMP_STREAM
        of.write(reinterpret_cast<char*>(frame->data), frame->length);
#endif
        return true;
      }
    }
    av_packet_unref(&g_packet);
  } else {
#ifdef DUMP_STREAM
    of.close();
#endif
    return false;
  }
  return true;
}

void decode_output_callback(const edk::CnFrame &info) {
  std::unique_lock<std::mutex> lk(g_mut);
  g_frames.push(info);
  g_cond.notify_one();
}

void decode_eos_callback() { g_receive_eos = true; }

void send_eos(edk::EasyDecode *decode) {
  edk::CnPacket pending_frame;
  pending_frame.data = nullptr;
  decode->SendData(pending_frame, true);
}

bool run() {
  std::unique_lock<std::mutex> lk(g_mut);
  edk::MluContext context;
  std::shared_ptr<edk::ModelLoader> model;
  std::shared_ptr<FeatureExtractor> feature_extractor;
  edk::MluMemoryOp mem_op;
  edk::EasyInfer infer;
  std::unique_ptr<edk::EasyDecode> decode = nullptr;
  edk::FeatureMatchTrack *tracker = nullptr;
  edk::MluResizeConvertOp rc_op;

  CnOsd osd;
  osd.set_rows(1);
  osd.set_cols(1);
  osd.LoadLabels(FLAGS_label_path);

  Shape in_shape;
  std::vector<Shape> out_shapes;
  std::vector<edk::DetectObject> track_result;
  std::vector<edk::DetectObject> detect_result;
  void **mlu_output = nullptr, **cpu_output = nullptr;

  try {
    // load offline model
    model = std::make_shared<edk::ModelLoader>(FLAGS_model_path.c_str(), FLAGS_func_name.c_str());
    model->InitLayout();
    in_shape = model->InputShapes()[0];
    out_shapes = model->OutputShapes();

    // set mlu environment
    context.SetDeviceId(0);
    context.BindDevice();

    // prepare mlu memory operator and memory
    mem_op.SetModel(model);

    // init cninfer
    infer.Init(model, 0);

    // create decoder
    edk::EasyDecode::Attr attr;
    attr.frame_geometry.w = 1920;
    attr.frame_geometry.h = 1080;
    attr.codec_type = edk::CodecType::H264;
    attr.pixel_format = edk::PixelFmt::NV21;
    attr.dev_id = 0;
    attr.frame_callback = decode_output_callback;
    attr.eos_callback = decode_eos_callback;
    attr.silent = false;
    attr.input_buffer_num = 6;
    attr.output_buffer_num = 6;
    decode = edk::EasyDecode::New(attr);
    g_decode = decode.get();

    tracker = new edk::FeatureMatchTrack;
    feature_extractor = std::make_shared<FeatureExtractor>();
    if (FLAGS_track_model_path != "" && FLAGS_track_model_path != "cpu") {
      feature_extractor->Init(FLAGS_track_model_path.c_str(), FLAGS_track_func_name.c_str());
    }
  } catch (edk::Exception &err) {
    LOG(ERROR) << err.what();
    g_cond.notify_one();
    lk.unlock();
    g_running = false;
    return false;
  }

  g_running = true;
  g_cond.notify_one();
  lk.unlock();

  // create postprocessor
  auto postproc = new edk::SsdPostproc;
  postproc->set_threshold(0.6);
  assert(nullptr != postproc);

  std::once_flag rcop_init_flag;

  void **mlu_input = mem_op.AllocMluInput();
  try {
    mlu_output = mem_op.AllocMluOutput();
    cpu_output = mem_op.AllocCpuOutput();

    while (g_running || !g_exit) {
      // inference
      std::unique_lock<std::mutex> inner_lk(g_mut);

      if (!g_cond.wait_for(inner_lk, std::chrono::milliseconds(100), [] { return !g_frames.empty(); })) {
        continue;
      }
      edk::CnFrame frame = g_frames.front();
      g_frames.pop();

      // Init resize and convert operator
      std::call_once(rcop_init_flag,
          [&] {
            // create mlu resize and convert op
            MluResizeConvertOp::Attr attr;
            attr.dst_h = in_shape.h;
            attr.dst_w = in_shape.w;
            attr.batch_size = 1;
            attr.core_version = context.GetCoreVersion();
            rc_op.SetMluQueue(infer.GetMluQueue());
            if (!rc_op.Init(attr)) {
              THROW_EXCEPTION(edk::Exception::INTERNAL, rc_op.GetLastError());
            }
          });

      // run resize and convert
      void *rc_output = mlu_input[0];
      edk::MluResizeConvertOp::InputData input;
      input.planes[0] = frame.ptrs[0];
      input.planes[1] = frame.ptrs[1];
      input.src_w = frame.width;
      input.src_h = frame.height;
      input.src_stride = frame.strides[0];
      rc_op.BatchingUp(input);
      if (!rc_op.SyncOneOutput(rc_output)) {
        g_running = false;
        g_exit = true;
        decode->ReleaseBuffer(frame.buf_id);
        THROW_EXCEPTION(edk::Exception::INTERNAL, rc_op.GetLastError());
      }

      // run inference
      infer.Run(mlu_input, mlu_output);
      mem_op.MemcpyOutputD2H(cpu_output, mlu_output);

      // alloc memory to store image
      auto img_data = new uint8_t[frame.strides[0] * frame.height * 3 / 2];

      // copy out frame
      decode->CopyFrameD2H(img_data, frame);

      // release codec buffer
      decode->ReleaseBuffer(frame.buf_id);
      // yuv to bgr
      cv::Mat yuv(frame.height * 3 / 2, frame.strides[0], CV_8UC1, img_data);
      cv::Mat img;
      cv::cvtColor(yuv, img, CV_YUV2BGR_NV21);
      delete[] img_data;

      // resize to show
      cv::resize(img, img, cv::Size(1280, 720));

      // post process
      std::vector<std::pair<float *, uint64_t>> postproc_param;
      postproc_param.push_back(std::make_pair(reinterpret_cast<float *>(cpu_output[0]), out_shapes[0].DataCount()));
      detect_result = postproc->Execute(postproc_param);

      // track
      edk::TrackFrame track_img;
      track_img.data = img.data;
      track_img.width = img.cols;
      track_img.height = img.rows;
      track_img.format = edk::TrackFrame::ColorSpace::RGB24;
      static int64_t frame_id = 0;
      track_img.frame_id = frame_id++;
      // extract feature
      for (auto &obj : detect_result) {
        obj.feature = feature_extractor->ExtractFeature(track_img, obj);
      }
      track_result.clear();
      tracker->UpdateFrame(track_img, detect_result, &track_result);

      osd.DrawLabel(img, track_result);
      osd.DrawChannels(img);
      osd.DrawFps(img, 20);

      if ((g_frames.size() == 0 && g_receive_eos) || !g_running) {
        break;
      }
      if (FLAGS_show) {
        auto window_name = "stream app";
        cv::imshow(window_name, img);
        cv::waitKey(5);
        // std::string fn = std::to_string(frame.frame_id) + ".jpg";
        // cv::imwrite(fn.c_str(), img);
      }
    }
  } catch (edk::Exception &err) {
    LOG(ERROR) << err.what();
    return false;
  }

  // uninitialize
  g_running = false;
  if (nullptr != mlu_output) mem_op.FreeMluOutput(mlu_output);
  if (nullptr != cpu_output) mem_op.FreeCpuOutput(cpu_output);
  if (nullptr != mlu_input) mem_op.FreeMluInput(mlu_input);
  if (nullptr != postproc) delete postproc;
  if (nullptr != tracker) delete tracker;
  return true;
}

void handle_sig(int sig) {
  g_running = false;
  g_exit = true;
  LOG(INFO) << "Got INT signal, ready to exit!";
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // check params
  CHECK_NE(FLAGS_data_path.size(), 0u);
  CHECK_NE(FLAGS_model_path.size(), 0u);
  CHECK_NE(FLAGS_func_name.size(), 0u);
  CHECK_NE(FLAGS_label_path.size(), 0u);
  CHECK_GE(FLAGS_wait_time, 0);
  CHECK_GE(FLAGS_repeat_time, 0);

  g_url = FLAGS_data_path.c_str();

  edk::CnPacket pending_frame;

  std::unique_lock<std::mutex> lk(g_mut);
  std::future<bool> loop_return = std::async(std::launch::async, &run);
  // wait for init done
  g_cond.wait(lk);
  lk.unlock();

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }
  signal(SIGALRM, handle_sig);
  signal(SIGINT, handle_sig);

  // set mlu environment
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  auto now_time = std::chrono::steady_clock::now();
  auto last_time = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> dura;
  try {
    while (g_running) {
      // sync decode
      static int loop_time = 0;
      bool eos = false;
      while (!unpack_data(&pending_frame)) {
        if (FLAGS_repeat_time > loop_time++) {
          ClearResources();
          prepare_video_resource();
          std::cout << "Loop..." << std::endl;
          continue;
        } else {
          eos = true;
          send_eos(g_decode);
          std::cout << "End Of Stream" << std::endl;
          break;
        }
      }

      if (eos) break;
      if (g_decode == nullptr) break;
      if (!g_decode->SendData(pending_frame)) break;
      if (g_p_bsfc) {
        av_free(reinterpret_cast<void *>(pending_frame.data));
      }
      av_packet_unref(&g_packet);
      now_time = std::chrono::steady_clock::now();
      dura = now_time - last_time;
      if (40 > dura.count()) {
        std::chrono::duration<double, std::milli> sleep_t(40 - dura.count());
        std::this_thread::sleep_for(sleep_t);
      }
      last_time = std::chrono::steady_clock::now();
    }
  } catch (edk::Exception &err) {
    LOG(ERROR) << err.what();
  }

  if (g_exit) {
    send_eos(g_decode);
  }

  ClearResources();

  bool ret = loop_return.get();
  if (ret) {
    std::cout << "run stream app SUCCEED!!!" << std::endl;
  }

  return ret ? 0 : 1;
}
