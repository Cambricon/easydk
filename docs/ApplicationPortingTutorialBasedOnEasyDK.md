# 基于EasyDK的应用程序MLU移植教程

> 以下所有介绍基于EasyDK提供的samples。



## samples框架

samples目录如下所示：

​	├── classification    # 分类示例
​	├── stream-app      # 检测示例
​	├── transcode         # 转码
​	├── common           # 通用功能
​	├── data                   # 数据
​	│      └── videos       # 视频
​	└── cmake               # cmake

## Common

提供video_parser, 三种video_decoder(包括EasyDecode, FFmpeg 和 FFmpeg-MLU), stream_runner, feature_extractor, preproc, postproc, osd等通用功能

### VideoParser

依赖FFmpeg，实现解析本地视频文件或rtsp流的功能

```C++
struct VideoInfo {
  AVCodecID codec_id;
  AVCodecParameters* codecpar;  // only for ffmpeg version >= 3.1
  AVCodecContext* codec_ctx;
  std::vector<uint8_t> extra_data;
  int width, height;
  int progressive;
};

class IDemuxEventHandle {
 public:
  virtual bool OnParseInfo(const VideoInfo& info) = 0;
  virtual bool OnPacket(const edk::CnPacket& frame) = 0;
  virtual void OnEos() = 0;
  virtual bool Running() = 0;
};

class VideoParser {
 public:
  explicit VideoParser(IDemuxEventHandle* handle);
  ~VideoParser();
  // url可以是本地文件或rtsp流地址，内部会自行解析。
  bool Open(const char *url, bool save_file = false);
  // -1 for error, 1 for eos
  int ParseLoop(uint32_t frame_interval);
  void Close();
  bool CheckTimeout();
  bool IsRtsp();

  const VideoInfo& GetVideoInfo() const;
};
```

提供IDemuxEventHandle指针给parser，在解析出packet或EOS时通过handle传递出去

在Open视频资源后，调用ParseLoop即可持续parse出ES帧，设置frame_interval控制帧率（frame_interval是两帧之间的间隔，单位毫秒），parse出EOS或出现error时退出loop

调用GetVideoInfo获取视频信息


### VideoDecoder

实现三种解码方式,分别是使用EasyDecode的硬解码，使用FFmpeg的软解码以及使用寒武纪FFmpeg-MLU的硬解码。

VideoDecoder继承IDemuxEventHandle，实现解封装之后的解码功能。

```C++
class IDecodeEventHandle {
 public:
  virtual void OnDecodeFrame(const edk::CnFrame& frame) = 0;
  virtual void OnEos() = 0;
};

class VideoDecoder : public IDemuxEventHandle {
 public:
  enum DecoderType {
    EASY_DECODE,
    FFMPEG,
    FFMPEG_MLU
  };
  VideoDecoder(StreamRunner* runner, DecoderType type, int device_id);
  bool OnParseInfo(const VideoInfo& info) override;
  bool OnPacket(const AVPacket* packet) override;
  void OnEos() override;
  bool Running() override;
  void SendEos();
  VideoInfo& GetVideoInfo() { return info_; }
  bool CopyFrameD2H(void *dst, const edk::CnFrame &frame);
  void ReleaseFrame(edk::CnFrame&& frame);
};
```
在构造时，通过传入不同DecoderType选择解码方式。提供IDecodeEventHandle指针给decoder，在解码后通过handle传出。

调用OnParseInfo函数初始化解码器。初始化完成后，调用OnPacket函数发送解封装后的packet数据给解码器。解码成功后通过handle的OnDecodeFrame函数传出解码后的数据。解码后的数据保存在MLU上，当不再需要解码后的数据时，通过ReleaseFrame函数释放内存。

### StreamRunner

StreamRunner在VideoParser的基础上提供repeat的功能，即读取到EOS后，回到视频头部再次开始解析，直到重复repeat_time次。

StreamRunner继承IDecodeEventHandle，将VideoDecoder解码出来的帧缓存在queue中。实现了循环获取解码出来的帧并执行Process函数的功能。

```C++
class StreamRunner : public IDecodeEventHandle {
 public:
  explicit StreamRunner(const std::string& data_path,
                        const VideoDecoder::DecoderType decode_type = VideoDecoder::EASY_DECODE,
                        int dev_id = 0);
  virtual ~StreamRunner();
  // running -> true
  void Start();
  // running -> false, notify thread blocked in RunLoop
  void Stop();

  // Get decode frame and process
  bool RunLoop();
  // process interface
  virtual void Process(edk::CnFrame frame) = 0;
  // demux loop, repeat when got EOS
  void DemuxLoop(const uint32_t repeat_time);

  // decoder event eos callback, not for user
  void OnEos() override;
  // decoder event frame callback, push frame to queue, not for user
  void OnDecodeFrame(const edk::CnFrame& info) override;

  // check if running
  bool Running();

 protected:
  // wait for run loop process done and exit
  void WaitForRunLoopExit();
  edk::MluContext env_;
  std::unique_ptr<edk::EasyDecode> decode_{nullptr};
};
```

