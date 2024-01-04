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

inline char *my_av_err2str(int errnum)
{
    char tmp[AV_ERROR_MAX_STRING_SIZE] = {0};
    return av_make_error_string(tmp, AV_ERROR_MAX_STRING_SIZE, errnum);
}

// Function to save an AVFrame to a PNG file
int frame_to_png(AVFrame *frame, const char *filename)
{
    int ret = 0;
    static struct SwsContext *sws_ctx = NULL;
    AVFrame *rgb_frame = NULL;
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *row_pointers = NULL;

    if (frame->format != AV_PIX_FMT_YUV420P)
    {
        printf("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
    }

    // To create the PNG files, the AVFrame data must be translated from YUV420P format into RGB24
    if (!sws_ctx)
    {
        sws_ctx = sws_getContext(
            frame->width, frame->height, (enum AVPixelFormat)frame->format,
            frame->width, frame->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, NULL, NULL, NULL);
    }

    // Allocate a new AVFrame for the output RGB24 image
    rgb_frame = av_frame_alloc();
    if (!rgb_frame)
    {
        printf("Error while preparing RGB frame: %s", my_av_err2str(ret));
        ret = -1;
        goto end;
    }

    // Set the properties of the output AVFrame
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;
    ret = av_frame_get_buffer(rgb_frame, 0);
    if (ret < 0)
    {
        printf("Error while preparing RGB frame: %s", my_av_err2str(ret));
        goto end;
    }

    printf("Transforming frame format from YUV420P into RGB24...");

    ret = sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, rgb_frame->data, rgb_frame->linesize);
    if (ret < 0)
    {
        printf("Error while translating the frame format from YUV420P into RGB24: %s", my_av_err2str(ret));
        goto end;
    }

    // Open the PNG file for writing
    fp = fopen(filename, "wb");
    if (!fp)
    {
        fprintf(stderr, "Failed to open file '%s'\n", filename);
        goto end;
    }

    // Create the PNG write struct and info struct
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fprintf(stderr, "Failed to create PNG write struct\n");
        goto end;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        fprintf(stderr, "Failed to create PNG info struct\n");
        goto end;
    }

    // Set up error handling for libpng
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        fprintf(stderr, "Error writing PNG file\n");
        goto end;
    }

    // Set the PNG file as the output for libpng
    png_init_io(png_ptr, fp);

    // Set the PNG image attributes
    png_set_IHDR(png_ptr, info_ptr, rgb_frame->width, rgb_frame->height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Allocate memory for the row pointers and fill them with the AVFrame data
    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * rgb_frame->height);
    for (int y = 0; y < rgb_frame->height; y++)
    {
        row_pointers[y] = (png_bytep)(rgb_frame->data[0] + y * rgb_frame->linesize[0]);
    }

    // Write the PNG file
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

end:
    if (rgb_frame)
    {
        av_frame_free(&rgb_frame);
    }

    // Clean up
    free(row_pointers);

    if (png_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
    }

    png_destroy_write_struct(&png_ptr, &info_ptr);
    if (fp)
    {
        fclose(fp);
    }

    return ret;
}

cv::Mat AVFrameToCvMat(AVFrame *input_avframe)
{
    if (input_avframe->format != AV_PIX_FMT_YUV420P)
    {
        printf("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
    }

    int image_width = input_avframe->width;
    int image_height = input_avframe->height;

    cv::Mat resMat(image_height, image_width, CV_8UC3);
    int cvLinesizes[1];
    cvLinesizes[0] = resMat.step1();

    SwsContext *avFrameToOpenCVBGRSwsContext = sws_getContext(
        image_width,
        image_height,
        (enum AVPixelFormat)input_avframe->format,
        /*AVPixelFormat::AV_PIX_FMT_YUV420P*/
        image_width,
        image_height,
        AVPixelFormat::AV_PIX_FMT_BGR24,
        SWS_FAST_BILINEAR,
        nullptr, nullptr, nullptr);

    sws_scale(avFrameToOpenCVBGRSwsContext,
              input_avframe->data,
              input_avframe->linesize,
              0,
              image_height,
              &resMat.data,
              cvLinesizes);

    if (avFrameToOpenCVBGRSwsContext != nullptr)
    {
        sws_freeContext(avFrameToOpenCVBGRSwsContext);
        avFrameToOpenCVBGRSwsContext = nullptr;
    }

    return resMat;
}

AVFrame *CVMatToAVFrame(cv::Mat &img)
{
    // 得到Mat信息
    int width = img.cols;
    int height = img.rows;

    // 创建AVFrame填充参数 注：调用者释放该frame
    AVFrame *frame = av_frame_alloc();
    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_YUV420P;

    // 初始化AVFrame内部空间
    int ret = av_frame_get_buffer(frame, 0 /*32*/);
    if (ret < 0)
    {
        printf("Could not allocate the video frame data");
        return nullptr;
    }
    ret = av_frame_make_writable(frame);
    if (ret < 0)
    {
        printf("Av frame make writable failed.");
        return nullptr;
    }

    // 转换颜色空间为YUV420
    cv::cvtColor(img, img, cv::COLOR_BGR2YUV_I420);

    // 按YUV420格式，设置数据地址
    int frame_size = width * height;
    unsigned char *data = img.data;
    memcpy(frame->data[0], data, frame_size);
    memcpy(frame->data[1], data + frame_size, frame_size / 4);
    memcpy(frame->data[2], data + frame_size * 5 / 4, frame_size / 4);

    return frame;
}
