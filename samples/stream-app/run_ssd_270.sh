#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

if [ ! -f "cache/resnet34_ssd.cambricon" ]; then
  wget -O cache/resnet34_ssd.cambricon http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon
fi
if [ ! -f "cache/label_voc.txt" ]; then
  wget -O cache/label_voc.txt http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/label_voc.txt
fi
model_file="${CURRENT_DIR}/cache/resnet34_ssd.cambricon"
label_path="${CURRENT_DIR}/cache/label_voc.txt"
data_path="${CURRENT_DIR}/../data/videos/cars.mp4"
track_model_path="cpu"

../bin/stream-app  \
     --model_path $model_file \
     --track_model_path $track_model_path \
     --label_path $label_path \
     --data_path $data_path \
     --net_type "SSD" \
     --show=false \
     --save_video=true \
     --wait_time 0 \
     --repeat_time 0
popd