实现具体的demo处理流程只需要重写Process虚函数，函数输入参数是解码后的MLU数据帧

### FeatureExtractor

接收frame和object，提取图像上object区域的特征值向量（128个float），有模型时使用MLU模型提取，不设置或设置无效模型路径时使用OpenCV feature2d提取。

```C++
class FeatureExtractor {
 public:
  ~FeatureExtractor();
  bool Init(const std::string& model_path, const std::string& func_name, int dev_id = 0);
  void Destroy();

  /*************************************************************************
   * @brief inference and extract feature of an object on mlu
   * @param
   *   frame[in] frame on mlu
   *   objs[in] detected objects of the frame
   * @return returns true if extract successfully, otherwise returns false.
   * ***********************************************************************/
  bool ExtractFeatureOnMlu(const edk::CnFrame& frame, std::vector<edk::DetectObject>* objs);
  /*************************************************************************
   * @brief inference and extract feature of an object on cpu
   * @param
   *   frame[in] frame on cpu
   *   objs[in] detected objects of the frame
   * @return returns true if extract successfully, otherwise returns false.
   * ***********************************************************************/
  bool ExtractFeatureOnCpu(const cv::Mat& frame, std::vector<edk::DetectObject>* obj);
  bool OnMlu();
};
```

### Preproc

模型输入预处理。提供使用Preprocessor作为预处理器的SSD预处理示例，构造时传入模型指针，设备号，以及模型输入的 pixel format。

使用Preprocessor作为预处理器，重载 ``operator()`` 作为处理函数，并将 ``operator()`` 传给InferServer::Preprocessor。

处理函数每次处理一个Batch的数据，输入参数分别为 infer_server::ModelIO* 存放模型batch输入的连续MLU内存，const infer_server::BatchData& 输入的batch数据，以及 const infer_server::ModelInfo* 模型信息。

```C++
struct PreprocSSD {
  PreprocSSD(infer_server::ModelPtr model, int dev_id, edk::PixelFmt dst_fmt);

  bool operator()(infer_server::ModelIO* model_input, const infer_server::BatchData& batch_infer_data,
                  const infer_server::ModelInfo* model);
};

int device_id = 0;
infer_server::InferServer infer_server(device_id);
infer_server::SessionDesc desc;
// ... other session descriptions
// create Preprocessor
desc.preproc = infer_server::Preprocessor::Create();
// set ProcessFunction to Preprocessor
desc.preproc->SetParams("process_function", infer_server::Preprocessor::ProcessFunction(
                        PreprocSSD(desc.model, device_id, edk::PixelFmt::BGRA)));
// create session
auto session = infer_server.CreateSyncSession(desc);
```

另外，也可以使用PreprocessorHost作为预处理器。重载 ``operator()`` 作为处理函数，并将 ``operator()`` 传给InferServer::PreprocessorHost。

处理函数每次处理一个数据，输入参数分别为 infer_server::ModelIO* 存放模型单个输入的CPU内存，const infer_server::InferData& 输入的单个数据，以及 const infer_server::ModelInfo* 模型信息。

```C++
struct PreprocHostExample {
  PreprocHostExample();

  bool operator()(infer_server::ModelIO* model_input, const infer_server::InferData& infer_data,
                  const infer_server::ModelInfo* model);
};

int device_id = 0;
infer_server::InferServer infer_server(device_id);
infer_server::SessionDesc desc;
// ... other session descriptions
// create PreprocessorHost
desc.preproc = infer_server::PreprocessorHost::Create();
// set ProcessFunction to PreprocessorHost
desc.preproc->SetParams("process_function", infer_server::Preprocessor::ProcessFunction(
                        PreprocHostExample()));
// create session
auto session = infer_server.CreateSyncSession(desc);
```

除此之外，还可以直接使用内置的PreprocessorMLU作为预处理器。支持使用CNCV，scaler(仅支持mlu220系列)做预处理。

支持一些常规的预处理，例如resize and convert, keep aspect ratio, mean, std, normalize等。

以Yolov5为例，假设预处理需要resize and conert YUV NV12 to ARGB, keep aspect ratio 和 normalize。

