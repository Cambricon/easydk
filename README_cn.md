[EN](README.md)|CN

# Cambricon<sup>®</sup> Easy Development Kit

EasyDK(Cambricon<sup>®</sup> Easy Development Kit)提供了一套面向寒武纪硬件设备的接口，用于快速开发和部署人工智能应用。

EasyDK支持如下特性:

  - BufSurface：描述及管理buffer。
  - Platform：初始化和去初始化平台，获取平台相关信息。
  - Decode：视频与图片的硬件解码及解码后缩放。
  - Encode：视频与图片的硬件编码及编码前缩放。
  - Transform：转换图片。
  - OSD：绘制框及位图。
  - Vin：捕捉摄像头输入。
  - Vout：渲染图片。


EasyDK还包含推理服务组件：提供了一套面向寒武纪硬件设备的类似服务器的推理接口（C++11标准），以及模型加载与管理，推理任务调度等功能，极大地简化了面向寒武纪硬件平台高性能人工智能应用的开发和部署工作。


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
