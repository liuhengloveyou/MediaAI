#include <iostream>
#include <string>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include<opencv2/opencv.hpp>


#include "inference.h"
#include "detector.h"
#include "transcode.h"
#include "yolov8_opencv_detector.h"
#include "yolov8_utils.h"

using namespace std;


/*
g++ main.cpp transcode.cpp frame_utils.c  detector.cpp inference.cpp yolov8_opencv_detector.cpp yolov8_utils.cpp \
    -I. -lavcodec -lavformat -lswscale -lswresample -lavutil -lpng \
    -I/data/dev/libopencv-4.8.1/include/opencv4/  -Wl,-rpath=/data/dev/libopencv-4.8.1/lib/ -L/data/dev/libopencv-4.8.1/lib/  -lopencv_stitching -lopencv_alphamat -lopencv_aruco -lopencv_barcode -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_dnn_objdetect -lopencv_dnn_superres -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_intensity_transform -lopencv_line_descriptor -lopencv_mcc -lopencv_quality -lopencv_rapid -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_shape -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_highgui -lopencv_datasets -lopencv_text -lopencv_plot -lopencv_ml -lopencv_videostab -lopencv_videoio -lopencv_viz -lopencv_wechat_qrcode -lopencv_ximgproc -lopencv_video -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_imgcodecs -lopencv_features2d -lopencv_dnn -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core \
    -I/data/dev/onnxruntime-linux-x64-1.16.3/include  -Wl,-rpath=/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -L/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -lonnxruntime

*/
int main(int argc, char *argv[])
{
	std::string seg_model_path = "./models/yolov8s-seg.onnx";
	std::string detect_rtdetr_path = "./models/rtdetr-l.onnx";  //yolov8-redetr
	
    cv::Mat src = cv::imread("./images/zidane.jpg");
	cv::Mat img = src.clone();

   
	// Yolov8Onnx task_detect_onnx;
	// RTDETROnnx task_detect_rtdetr_onnx;
	// Yolov8Seg task_segment;
	// Yolov8SegOnnx task_segment_onnx;

	// yolov8_cv_detect<Yolov8CV>(cv_detect, img);    //yolov8 opencv detect
	// img = src.clone();
	// yolov8_onnx(task_detect_onnx,img,detect_model_path);  //yoolov8 onnxruntime detect
	
	// img = src.clone();
	// yolov8_onnx(task_detect_rtdetr_onnx, img, detect_rtdetr_path);  //yolov8-rtdetr onnxruntime detect

	// img = src.clone();
	// yolov8(task_segment,img,seg_model_path);   //yolov8 opencv segment

	// img = src.clone();
	// yolov8_onnx(task_segment_onnx,img,seg_model_path); //yolov8 onnxruntime segment

// #ifdef VIDEO_OPENCV
// 	// video_demo(task_detect, detect_model_path);
// 	//video_demo(task_segment, seg_model_path);
// #else
// 	//video_demo(task_detect_onnx, detect_model_path);
// 	//video_demo(task_detect_rtdetr_onnx, detect_rtdetr_path);
// 	//video_demo(task_segment_onnx, seg_model_path);
// #endif
    // return 0;

    const char *in = "./datasets/input.mp4"; //"rtsp://admin:qwer1234@172.29.251.10:554/h264/ch33/main/av_stream";
    const char *out = "./datasets/a.m3u8";

    InitDetect("models/yolov8n.onnx");

    Transcode tran(in, out);
    int ret = tran.Init();
    printf("init: %d\n", ret);

    tran.Run();
}