```C++
int device_id = 0;
infer_server::InferServer infer_server(device_id);
infer_server::SessionDesc desc;
// ... other session descriptions
// create PreprocessorMLU
desc.preproc = infer_server::video::PreprocessorMLU::Create();
// set parameters to PreprocessorMLU
desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                        "src_format", infer_server::video::PixelFmt::NV12,
                        "dst_format", infer_server::video::PixelFmt::ARGB,
                        "keep_aspect_ratio", true,
                        "normalize", true);
// create session
auto session = infer_server.CreateSyncSession(desc);
```

### Postproc

模型输出后处理，threshold将过滤掉分数较低的结果目标。

内置了Classification，SSD，YOLOv3三种后处理实现。

使用Postprocessor作为预处理器，重载 ``operator()`` 作为处理函数，并将 ``operator()`` 传给InferServer::Postprocessor。

处理函数每次处理一个数据，输入参数分别为 infer_server::InferData* 输出的单个数据， const infer_server::ModelIO& 存放模型单个输出的CPU内存，以及 const infer_server::ModelInfo* 模型信息。

```C++
struct PostprocClassification {
  float threshold;

  explicit PostprocClassification(float _threshold = 0) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
};  // struct PostprocClassification

struct PostprocSSD {
  float threshold;

  explicit PostprocSSD(float _threshold) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
};  // struct PostprocSSD

struct PostprocYolov3 {
  float threshold;

  explicit PostprocYolov3(float _threshold) : threshold(_threshold) {}

  bool operator()(infer_server::InferData* result, const infer_server::ModelIO& model_output,
                  const infer_server::ModelInfo* model);
  void SetFrameSize(FrameSize size);
  FrameSize GetFrameSize();

};  // struct PostprocYolov3
```

### OSD

将推理结果绘制在图像上

```C++
class CnOsd {
 public:
  // 加载好的label组
  CnOsd(const vector<std::string>& labels);
  CnOsd(const std::string& label_fname);

  // 加载label文件
  void LoadLabels(const std::string& fname);
  inline const std::vector<std::string> labels() const { return labels_; }

  // 标注图像抬头
  void DrawId(Mat image, string text) const;
  // 标注fps
  void DrawFps(Mat image, float fps) const;
  // 标注object
  void DrawLabel(Mat image, const vector<edk::DetectObject>& objects) const;

  // 设置字体
  void set_font(int font);

  inline void set_benchmark_size(cv::Size size) { bm_size_ = size; }
  inline cv::Size benchmark_size() const { return bm_size_; }
  inline void set_benchmark_rate(float rate) { bm_rate_ = rate; }
  inline float benchmark_rate() const { return bm_rate_; }

  // 设置框的粗细
  inline void set_box_thickness(int box_thickness) { box_thickness_ = box_thickness; }
  inline int get_box_thickness() const { return box_thickness_; }
};
```



## Classification

实现分类网络demo只需要实现继承自StreamRunner的ClassificationRunner

```C++
class ClassificationRunner : public StreamRunner {
 public:
  ClassificationRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                       const std::string& model_path, const std::string& func_name, const std::string& label_path,
                       const std::string& data_path, bool show, bool save_video);
  ~ClassificationRunner();

  void Process(edk::CnFrame frame) override;
};
```

在构造函数中初始化资源：

```C++
ClassificationRunner::ClassificationRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                                           const std::string& model_path, const std::string& func_name,
                                           const std::string& label_path, const std::string& data_path,
                                           bool show, bool save_video)
    : StreamRunner(data_path, decode_type, device_id), show_(show), save_video_(save_video) {
  // 初始化InferServer。在同一个进程中同一张卡只有一个InferServer实例 (单例)
  infer_server_.reset(new infer_server::InferServer(device_id));
  // 定义创建Session需要的Session Descriptions
  infer_server::SessionDesc desc;
  // 选择组batch策略，详见InferServer
  desc.strategy = infer_server::BatchStrategy::STATIC;
  // 引擎数目，即实例化多少个模型实例。
  desc.engine_num = 1;
  // 优先级 0-9，数值越大优先级越高
  desc.priority = 0;
  // 显示统计信息
  desc.show_perf = true;
  desc.name = "classification session";

  // 加载离线模型
  desc.model = infer_server::InferServer::LoadModel(model_path, func_name);

  // 设置预处理和后处理
  desc.preproc = infer_server::video::PreprocessorMLU::Create();
  desc.postproc = infer_server::Postprocessor::Create();

#ifdef CNIS_USE_MAGICMIND
  // MLU300系列平台，后端使用MagicMind推理
  desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                          "src_format", infer_server::video::PixelFmt::NV12,
                          "dst_format", infer_server::video::PixelFmt::RGB24,
                          "normalize", false,
                          "mean", std::vector<float>({104, 117, 123}),
                          "std", std::vector<float>({1, 1, 1}));
#else
  // MLU200系列平台，后端使用CNRT推理
  desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                          "src_format", infer_server::video::PixelFmt::NV12,
                          "dst_format", infer_server::video::PixelFmt::BGRA);
#endif
  desc.postproc->SetParams("process_function",
                           infer_server::Postprocessor::ProcessFunction(PostprocClassification(0.2)));

  // 创建推理session
  session_ = infer_server_->CreateSyncSession(desc);

  // 初始化osd，加载labels
  osd_.LoadLabels(label_path);

  // 创建VideoWriter
  if (save_video_) {
    video_writer_.reset(
        new cv::VideoWriter("out.avi", cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 25, g_out_video_size));
    if (!video_writer_->isOpened()) {
      THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "create output video file failed");
    }
  }

  // 通知StreamRunner进入running状态
  Start();
}
```

