#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

# download 370 model and labels
if [ ! -f "cache/resnet50_nhwc.model" ]; then
  wget -O cache/resnet50_nhwc.model http://video.cambricon.com/models/MLU370/resnet50_nhwc_tfu_0.8.2_uint8_int8_fp16.model
fi
if [ ! -f "cache/synset_words.txt" ]; then
  wget -O cache/synset_words.txt http://video.cambricon.com/models/MLU270/Classification/resnet50/synset_words.txt
fi
model_file="${CURRENT_DIR}/cache/resnet50_nhwc.model"
label_path="${CURRENT_DIR}/cache/synset_words.txt"
data_path="${CURRENT_DIR}/../data/videos/cars.mp4"

../bin/classification  \
     --model_path $model_file \
     --label_path $label_path \
     --data_path $data_path \
     --show=false \
     --save_video=true \
     --wait_time 0 \
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0
popd
