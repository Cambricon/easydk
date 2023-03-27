[EN](README.md)|CN

# Cambricon<sup>®</sup> Easy Development Kit

EasyDK(Cambricon<sup>®</sup> Neuware Easy Development Kit)提供了一套面向 
MLU(Machine Learning Unit,寒武纪机器学习单元)设备的高级别的接口（C++11标准），
用于面向MLU平台（MLU270，MLU220，MLU370）快速开发和部署人工智能应用。

EasyDK共包含如下6个模块:

  - Device: 提供MLU设备上下文及内存等相关操作
  - EasyCodec: 提供支持视频与图片的MLU硬件编解码功能
  - EasyInfer: 提供离线模型推理相关功能
  - EasyBang: 提供简易调用Bang算子的接口，目前支持的算子有ResizeConvertCrop和ResizeYuv
  - EasyTrack: 提供目标追踪的功能
  - cxxutil: 其他模块用到的部分cpp实现

![modules](docs/images/software_stack.png)

EasyDK还包含推理服务组件：提供了一套面向MLU（Machine Learning Unit，寒武纪机器学习单元）类似服务器的推理接口（C++11标准），以及模型加载与管理，推理任务调度等功能，极大地简化了面向MLU平台高性能人工智能应用的开发和部署工作。

![infer_server](docs/images/infer_server_software_stack.png)

推理服务共包含以下3个模块的用户接口:

- Model: 模型加载与管理
- Processor: 可自定义的后端处理单元
- InferServer: 执行推理任务

## 快速入门 ##

  如何从零开始构建EasyDK，并运行示例代码完成简单的人工智能任务。请参考文档[Cambricon-EasyDK-User-Guide-CN.pdf](./docs/release_document/latest/Cambricon-EasyDK-User-Guide-CN-vlatest.pdf)中的 ***快速入门*** 章节。

## 文档 ##

[Cambricon Forum Docs](https://www.cambricon.com/docs/easydk/user_guide_html/index.html)

更多内容请参看文档，包括如何使用EasyDK，EasyDK模块的详细介绍以及一些示例代码等等。

另外也可参看 [release documents](docs/release_document) 。
