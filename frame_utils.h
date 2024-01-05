#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>
// Required to create the PNG files
#include <png.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

int frame_to_png(AVFrame *frame, const char *filename);
cv::Mat AVFrameToCvMat(AVFrame *input_avframe);
AVFrame *CVMatToAVFrame(cv::Mat &img);