#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

if [ ! -f "data/resnet18_220.cambricon" ]; then
  wget -O data/resnet18_220.cambricon http://video.cambricon.com/models/MLU220/classification/resnet18/resnet18_bs4_c4.cambricon
fi
if [ ! -f "data/resnet50_270.cambricon" ]; then
  wget -O data/resnet50_270.cambricon http://video.cambricon.com/models/MLU270/Classification/resnet50/resnet50_offline.cambricon
fi

if [ $NEUWARE_HOME ] ;then
  echo NEUWARE_HOME: $NEUWARE_HOME
else
  export NEUWARE_HOME=/usr/local/neuware
  echo NEUWARE_HOME: $NEUWARE_HOME
fi

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$NEUWARE_HOME/lib64

./bin/tests_edk

popd
