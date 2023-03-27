#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p cache

if [ ! -f "cache/yolov3_nhwc.model" ]; then
  wget -O cache/yolov3_nhwc.model http://video.cambricon.com/models/magicmind/v1.1.0/yolov3_v1.1.0_4b_rgb_uint8.magicmind
fi
if [ ! -f "cache/label_map_coco.txt" ]; then
  wget -O cache/label_map_coco.txt http://video.cambricon.com/models/MLU270/yolov3/label_map_coco.txt
fi
if [ ! -f "cache/feature_extract_nhwc.model" ]; then
  wget -O cache/feature_extract_nhwc.model http://video.cambricon.com/models/magicmind/v1.1.0//feature_extract_v1.1.0_4b_rgb_fp32.magicmind
fi
model_file="${CURRENT_DIR}/cache/yolov3_nhwc.model"
label_path="${CURRENT_DIR}/cache/label_map_coco.txt"
data_path="${CURRENT_DIR}/../data/videos/cars.mp4"
track_model_path="${CURRENT_DIR}/cache/feature_extract_nhwc.model"

../bin/stream-app  \
     --model_path $model_file \
     --label_path $label_path \
     --track_model_path $track_model_path \
     --data_path $data_path \
     --net_type "YOLOv3" \
     --show=false \
     --save_video=true \
     --wait_time 0 \
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0 \
     --colorlogtostderr \
     --alsologtostderr
popd
