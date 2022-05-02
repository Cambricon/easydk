#!/bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )
EASYDK_DIR=$CURRENT_DIR/..
MLU_TYPE="mlu270"

PrintUsages() {
  echo "Usages: coverage.sh [mlu270/mlu370]"
}

if [ $# -ge 1 ]; then
  if [[ $# > 1 || ${1} != "mlu270" && ${1} != "mlu370" ]]; then
    PrintUsages
    exit 1
  else
    MLU_TYPE=${1}
  fi
else
  echo -e "\033[33mWARNING: Run this sript with no parameter is deprecated and will be removed in a future release\033[0m"
  PrintUsages
fi

if [ $COVERAGE_TRACE_FILE_NAME ]; then
  trace_file=$COVERAGE_TRACE_FILE_NAME
else
  trace_file="${PWD}/case.coverage"
fi

if [ $COVERAGE_REPORT_DIR ]; then
  dir=$COVERAGE_REPORT_DIR
else
  dir="${PWD}/coverage.html"
fi
echo $trace_file
echo $dir

pushd $EASYDK_DIR
  if [ -d $EASYDK_DIR/build/ ];  then
    pushd $EASYDK_DIR/build/
    make clean
    rm -rf *
  else
    mkdir -p $EASYDK_DIR/build
    pushd $EASYDK_DIR/build/
  fi

  if [[ "${1}" == "mlu370" ]];then
    cmake -DBUILD_TESTS=ON -DCODE_COVERAGE_TEST=ON -DCNIS_USE_MAGICMIND=ON -DCNIS_WITH_PYTHON_API=ON $EASYDK_DIR
  else
    cmake -DBUILD_TESTS=ON -DCODE_COVERAGE_TEST=ON -DCNIS_WITH_PYTHON_API=ON $EASYDK_DIR
  fi
  make -j8
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64/

  if [ $? -ne 0 ]; then
    exit 1
  fi

  pushd $EASYDK_DIR/build
    if [[ "${1}" == "mlu370" ]];then
      ./tests/tests_edk  --gtest_filter=-EasyInfer.*
    else
      ./tests/tests_edk
    fi
    if [ $? -ne 0 ]; then
      exit 1
    fi

    ./infer_server/tests/apitest_cnis
    if [ $? -ne 0 ]; then
      exit 1
    fi

    # For mlu370 centos platform,
    # ./infer_server/tests/unittest_cnis --gtest_filter=-InferServerCore.Executor:InferServerCore.SessionInit:InferServerCore.SessionSend:InferServerCore.SessionCheckAndResponse:InferServerCore.SessionDiscardTask:InferServerCoreDeathTest.InitExecutorFail:InferServerTest.Model
    ./infer_server/tests/unittest_cnis
    if [ $? -ne 0 ]; then
      exit 1
    fi

    pip3 install -r ../infer_server/python/requirements.txt
    ./../infer_server/python/test/run_pytest.sh
    if [ $? -ne 0 ]; then
      exit 1
    fi
  popd

  lcov --rc lcov_branch_coverage=1 -c -d . -o clog
  lcov --rc lcov_branch_coverage=1 -r clog -o easydk_all.coverage
  rm clog
  if [[ "${1}" == "mlu370" ]];then
    lcov --rc lcov_branch_coverage=1 -e easydk_all.coverage '*/src/cxxutil/*.cpp' '*/src/device/*.cpp' '*/src/easybang/resize/*.cpp' '*/src/easybang/resize_and_convert/*.cpp' '*/src/easycodec/*.cpp' '*/src/easytrack/*.cpp' '*/src/easyplugin/resize_yuv_to_rgba/*' '*/src/easyplugin/resize_yuv_to_yuv/*' '*/infer_server/src/core/*.cpp' '*/infer_server/src/model/*.cpp' '*/infer_server/src/preprocessor/*.cpp' '*/infer_server/python/src/*.cpp' -o $trace_file
  else
    lcov --rc lcov_branch_coverage=1 -e easydk_all.coverage '*/src/cxxutil/*.cpp' '*/src/device/*.cpp' '*/src/easybang/resize/*.cpp' '*/src/easybang/resize_and_convert/*.cpp' '*/src/easycodec/*.cpp' '*/src/easyinfer/*.cpp' '*/src/easytrack/*.cpp' '*/src/easyplugin/resize_yuv_to_rgba/*' '*/src/easyplugin/resize_yuv_to_yuv/*' '*/infer_server/src/core/*.cpp' '*/infer_server/src/model/*.cpp' '*/infer_server/src/preprocessor/*.cpp' '*/infer_server/python/src/*.cpp' -o $trace_file
  fi
  rm easydk_all.coverage
popd
genhtml --rc lcov_branch_coverage=1 $trace_file -o $dir


