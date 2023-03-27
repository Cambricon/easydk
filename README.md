EN|[CN](README_cn.md)

# Cambricon<sup>®</sup> Easy Development Kit

Cambricon<sup>®</sup> Easy Development Kit is a toolkit, which aims at helping with developing software on Cambricon MLU270/MLU220/MLU370 platform.

EasyDK provides following modules:
- Device: MLU device context operation
- EasyCodec: easy decode and encode on MLU
- EasyInfer: easy inference accelerator on MLU
- EasyBang: easy Bang operator
- EasyTrack: easy track

![modules](docs/images/software_stack.png)

Besides, toolkit contains InferServer component, which aims at simplify developing and deploying High-performance AI applications on Cambricon MLU270/MLU220/MLU370 platform. InferServer provides APIs for inference and it provides functions like model loading and management, inference task scheduling and so on.

![infer_server](docs/images/infer_server_software_stack.png)

InferServer contains the following user APIs:
- Model: model loading and management
- Processor: backend processing unit, supports customization
- InferServer: executes inference tasks

## Getting started ##

  To start using EasyDK, please refer to the chapter of ***quick start*** in the document of [Cambricon-EasyDK-User-Guide-CN.pdf](./docs/release_document/latest/Cambricon-EasyDK-User-Guide-CN-vlatest.pdf) .

## Documentation ##

[Cambricon Forum Docs](https://www.cambricon.com/docs/easydk/user_guide_html/index.html)

For more details, please refer to the documentation on the webpage, including how to use EasyDK, introduction to EasyDK modules and sample codes.

Besides, please refer to [release documents](docs/release_document) .
