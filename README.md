EN|[CN](README_cn.md)

# Cambricon<sup>®</sup> Easy Development Kit

Cambricon<sup>®</sup> Easy Development Kit is a toolkit, which aims at helping with developing software on Cambricon hardware platforms.

EasyDK provides following features:
  - BufSurface: Buffer describing and management.
  - Platform: Initialize and uninitialize platform. Get platform information.
  - Decode: Decode videos and images on hardware and resize after decoding.
  - Encode: Encode videos and images on hardware and resize before encoding.
  - Transform: Transform images.
  - Osd: Draw boxed and bitmaps.
  - Vin: Capture camera input.
  - Vout: render images.

Besides, EasyDK contains InferServer component, which aims at simplify developing and deploying High-performance AI applications on Cambricon hardware platforms. InferServer provides APIs for inference and it provides functions like model loading and management, inference task scheduling and so on.

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
