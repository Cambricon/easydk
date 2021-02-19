#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "opencv2/opencv.hpp"

#include "device/mlu_context.h"
#include "easyplugin/resize_yuv_to_rgba.h"
#include "../test_base.h"

#define SAVE_RESULT 0

static std::string exe_path = GetExePath();  //NOLINT
static std::string dir = "../../tests/data/";  // NOLINT
static std::mutex print_mutex;

struct TestResizeParam {
  int src_w;
  int src_h;
  int dst_w;
  int dst_h;
  int bsize;
  int core_number;
  bool yuv_nv12;
};

#if SAVE_RESULT
static void SaveImg(cv::Mat yuv_img, bool yuv_nv12, int cnt, std::string prefix) {
  cv::Mat rgb_image;
  int CV_MODE = yuv_nv12 ? CV_YUV2RGB_NV12 : CV_YUV2RGB_NV21;
  // transfer from yuv to rgb
  cvtColor(yuv_img, rgb_image, CV_MODE);
  // save RGB image
  cv::imwrite(exe_path + dir + prefix +  std::to_string(cnt) + ".jpg", rgb_image);
}
#endif

static void Rgb2Yuv(std::string path, TestResizeParam p, uint8_t* cpu_input, int frame_cnt) {
  cv::Mat src_image, src_yuv_image;
  // read src image
  src_image = cv::imread(exe_path + dir + path, CV_LOAD_IMAGE_COLOR);
  ASSERT_FALSE(src_image.empty()) << "read \"" << exe_path + dir + path << "\" failed";
  int src_img_area = p.src_w * p.src_h;
  // resize to src h x w
  cv::resize(src_image, src_image, cv::Size(p.src_w, p.src_h));
  // bgr 2 yuv 420
  cvtColor(src_image, src_yuv_image, CV_BGR2YUV_I420);
  // yuv to yuv nv12 or nv21
  uint8_t* srcU_ = reinterpret_cast<uint8_t*>(src_yuv_image.data) + src_img_area;
  uint8_t* srcV_ = srcU_ + src_img_area / 4;
  uint8_t* srcUV = cpu_input + src_img_area;
  memcpy(cpu_input, reinterpret_cast<uint8_t*>(src_yuv_image.data), src_img_area);

  for (int i = 0; i < src_img_area / 4; i++) {
    if (!p.yuv_nv12) {
      (*srcUV++) = (*srcU_++);
      (*srcUV++) = (*srcV_++);
    } else {
      (*srcUV++) = (*srcV_++);
      (*srcUV++) = (*srcU_++);
    }
  }
#if SAVE_RESULT
  // uncomment to save image
  src_yuv_image.data = (unsigned char*)cpu_input;
  SaveImg(src_yuv_image, p.yuv_nv12, frame_cnt, "src_");
#endif
}

#if SAVE_RESULT
static void D2H(uint8_t* cpu_output, void* mlu_output, TestResizeParam param) {
  cv::Mat dst_rgb_image;
  int dst_img_size = param.dst_w * param.dst_h * 4;
  // copy result from mlu to cpu
  cnrtMemcpy(cpu_output, mlu_output, dst_img_size * param.bsize, CNRT_MEM_TRANS_DIR_DEV2HOST);
  // save to rgb
  dst_rgb_image = cv::Mat(cv::Size(param.dst_w, param.dst_h), CV_8UC4, cv::Scalar::all(0));
  for (int i = 0; i < param.bsize; i++) {
    dst_rgb_image.data = cpu_output + dst_img_size * i;
    cv::imwrite(exe_path + dir + "dst_" +  std::to_string(i) + ".jpg", dst_rgb_image);
  }
}
#endif

static void H2D(TestResizeParam param, std::vector<std::string> image_name, void** mlu_input, void** mlu_output,
                uint8_t** cpu_input, uint8_t** cpu_output) {
  int src_img_size = param.src_w * param.src_h * 3 / 2;
  int dst_img_size = param.dst_w * param.dst_h * 4;
  if (*cpu_input != nullptr) {
    free(*cpu_input);
  }
  if (*cpu_output != nullptr) {
    free(*cpu_output);
  }
  *cpu_input = reinterpret_cast<uint8_t*>(malloc(sizeof(char*) * src_img_size * param.bsize));
  *cpu_output = reinterpret_cast<uint8_t*>(malloc(sizeof(char*) * dst_img_size * param.bsize));
  memset(*cpu_output, 0, sizeof(uint8_t) * dst_img_size * param.bsize);

  for (int i = 0; i < param.bsize; i++) {
    Rgb2Yuv(image_name[i], param, *cpu_input + i * src_img_size, i);
  }

  cnrtMalloc(reinterpret_cast<void**>(mlu_input), sizeof(uint8_t) * src_img_size * param.bsize);
  cnrtMalloc(reinterpret_cast<void**>(mlu_output), sizeof(uint8_t) * dst_img_size * param.bsize);
  cnrtMemcpy(reinterpret_cast<void*>(*mlu_input), *cpu_input, src_img_size * param.bsize, CNRT_MEM_TRANS_DIR_HOST2DEV);
}

