#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

# download 220 model and labels
if [ ! -f "cache/resnet18_b4c4_bgra_mlu220.cambricon" ]; then
  wget -O cache/resnet18_b4c4_bgra_mlu220.cambricon http://video.cambricon.com/models/MLU220/resnet18_b4c4_bgra_mlu220.cambricon
fi
if [ ! -f "cache/synset_words.txt" ]; then
  wget -O cache/synset_words.txt http://video.cambricon.com/models/MLU220/classification/resnet18/synset_words.txt
fi
model_file="${CURRENT_DIR}/cache/resnet18_b4c4_bgra_mlu220.cambricon"
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
