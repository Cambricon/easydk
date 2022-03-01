EN|[CN](README_cn.md)

# Cambricon<sup>®</sup> Easy Development Kit

Cambricon<sup>®</sup> Easy Development Kit is a toolkit, which aims at helping with developing software on Cambricon MLU270/MLU220/MLU370 platform.

Toolkit provides following modules:
- Device: MLU device context operation
- EasyCodec: easy decode and encode on MLU
- EasyInfer: easy inference accelerator on MLU
- EasyTrack: easy track, including feature match track and kcf track
- EasyBang: easy Bang operator

![modules](docs/images/software_stack.png)

Besides, toolkit contains InferServer component, which aims at simplify developing and deploying High-performance deep learning applications on Cambricon MLU270/MLU220/MLU370 platform. InferServer provides APIs for inference and it provides functions like model loading and management, inference task scheduling and so on.

![infer_server](docs/images/infer_server_software_stack.png)

InferServer contains the following user APIs:
- Model: model loading and management
- Processor: backend processing unit, supports customization
- InferServer: executes inference tasks

## **Cambricon Dependencies** ##

You can find the cambricon dependencies, including headers and libraries, in the neuware home (installed in `/usr/local/neuware` by default).

### Quick Start ###

This section introduces how to quickly build instructions on EasyDK and how to develop your own applications based on EasyDK.

#### **Required environments** ####

Before building instructions, you need to install the following software:

- cmake  2.8.7+
- GCC    4.8.5+
- GLog   0.3.4
- Cambricon Neuware Toolkit >= 1.5.0
- CNCV >= 0.4.0 (optional)
- libcurl-dev (optional)

Using the Magicmind backend has the following additional dependencies:

- Cambricon Neuware Toolkit >= 2.4.2
- Magicmind >= 0.5.0

(Magicmind runtime library introduces dependencies on CNNL, CNNL_extra, CNLight. For version requirements, please see Magicmind User Manual)

For decoding or Encoding on MLU370 platform, there are some additional dependencies:

- Cambricon Neuware Toolkit >= 2.6.2
- CNCodec_v3 >= 0.8.2

samples & tests dependencies:

- OpenCV 2.4.9+
- GFlags 2.1.2
- FFmpeg 2.8 3.4 4.2

#### Ubuntu or Debian ####

If you are using Ubuntu or Debian, run the following commands:

   ```bash
   sudo apt install libgoogle-glog-dev cmake
   # optional dependencies
   sudo apt install curl libcurl4-openssl-dev
   # samples dependencies
   sudo apt install libgflags-dev libopencv-dev
   ```

#### Centos ####

If you are using CentOS, run the following commands:

   ```bash
   sudo yum install glog.x86_64 cmake3.x86_64
   # optional dependencies
   sudo yum install curl libcurl-devel
   # samples dependencies
   sudo yum install gflags.x86_64 opencv-devel.x86_64 ffmpeg ffmpeg-devel
   ```

## Build Instructions Using CMake ##

After finished prerequisite, you can build instructions with the following steps:

1. Run the following command to create a directory for saving the output.

   ```bash
   mkdir build       # Create a directory to save the output.
   ```

   A Makefile will be generated in the build folder.

2. Run the following command to generate a script for building instructions.

   ```bash
   cd build
   cmake ${EASYDK_DIR}  # Generate native build scripts.
   ```

   Cambricon easydk provides a CMake script ([CMakeLists.txt](CMakeLists.txt)) to build instructions. You can download CMake for free from <http://www.cmake.org/>.

   `${EASYDK_DIR}` specifies the directory where easydk saves for.

   | cmake option         | range           | default | description                                 |
   | -------------------- | --------------- | ------- | ------------------------------------------- |
   | BUILD_SAMPLES        | ON / OFF        | OFF     | build with samples                          |
   | BUILD_TESTS          | ON / OFF        | OFF     | build with tests                            |
   | WITH_CODEC           | ON / OFF        | ON      | build codec                                 |
   | WITH_INFER           | ON / OFF        | ON      | build infer                                 |
   | WITH_TRACKER         | ON / OFF        | ON      | build tracker                               |
   | WITH_BANG            | ON / OFF        | ON      | build bang                                  |
   | WITH_BACKWARD        | ON / OFF        | ON      | build backward                              |
   | WITH_TURBOJPEG       | ON / OFF        | OFF     | build with turbo-jpeg                       |
   | ENABLE_KCF           | ON / OFF        | OFF     | build with KCF track                        |
   | WITH_INFER_SERVER    | ON / OFF        | ON      | build infer-server                          |
   | CNIS_WITH_CONTRIB    | ON / OFF        | ON      | build infer-server contrib                  |
   | CNIS_RECORD_PERF     | ON / OFF        | ON      | enable recording infer-server perf info     |
   | CNIS_WITH_CURL       | ON / OFF        | ON      | enable curl, to download model from web     |
   | CNIS_USE_MAGICMIND   | ON / OFF        | OFF     | enable infer-server with magicmind backend  |
   | CNIS_WITH_PYTHON_API | ON / OFF        | OFF     | enable infer-server python api              |
   | SANITIZE_MEMORY      | ON / OFF        | OFF     | check memory                                |
   | SANITIZE_ADDRESS     | ON / OFF        | OFF     | check address                               |
   | SANITIZE_THREAD      | ON / OFF        | OFF     | check thread                                |
   | SANITIZE_UNDEFINED   | ON / OFF        | OFF     | check undefined behavior                    |

   Example:

   ```bash
   cd build
   # build without samples and tests
   cmake ${EASYDK_DIR}      \
        -DBUILD_SAMPLES=ON  \
        -DBUILD_TESTS=ON
   ```

3. Run the following command to build instructions:

   ```bash
   make
   ```

4. After compilation, the library files are stored in  `${EASYDK_DIR}/lib` ; the header filers are stored in `${EASYDK_DIR}/include`; the header files of InferServer are stores in `${EASYDK_DIR}/infer_server/include` .

5. To cross-compile EasyDK, please cross-compile and install third-party dependencies in advance and config `CMAKE_TOOLCHAIN_FILE` file. For example, on MLU220-SOM platform:

    ```bash
    export NEUWARE_HOME=/your/path/to/neuware
    export PATH=$PATH:/your/path/to/cross-compiler/bin
    cmake ${EASYDK_DIR} -DCMAKE_FIND_ROOT_PATH=/your/path/to/3rdparty-libraries-install-path -DCMAKE_TOOLCHAIN_FILE=${EASYDK_DIR}/cmake/cross-compile.cmake  -DCNIS_WITH_CURL=OFF
    ```

   A compilation package is provided at http://video.cambricon.com/models/edge.tar.gz. After decompressing the package, follows the steps in README to compile.

## Documentation ##

[Cambricon Forum Docs](https://www.cambricon.com/docs/easydk/user_guide_html/index.html)

For more details, please refer to the documentation on the webpage, including how to use EasyDK, introduction to EasyDK modules and sample codes.

Besides, please refer to the EasyDK-based application porting tutorial `${EASYDK_DIR}/docs/ApplicationPortingTutorialBasedOnEasyDK.md` .