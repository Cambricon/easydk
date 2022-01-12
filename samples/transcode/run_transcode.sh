#! /bin/bash
CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR

mkdir -p output

data_path="${CURRENT_DIR}/../data/videos/cars.mp4"

../bin/transcode  \
     --data_path $data_path \
     --output_file_name="./output/out.h264" \
     --dst_width 352 \
     --dst_height 288 \
     --dst_frame_rate 30 \
     --wait_time 0 \
     --repeat_time 0 \
     --decode_type mlu \
     --dev_id 0
popd