在Process函数处理解码后的数据：

```C++
void ClassificationRunner::Process(edk::CnFrame frame) {
  // 准备输入数据
  infer_server::video::VideoFrame vframe;
  vframe.plane_num = frame.n_planes;
  vframe.format = infer_server::video::PixelFmt::NV12;
  vframe.width = frame.width;
  vframe.height = frame.height;

  for (size_t plane_idx = 0; plane_idx < vframe.plane_num; ++plane_idx) {
    vframe.stride[plane_idx] = frame.strides[plane_idx];
    uint32_t plane_bytes = vframe.height * vframe.stride[plane_idx];
    if (plane_idx == 1) plane_bytes = std::ceil(1.0 * plane_bytes / 2);
    infer_server::Buffer mlu_buffer(frame.ptrs[plane_idx], plane_bytes, nullptr, GetDeviceId());
    vframe.plane[plane_idx] = mlu_buffer;
  }

  infer_server::PackagePtr in = infer_server::Package::Create(1);
  in->data[0]->Set(std::move(vframe));

  // 推理
  infer_server::PackagePtr out = infer_server::Package::Create(1);
  infer_server::Status status = infer_server::Status::SUCCESS;

  bool ret = infer_server_->RequestSync(session_, std::move(in), &status, out);

  if (!ret || status != infer_server::Status::SUCCESS) {
    decoder_->ReleaseFrame(std::move(frame));
    THROW_EXCEPTION(edk::Exception::INTERNAL,
        "Request sending data to infer server failed. Status: " + std::to_string(static_cast<int>(status)));
  }

  // 将解码镇转换成CV::Mat，并释放解码帧
  cv::Mat img = ConvertToMatAndReleaseBuf(&frame);

  // 获取结果
  const std::vector<DetectObject>& postproc_results = out->data[0]->GetLref<std::vector<DetectObject>>();
  std::vector<edk::DetectObject> detect_result;
  detect_result.reserve(postproc_results.size());
  for (auto &obj : postproc_results) {
    edk::DetectObject detect_obj;
    detect_obj.label = obj.label;
    detect_obj.score = obj.score;
    detect_result.emplace_back(std::move(detect_obj));
  }

  // 打印分类结果
  std::cout << "----- Classification Result:\n";
  int show_number = 2;
  for (auto& obj : detect_result) {
    std::cout << "[Object] label: " << obj.label << " score: " << obj.score << "\n";
    if (!(--show_number)) break;
  }
  std::cout << "-----------------------------------\n" << std::endl;

  // 把结果画在图上
  osd_.DrawLabel(img, detect_result);

  if (show_) {
    // 显示在屏幕上
    auto window_name = "classification";
    cv::imshow(window_name, img);
    cv::waitKey(5);
    // std::string fn = std::to_string(frame.frame_id) + ".jpg";
    // cv::imwrite(fn.c_str(), img);
  }
  if (save_video_) {
    // 存储本地视频文件
    video_writer_->write(img);
  }
}

```

然后在main函数中创建ClassificationRunner，开始运行即可：

