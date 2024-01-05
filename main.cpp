#include <iostream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <random>

#include "inference.h"
#include "detector.h"
#include "transcode.h"

/*
g++ main.cpp transcode.cpp frame_utils.c  detector.cpp inference.cpp \
    -I. -lavcodec -lavformat -lswscale -lswresample -lavutil -lpng \
    -I/data/dev/libopencv-4.8.1/include/opencv4/  -Wl,-rpath=/data/dev/libopencv-4.8.1/lib/ -L/data/dev/libopencv-4.8.1/lib/  -lopencv_stitching -lopencv_alphamat -lopencv_aruco -lopencv_barcode -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_dnn_objdetect -lopencv_dnn_superres -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_intensity_transform -lopencv_line_descriptor -lopencv_mcc -lopencv_quality -lopencv_rapid -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_shape -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_highgui -lopencv_datasets -lopencv_text -lopencv_plot -lopencv_ml -lopencv_videostab -lopencv_videoio -lopencv_viz -lopencv_wechat_qrcode -lopencv_ximgproc -lopencv_video -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_imgcodecs -lopencv_features2d -lopencv_dnn -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core \
    -I/data/dev/onnxruntime-linux-x64-1.16.3/include  -Wl,-rpath=/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -L/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -lonnxruntime

*/
int main(int argc, char *argv[])
{
    const char *in = "./datasets/input.mp4"; //"rtsp://admin:qwer1234@172.29.251.10:554/h264/ch33/main/av_stream";
    const char *out = "./datasets/a.mp4";

    InitDetect("models/yolov8n.onnx");

    Transcode tran(in, out);
    int ret = tran.Init();
    printf("init: %d\n", ret);

    tran.Run();
}