static void RunPluginResizeYuv2Rgba(void* mlu_input, void* mlu_output, TestResizeParam param,
                                    std::vector<std::string> image_name, uint8_t channel_id, int batch_num) {
  // set context
  edk::MluContext context;
  context.SetDeviceId(0);
  {
    std::lock_guard<std::mutex> lock_g(print_mutex);
    std::cout << "channle id  " << channel_id % 4 << std::endl;
  }
  context.SetChannelId(channel_id % 4);
  context.ConfigureForThisThread();

  edk::MluResizeAttr attr;
  attr.src_h = param.src_h;
  attr.src_w = param.src_w;
  attr.src_stride = param.src_w;
  attr.dst_h = param.dst_h;
  attr.dst_w = param.dst_w;
  attr.crop_x = 0;
  attr.crop_y = 0;
  attr.crop_w = param.src_w;
  attr.crop_h = param.src_h;
  if (param.yuv_nv12) {
    attr.color_mode = edk::ColorMode::YUV2RGBA_NV12;
  } else {
    attr.color_mode = edk::ColorMode::YUV2RGBA_NV21;
  }
  attr.fill_color_r = 255;
  attr.fill_color_g = 0;
  attr.fill_color_b = 0;
  attr.keep_aspect_ratio = 0;
  attr.batch_size = param.bsize;
  attr.core_version = edk::CoreVersion::MLU270;
  attr.core_number = param.core_number;

  edk::MluResizeYuv2Rgba *resize = new edk::MluResizeYuv2Rgba();
  if (!resize->Init(attr)) {
    std::cout << "resize->Init() failed" << std::endl;;
  }

  // batching up
  void **src_y_mlu_in_cpu = reinterpret_cast<void**>(malloc(param.bsize * sizeof(void*)));
  void **src_uv_mlu_in_cpu = reinterpret_cast<void**>(malloc(param.bsize * sizeof(void*)));
  int src_img_size = param.src_w * param.src_h * 3 / 2;
  while (batch_num--) {
    for (int i = 0; i < param.bsize; i++) {
      src_y_mlu_in_cpu[i] = reinterpret_cast<uint8_t*>(mlu_input) + i * src_img_size;
      src_uv_mlu_in_cpu[i] = reinterpret_cast<uint8_t*>(mlu_input) + i * src_img_size + param.src_w * param.src_h;
      resize->BatchingUp(src_y_mlu_in_cpu[i], src_uv_mlu_in_cpu[i]);
    }
    // compute forward
    resize->SyncOneOutput(mlu_output);
  }
  // release resources
  if (src_y_mlu_in_cpu) free(reinterpret_cast<void*>(src_y_mlu_in_cpu));
  if (src_uv_mlu_in_cpu) free(reinterpret_cast<void*>(src_uv_mlu_in_cpu));
  if (resize) {
    resize->Destroy();
    delete resize;
  }
}

static void Process(const TestResizeParam& param, std::vector<std::string> image_name, uint32_t thread_num,
                    int batch_num) {
  // set context
  edk::MluContext context;
  context.SetDeviceId(0);
  context.ConfigureForThisThread();
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
  std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
  uint8_t *cpu_input = nullptr, *cpu_output = nullptr;
  std::vector<void*> mlu_outputs, mlu_inputs;
  mlu_inputs.clear();
  mlu_outputs.clear();
  for (uint32_t th_i = 0; th_i < thread_num ; ++th_i) {
    // set context
    context.SetChannelId(th_i % 2);
    context.ConfigureForThisThread();
    mlu_inputs.push_back(nullptr);
    mlu_outputs.push_back(nullptr);
    H2D(param, image_name, &mlu_inputs[th_i], &mlu_outputs[th_i], &cpu_input, &cpu_output);
  }
  start_time = end_time = std::chrono::high_resolution_clock::now();
  std::vector<std::thread> ths;
  for (uint32_t th_i = 0; th_i < thread_num ; ++th_i) {
    ths.push_back(
        std::thread(&RunPluginResizeYuv2Rgba, mlu_inputs[th_i], mlu_outputs[th_i], param, image_name, th_i, batch_num));
  }
  for (auto &it : ths) {
    if (it.joinable()) {
      it.join();
    }
  }
  end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> diff = end_time - start_time;
  std::cout << "===================== core number : " << param.core_number << " =======================" << std::endl;
  std::cout << "  bsize = " << param.bsize << "\n";
  std::cout << "  src_h = " << param.src_h << ", src_w = " << param.src_w << "\n"
            << "  dst_h = " << param.dst_h << ", dst_w = " << param.dst_w << std::endl;
  std::cout << "=================== total time " << diff.count() << "ms =====================" <<std::endl << std::endl;
#if SAVE_RESULT
  // uncomment to copy result to host and save image
  D2H(cpu_output, mlu_outputs[0], param);
#endif
  for (uint32_t th_i = 0; th_i < thread_num ; ++th_i) {
    // set context
    context.SetChannelId(th_i % 2);
    context.ConfigureForThisThread();
    cnrtFree(mlu_inputs[th_i]);
    cnrtFree(mlu_outputs[th_i]);
  }
  if (cpu_input) free(cpu_input);
  if (cpu_output) free(cpu_output);
}

// test cnplugin resize yuv to rgba op
TEST(resize, resize_yuv2rgba_cnplugin) {
  int src_width = 1920;
  int src_height = 1080;
  int dst_width = 352;
  int dst_height = 288;
  int batch_size = 16;
  int core_number = 4;
  bool yuv_nv12 = true;
  uint32_t thread_num = 2;
  int batch_num = 1;
  std::vector<std::string> image_name;
  std::string image_1 = "0.jpg";
  std::string image_2 = "1.jpg";
  for (int i = 0; i < batch_size / 2 + 1; i++) {
    image_name.push_back(image_1);
    image_name.push_back(image_2);
  }
  TestResizeParam
     param = {src_width, src_height, dst_width, dst_height, batch_size, core_number, yuv_nv12};
  Process(param, image_name, thread_num, batch_num);
}