```C++
  try {
    // FLAGS_* 是GFlags解析的命令行参数
    g_runner = std::make_shared<ClassificationRunner>(decode_type, FLAGS_dev_id,
                                                      FLAGS_model_path, FLAGS_func_name, FLAGS_label_path,
                                                      FLAGS_data_path, FLAGS_show, FLAGS_save_video);
  } catch (edk::Exception& e) {
    LOG(ERROR) << "Create stream runner failed" << e.what();
    return -1;
  }

  // 启动Runloop
  std::future<bool> process_loop_return = std::async(std::launch::async, &StreamRunner::RunLoop, g_runner.get());

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }
  signal(SIGALRM, HandleSignal);

  // set mlu environment
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  // 开始解析视频源，阻塞直到EOS或重复repeat_time次
  g_runner->DemuxLoop(FLAGS_repeat_time);

  // 等待RunLoop处理完毕退出
  process_loop_return.wait();
  // 释放资源
  g_runner.reset();

  // 出现错误
  if (!process_loop_return.get()) {
    return 1;
  }

  LOGI(SAMPLES) << "run classification SUCCEED!!!" << std::endl;
  edk::log::ShutdownLogging();
```



## Detection

实现检测网络demo只需要实现继承自StreamRunner的DetectionRunner

```C++
class DetectionRunner : public StreamRunner {
 public:
  DetectionRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                  const std::string& model_path, const std::string& func_name, const std::string& label_path,
                  const std::string& track_model_path, const std::string& track_func_name,
                  const std::string& data_path, const std::string& net_type, bool show, bool save_video);
  ~DetectionRunner();

  void Process(edk::CnFrame frame) override;
};
```


在构造函数中初始化资源：

```C++
DetectionRunner::DetectionRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                                 const std::string& model_path, const std::string& func_name,
                                 const std::string& label_path, const std::string& track_model_path,
                                 const std::string& track_func_name, const std::string& data_path,
                                 const std::string& net_type, bool show, bool save_video)
    : StreamRunner(data_path, decode_type, device_id), show_(show), save_video_(save_video), net_type_(net_type) {
  // set mlu environment
  env_.SetDeviceId(0);
  env_.BindDevice();

  // 初始化InferServer。在同一个进程中同一张卡只有一个InferServer实例 (单例)
  infer_server_.reset(new infer_server::InferServer(device_id));
  // 定义创建Session需要的Session Descriptions
  infer_server::SessionDesc desc;
  // 选择组batch策略，详见InferServer
  desc.strategy = infer_server::BatchStrategy::STATIC;
  // 引擎数目，即实例化多少个模型实例。
  desc.engine_num = 1;
  // 优先级 0-9，数值越大优先级越高
  desc.priority = 0;
  // 显示统计信息
  desc.show_perf = true;
  desc.name = "detection session";

  // 加载离线模型
  desc.model = infer_server::InferServer::LoadModel(model_path, func_name);
  // 设置预处理和后处理
  desc.preproc = infer_server::video::PreprocessorMLU::Create();
  desc.postproc = infer_server::Postprocessor::Create();

#ifdef CNIS_USE_MAGICMIND
  // MLU300系列平台，后端使用MagicMind推理
  if (net_type == "YOLOv3") {
    desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                            "src_format", infer_server::video::PixelFmt::NV12,
                            "dst_format", infer_server::video::PixelFmt::RGB24,
                            "normalize", true,
                            "keep_aspect_ratio", true);
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov3MM(0.5)));
  } else {
    THROW_EXCEPTION(edk::Exception::INVALID_ARG, "unsupported magicmind net type: " + net_type);
  }
#else
  // MLU200系列平台，后端使用CNRT推理
  if (net_type == "SSD") {
    // custom preprocessor example
    desc.preproc = infer_server::Preprocessor::Create();
    desc.preproc->SetParams("process_function", infer_server::Preprocessor::ProcessFunction(
        PreprocSSD(desc.model, device_id, edk::PixelFmt::BGRA)));
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocSSD(0.5)));
  } else if (net_type == "YOLOv3") {
    desc.preproc->SetParams("preprocess_type", infer_server::video::PreprocessType::CNCV_PREPROC,
                            "src_format", infer_server::video::PixelFmt::NV12,
                            "dst_format", infer_server::video::PixelFmt::ARGB,
                            "keep_aspect_ratio", true);
    desc.postproc->SetParams("process_function", infer_server::Postprocessor::ProcessFunction(PostprocYolov3(0.5)));
  } else {
    THROW_EXCEPTION(edk::Exception::INVALID_ARG, "unsupported net type: " + net_type);
  }
#endif
  // 创建推理session
  session_ = infer_server_->CreateSyncSession(desc);

  // 创建追踪器
  tracker_.reset(new edk::FeatureMatchTrack);
  // 创建特征提取器
  feature_extractor_.reset(new FeatureExtractor);
  if (track_model_path != "" && track_model_path != "cpu") {
    feature_extractor_->Init(track_model_path.c_str(), track_func_name.c_str(), device_id);
  }

  // 初始化osd，加载labels
  osd_.LoadLabels(label_path);

  // 初始化VideoWriter
  if (save_video_) {
    video_writer_.reset(
        new cv::VideoWriter("out.avi", cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 25, g_out_video_size));
    if (!video_writer_->isOpened()) {
      THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "create output video file failed");
    }
  }

  // 通知StreamRunner进入running状态
  Start();
}
```

