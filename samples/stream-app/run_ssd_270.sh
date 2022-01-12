#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

if [ ! -f "cache/vgg16_ssd_b4c4_bgra_mlu270.cambricon" ]; then
  wget -O cache/vgg16_ssd_b4c4_bgra_mlu270.cambricon http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/vgg16_ssd_b4c4_bgra_mlu270.cambricon
fi
if [ ! -f "cache/label_voc.txt" ]; then
  wget -O cache/label_voc.txt http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/label_voc.txt
fi
model_file="${CURRENT_DIR}/cache/vgg16_ssd_b4c4_bgra_mlu270.cambricon"
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
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0
popd
