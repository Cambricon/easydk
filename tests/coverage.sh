#!/bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )
EASYDK_DIR=$CURRENT_DIR/..
  if [ $COVERAGE_TRACE_FILE_NAME ]; then
    trace_file=$COVERAGE_TRACE_FILE_NAME
  else
    trace_file="case.coverage"
  fi

  if [ $COVERAGE_REPORT_DIR ]; then
    dir=$COVERAGE_REPORT_DIR
  else
    dir="coverage.html"
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

  cmake -DBUILD_TESTS=ON -DCODE_COVERAGE_TEST=ON -DENABLE_KCF=OFF $EASYDK_DIR
  make -j8
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64/
  ./tests/tests_edk
  popd

  lcov --rc lcov_branch_coverage=1 -c -d . -o clog
  lcov --rc lcov_branch_coverage=1 -r clog -o easydk_all.coverage
  rm clog
  lcov --rc lcov_branch_coverage=1 -e easydk_all.coverage '*/src/cxxutil/*.cpp' '*/src/device/*.cpp' '*/src/easybang/resize/*.cpp' '*/src/easybang/resize_and_convert/*.cpp' '*/src/easycodec/*.cpp' '*/src/easyinfer/*.cpp' '*/src/easytrack/*.cpp' '*/src/easyplugin/resize_yuv_to_rgba/*' '*/src/easyplugin/resize_yuv_to_yuv/*' -o $trace_file
  rm easydk_all.coverage
  genhtml --rc lcov_branch_coverage=1 $trace_file -o $dir