在Process函数处理解码后的数据：

```C++
void DetectionRunner::Process(edk::CnFrame frame) {
  // 准备输入数据
  infer_server::PackagePtr in = infer_server::Package::Create(1);
  if (net_type_ == "YOLOv3") {
    // Yolov3使用PreprocessorMLU作为预处理器，输入数据必须为VideoFrame类型。
    infer_server::video::VideoFrame vframe;
    vframe.plane_num = frame.n_planes;
    vframe.format = infer_server::video::PixelFmt::NV12;
    vframe.width = frame.width;
    vframe.height = frame.height;

    for (size_t plane_idx = 0; plane_idx < vframe.plane_num; ++plane_idx) {
      vframe.stride[plane_idx] = frame.strides[plane_idx];
      uint32_t plane_bytes = vframe.height * vframe.stride[plane_idx];
      if (plane_idx == 1) plane_bytes = std::ceil(1.0 * plane_bytes / 2);
      infer_server::Buffer mlu_buffer(frame.ptrs[plane_idx], plane_bytes, nullptr, GetDeviceId());
      vframe.plane[plane_idx] = mlu_buffer;
    }
    in->data[0]->Set(std::move(vframe));
    // 由于yolov3预处理中对原图做保持宽高比的resize。将帧的宽高信息作为user data传给后处理。以便计算bbox坐标。
    in->data[0]->SetUserData(FrameSize{static_cast<int>(vframe.width), static_cast<int>(vframe.height)});
  } else if (net_type_ == "SSD") {
    // 自定义预处理，可以使用任意类型作为输入数据。预处理函数将对应的类型取出并进行预处理。
    in->data[0]->Set(frame);
  } else {
    THROW_EXCEPTION(edk::Exception::INVALID_ARG, "unsupported net type: " + net_type_);
  }

  // 推理
  infer_server::PackagePtr out = infer_server::Package::Create(1);
  infer_server::Status status = infer_server::Status::SUCCESS;
  bool ret = infer_server_->RequestSync(session_, std::move(in), &status, out);
  if (!ret || status != infer_server::Status::SUCCESS) {
    decoder_->ReleaseFrame(std::move(frame));
    THROW_EXCEPTION(edk::Exception::INTERNAL,
        "Request sending data to infer server failed. Status: " + std::to_string(static_cast<int>(status)));
  }

  // 获取输出结果
  const std::vector<DetectObject>& postproc_results = out->data[0]->GetLref<std::vector<DetectObject>>();
  std::vector<edk::DetectObject> detect_objs;
  detect_objs.reserve(postproc_results.size());
  for (auto &obj : postproc_results) {
    edk::DetectObject detect_obj;
    detect_obj.label = obj.label;
    detect_obj.score = obj.score;
    detect_obj.bbox.x = obj.bbox.x;
    detect_obj.bbox.y = obj.bbox.y;
    detect_obj.bbox.width = obj.bbox.w;
    detect_obj.bbox.height = obj.bbox.h;
    detect_objs.emplace_back(std::move(detect_obj));
  }

  cv::Mat img;
  if (feature_extractor_->OnMlu()) {
    // 使用模型在MLU上做特征提取
    if (!feature_extractor_->ExtractFeatureOnMlu(frame, &detect_objs)) {
      THROW_EXCEPTION(edk::Exception::INTERNAL, "Extract feature on Mlu failed");
    }
    // 解码帧转换成CV::Mat，然后释放解码帧
    img = ConvertToMatAndReleaseBuf(&frame);
  } else {
    // 解码帧转换成CV::Mat，然后释放解码帧
    img = ConvertToMatAndReleaseBuf(&frame);
    // 使用Opencv在CPU上做特征提取
    if (!feature_extractor_->ExtractFeatureOnCpu(img, &detect_objs)) {
      THROW_EXCEPTION(edk::Exception::INTERNAL, "Extract feature on Cpu failed");
    }
  }

  // 追踪
  std::vector<edk::DetectObject> track_result;
  track_result.clear();
  tracker_->UpdateFrame(edk::TrackFrame(), detect_objs, &track_result);

  // 打印推理和跟踪结果
  std::cout << "----- Object detected in one frame:\n";
  for (auto& obj : track_result) {
    std::cout << obj << "\n";
  }
  std::cout << "-----------------------------------\n" << std::endl;

  osd_.DrawLabel(img, track_result);

  // 显示在屏幕上
  if (show_) {
    auto window_name = "stream app";
    cv::imshow(window_name, img);
    cv::waitKey(5);
    // std::string fn = std::to_string(frame.frame_id) + ".jpg";
    // cv::imwrite(fn.c_str(), img);
  }
  // 存储本地视频文件
  if (save_video_) {
    video_writer_->write(img);
  }
}
```



