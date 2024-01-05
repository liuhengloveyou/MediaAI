#pragma once

#include <iostream>
#include <iomanip>
#include "inference.h"
#include <filesystem>
#include <fstream>
#include <random>

#include <opencv2/opencv.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}


#define IMG_SIZE 640

AVFrame *detect(AVFrame *frame);

int ReadCocoYaml(YOLO_V8 *&p);
int InitDetect(std::string path);