然后在main函数中创建DetectionRunner，开始运行即可：

```C++
  try {
    // FLAGS_* 是GFlags解析的命令行参数
    g_runner = std::make_shared<DetectionRunner>(decode_type, FLAGS_dev_id,
                                                 FLAGS_model_path, FLAGS_func_name, FLAGS_label_path,
                                                 FLAGS_track_model_path, FLAGS_track_func_name,
                                                 FLAGS_data_path, FLAGS_net_type,
                                                 FLAGS_show, FLAGS_save_video);
  } catch (edk::Exception& e) {
    LOG(ERROR) << "Create stream runner failed" << e.what();
    return -1;
  }

  // 启动Runloop
  std::future<bool> process_loop_return = std::async(std::launch::async, &StreamRunner::RunLoop, g_runner.get());

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }
  signal(SIGALRM, HandleSignal);

  // set mlu environment
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  // 开始解析视频源，阻塞直到EOS或重复repeat_time次
  g_runner->DemuxLoop(FLAGS_repeat_time);

  // 等待RunLoop处理完毕退出
  process_loop_return.wait();
  // 释放资源
  g_runner.reset();

  // 出现错误
  if (!process_loop_return.get()) {
    return 1;
  }
  LOGI(SAMPLES) << "run stream app SUCCEED!!!" << std::endl;
  edk::log::ShutdownLogging();
```

## Transcode

实现转码demo只需要实现继承自StreamRunner的TranscodeRunner

```C++
class TranscodeRunner : public StreamRunner {
 public:
  TranscodeRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                  const std::string& data_path, const std::string& output_file_name,
                  int dst_width, int dst_height, double dst_frame_rate);
  ~TranscodeRunner();

  void Process(edk::CnFrame frame) override;
};
```

在构造函数中初始化资源：

```C++
TranscodeRunner::TranscodeRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                                 const std::string& data_path, const std::string& output_file_name,
                                 int dst_width, int dst_height, double dst_frame_rate)
    : StreamRunner(data_path, decode_type, device_id), dst_frame_rate_(dst_frame_rate),
    dst_width_(dst_width), dst_height_(dst_height), output_file_name_(output_file_name) {
  // 定义创建编码器需要的一些参数
  edk::EasyEncode::Attr attr;
  // 设置宽，高，pixel format
  attr.frame_geometry.w = dst_width;
  attr.frame_geometry.h = dst_height;
  attr.pixel_format = edk::PixelFmt::NV12;

  // 根据输出文件名，判断codec类型
  edk::CodecType codec_type = edk::CodecType::H264;
  std::string file_name = output_file_name;
  auto dot = file_name.find_last_of(".");
  if (dot == std::string::npos) {
    THROW_EXCEPTION(edk::Exception::INVALID_ARG, "unknown file type: " + file_name);
  }
  std::transform(file_name.begin(), file_name.end(), file_name.begin(), ::tolower);
  if (file_name.find("hevc") != std::string::npos || file_name.find("h265") != std::string::npos) {
    codec_type = edk::CodecType::H265;
  }
  file_extension_ = file_name.substr(dot + 1);
  file_name_ = file_name.substr(0, dot);
  if (file_extension_ == "jpg" || file_extension_ == "jpeg") {
    codec_type = edk::CodecType::JPEG;
    jpeg_encode_ = true;
  }
  attr.codec_type = codec_type;
  edk::MluContext ctx;
  edk::CoreVersion core_ver = ctx.GetCoreVersion();
  if (core_ver == edk::CoreVersion::MLU220 || core_ver == edk::CoreVersion::MLU270) {
    // Mlu200系列平台上，设置frame rate参数
    attr.attr_mlu200.rate_control.frame_rate_den = 10;
    attr.attr_mlu200.rate_control.frame_rate_num =
        std::ceil(static_cast<int>(dst_frame_rate * attr.attr_mlu200.rate_control.frame_rate_den));
  } else if (core_ver == edk::CoreVersion::MLU370) {
    // Mlu300系列平台上，设置frame rate参数
    attr.attr_mlu300.frame_rate_den = 10;
    attr.attr_mlu300.frame_rate_num =
        std::ceil(static_cast<int>(dst_frame_rate * attr.attr_mlu300.frame_rate_den));
  } else {
    THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Not supported core version");
  }

  // 设置 eos callback 函数
  attr.eos_callback = std::bind(&TranscodeRunner::EosCallback, this);
  // 设置 packet callback 函数
  attr.packet_callback = std::bind(&TranscodeRunner::PacketCallback, this, std::placeholders::_1);

  // 创建编码器
  encode_ = edk::EasyEncode::New(attr);

#ifdef HAVE_CNCV
  // 创建cncv resize yuv做预处理
  resize_.reset(new CncvResizeYuv(device_id));
  // 初始化预处理
  if (!resize_->Init()) {
    THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Create CNCV resize yuv failed");
  }
#else
  THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Create resize yuv failed, please install CNCV");
#endif

  // 通知StreamRunner进入running状态
  Start();
}

void TranscodeRunner::PacketCallback(const edk::CnPacket &packet) {
  if (packet.length == 0 || packet.data == 0) {
    LOGW(SAMPLE) << "[TranscodeRunner] PacketCallback received empty packet.";
    return;
  }
  if (packet.codec_type == edk::CodecType::JPEG) {
    output_file_name_ = file_name_ + std::to_string(frame_count_) + "." + file_extension_;
    file_.open(output_file_name_.c_str());
  } else if (!file_.is_open()) {
    file_.open(output_file_name_.c_str());
  }
  if (!file_.is_open()) {
    LOGE(SAMPLE) << "[TranscodeRunner] PacketCallback open output file failed";
  } else {
    file_.write(reinterpret_cast<const char *>(packet.data), packet.length);
    if (packet.codec_type == edk::CodecType::JPEG) {
      file_.close();
    }
  }
  frame_count_++;
  std::cout << "encode frame count: " << frame_count_<< ", pts: " << packet.pts << std::endl;
  encode_->ReleaseBuffer(packet.buf_id);
}

void TranscodeRunner::EosCallback() {
  LOGI(SAMPLE) << "[TranscodeRunner] EosCallback ... ";
  std::lock_guard<std::mutex>lg(encode_eos_mut_);
  encode_received_eos_ = true;
  encode_eos_cond_.notify_one();
}
```

在Process函数处理解码后的数据：

```C++
void TranscodeRunner::Process(edk::CnFrame frame) {
  // 向编码器请求一帧frame，将编码器的输入buffer的mlu内存地址放入dst_frame的ptrs中。
  edk::CnFrame dst_frame;
  if (!encode_->RequestFrame(&dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Request frame from encoder failed");
  }
#ifdef HAVE_CNCV
  // 预处理， yuv to yuv resize
  if (!resize_->Process(frame, &dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Resize yuv failed");
  }
#endif
  // 释放解码帧
  decoder_->ReleaseFrame(std::move(frame));
  // 赋值presentation timestamp （编码器透传pts，编码后的数据可以获得该pts）
  dst_frame.pts = frame.pts;
  // 喂数据给编码器
  if (!encode_->FeedData(dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Feed data to encoder failed");
  }
}
```

然后在main函数中创建TranscodeRunner，开始运行即可：

```C++
  try {
    // FLAGS_* 是GFlags解析的命令行参数
    g_runner = std::make_shared<TranscodeRunner>(decode_type, FLAGS_dev_id, FLAGS_data_path, FLAGS_output_file_name,
                                                 FLAGS_dst_width, FLAGS_dst_height, FLAGS_dst_frame_rate);
  } catch (edk::Exception& e) {
    LOG(ERROR) << "Create stream runner failed" << e.what();
    return -1;
  }

  // 启动Runloop
  std::future<bool> process_loop_return = std::async(std::launch::async, &StreamRunner::RunLoop, g_runner.get());

  if (0 < FLAGS_wait_time) {
    alarm(FLAGS_wait_time);
  }
  signal(SIGALRM, HandleSignal);

  // set mlu environment
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  // 开始解析视频源，阻塞直到EOS或重复repeat_time次
  g_runner->DemuxLoop(FLAGS_repeat_time);

  // 等待RunLoop处理完毕退出
  process_loop_return.wait();
  // 释放资源
  g_runner.reset();

  // 出现错误
  if (!process_loop_return.get()) {
    return 1;
  }
  LOGI(SAMPLES) << "run transcode SUCCEED!!!" << std::endl;
  edk::log::ShutdownLogging();
```

## 移植新模型

如上所示，移植其他模型仅需实现对应的后处理，若前处理有特殊需求，再实现一个前处理即可。

然后根据所需功能，在Process函数中对解码后的帧数据做相应处理。

