/**
g++  hw_transcode.c frame_utils.c  detector.cpp inference.cpp \
    -I. -lavcodec -lavformat -lswscale -lswresample -lavutil -lpng \
    -I/data/dev/libopencv-4.8.1/include/opencv4/  -Wl,-rpath=/data/dev/libopencv-4.8.1/lib/ -L/data/dev/libopencv-4.8.1/lib/  -lopencv_stitching -lopencv_alphamat -lopencv_aruco -lopencv_barcode -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_dnn_objdetect -lopencv_dnn_superres -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_intensity_transform -lopencv_line_descriptor -lopencv_mcc -lopencv_quality -lopencv_rapid -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_shape -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_highgui -lopencv_datasets -lopencv_text -lopencv_plot -lopencv_ml -lopencv_videostab -lopencv_videoio -lopencv_viz -lopencv_wechat_qrcode -lopencv_ximgproc -lopencv_video -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_imgcodecs -lopencv_features2d -lopencv_dnn -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core \
    -I/data/dev/onnxruntime-linux-x64-1.16.3/include  -Wl,-rpath=/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -L/data/dev/onnxruntime-linux-x64-1.16.3/lib/ -lonnxruntime

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#ifdef __cplusplus
}
#endif

extern int frame_to_png(AVFrame *frame, const char *filename);
extern char *my_av_err2str(int errnum);
extern cv::Mat AVFrameToCvMat(AVFrame *input_avframe);
extern AVFrame *CVMatToAVFrame(cv::Mat &img);
extern void InitDetect();
extern void DetectorMat(cv::Mat &img);

// 轨道上下文结构体，存放解码器、编码器、输出轨道序号、轨道类型等
// Track context structure, inlcude the decoder, encoder, output track number, track type
typedef struct StreamContext
{
    enum AVMediaType codec_type; // 轨道类型

    const AVCodec *decoder;
    AVCodecContext *decoder_ctx; // 解码器

    AVCodecContext *encoder_ctx; // 编码器
    const AVCodec *encoder;

    unsigned int outIndex; // 输出轨道序号

    int isDecodeEnd; // 解码器处理完毕标志
    int isEncodeEnd; // 编码器处理完毕标志
} StreamContext;

static AVBufferRef *hw_device_ctx = NULL;
static AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;

// 轨道上下文关联表
StreamContext *streamContextMapping = NULL;
int streamContextLength = 0;
int video_stream_index = -1;

// 进度
float duration = 0;
float dec_pos = 0;

// 统计线程
void *stat_thread(void *arg)
{
    while (1)
    {
        sleep(1);
        // printf("stat:: %.2f \n", dec_pos / duration);

        // if (progressCB)
        // {
        //     // progressCB("TranscodeProgress", ctx->aFilePath, ctx->iFmtCtx->duration, packet->pos);
        // }
    }
}

static enum AVPixelFormat get_format(AVCodecContext *avctx, const enum AVPixelFormat *pix_fmts)
{
    while (*pix_fmts != AV_PIX_FMT_NONE)
    {
        if (*pix_fmts == AV_PIX_FMT_QSV)
        {
            return AV_PIX_FMT_QSV;
        }

        pix_fmts++;
    }

    fprintf(stderr, "The QSV pixel format not offered in get_format()\n");

    return AV_PIX_FMT_NONE;
}

static int open_hw_device()
{
    // 打开硬件上下文
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV, NULL, NULL, 0);
    if (ret < 0)
    {
        hw_device_ctx = NULL;
        fprintf(stderr, "Failed to create a QSV device. Error code: %s\n", my_av_err2str(ret));
        return AVERROR(EPERM);
    }

    return ret;
}

static int open_input_stream(const char *url)
{
    int ret = 0;

    AVDictionary *opt = NULL;                   // 设置输入源封装参数
    av_dict_set(&opt, "rw_timeout", "3000", 0); // 设置网络超时
    if ((ret = avformat_open_input(&ifmt_ctx, url, NULL, &opt)) < 0)
    {
        fprintf(stderr, "Cannot open input '%s', Error code: %s\n", url, my_av_err2str(ret));
        return ret;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0)
    {
        fprintf(stderr, "Cannot find input stream information. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }

    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot find a video stream in the input file. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }
    video_stream_index = ret;
    duration = ifmt_ctx->duration * av_q2d(AV_TIME_BASE_Q);

    // 根据源轨道信息创建streamContextMapping
    streamContextLength = ifmt_ctx->nb_streams;
    streamContextMapping = (StreamContext *)av_malloc_array(streamContextLength, sizeof(*streamContextMapping));
    if (!streamContextMapping)
    {
        streamContextLength = 0;
        fprintf(stderr, "Could not allocate stream context mapping.");
    }
    for (int i = 0; i < streamContextLength; i++)
    {
        printf("codec_type: %d %d\n", i, ifmt_ctx->streams[i]->codecpar->codec_type);
        streamContextMapping[i].codec_type = ifmt_ctx->streams[i]->codecpar->codec_type;
        streamContextMapping[i].decoder_ctx = NULL;
        streamContextMapping[i].encoder_ctx = NULL;
        streamContextMapping[i].encoder = NULL;
        streamContextMapping[i].outIndex = -1;
        streamContextMapping[i].isDecodeEnd = false;
        streamContextMapping[i].isEncodeEnd = false;
    }

    return ret;
}

static int open_decoder()
{
    int ret = 0;

    // 初始化轨道上下文列表
    if (streamContextLength == 0)
    {
        return AVERROR(EPERM);
    }

    // STEP::根据输入文件的流信息创建解码器
    for (int i = 0; i < streamContextLength; i++)
    {
        const AVCodec *decoder = NULL;
        AVCodecContext *decoder_ctx = NULL;

        AVStream *inStream = ifmt_ctx->streams[i];
        // 过滤除video、audio、subtitle以外的轨道
        if (inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            inStream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
        {
            continue;
        }

        if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (hw_device_ctx == NULL)
            {
                decoder = avcodec_find_decoder(inStream->codecpar->codec_id);
            }
            else
            {
                switch (inStream->codecpar->codec_id)
                {
                case AV_CODEC_ID_H264:
                    decoder = avcodec_find_decoder_by_name("h264_qsv");
                    break;
                case AV_CODEC_ID_HEVC:
                    decoder = avcodec_find_decoder_by_name("hevc_qsv");
                    break;
                case AV_CODEC_ID_VP9:
                    decoder = avcodec_find_decoder_by_name("vp9_qsv");
                    break;
                case AV_CODEC_ID_VP8:
                    decoder = avcodec_find_decoder_by_name("vp8_qsv");
                    break;
                case AV_CODEC_ID_AV1:
                    decoder = avcodec_find_decoder_by_name("av1_qsv");
                    break;
                case AV_CODEC_ID_MPEG2VIDEO:
                    decoder = avcodec_find_decoder_by_name("mpeg2_qsv");
                    break;
                case AV_CODEC_ID_MJPEG:
                    decoder = avcodec_find_decoder_by_name("mjpeg_qsv");
                    break;
                default:
                    fprintf(stderr, "Codec is not supportted by qsv\n");
                    return -1;
                }
            }

            if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
                return AVERROR(ENOMEM);

            if ((ret = avcodec_parameters_to_context(decoder_ctx, inStream->codecpar)) < 0)
            {
                fprintf(stderr, "avcodec_parameters_to_context error. Error code: %s\n", my_av_err2str(ret));
                return ret;
            }
            // decoder_ctx->framerate = av_guess_frame_rate(ifmt_ctx, inStream, NULL);

            if (hw_device_ctx)
            {
                decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                if (!decoder_ctx->hw_device_ctx)
                {
                    fprintf(stderr, "A hardware device reference create failed.\n");
                    return AVERROR(ENOMEM);
                }
                decoder_ctx->get_format = get_format;
                // decoder_ctx->pkt_timebase = inStream->time_base;
            }

            AVDictionary *optionsDict = NULL;
            // av_dict_set(&optionsDict, "threads", "2", 0);
            if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0)
                fprintf(stderr, "Failed to open codec for decoding. Error code: %s\n", my_av_err2str(ret));
        }
        else if (inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            // 不转码
        }

        streamContextMapping[i].decoder_ctx = decoder_ctx;
        streamContextMapping[i].decoder = decoder;
    }

    return 0;
}

static int open_output_stream(const char *url)
{
    int ret = 0;

    if ((ret = (avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, url))) < 0)
    {
        fprintf(stderr, "Failed to deduce output format from file extension. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }

    // STEP::根据源轨道信息创建输出文件的音视频轨道
    // 由于仅做转封装是无法改变音视频数据的，所以轨道信息只能复制
    unsigned int outStreamIndex = 0;
    for (unsigned int i = 0; i < streamContextLength; i++)
    {
        AVStream *inStream = ifmt_ctx->streams[i];
        // 创建输出的轨道
        AVStream *outStream = avformat_new_stream(ofmt_ctx, NULL);
        if (streamContextMapping[i].encoder_ctx)
        {
            // 尝试从编码器复制输出轨道信息
            ret = avcodec_parameters_from_context(outStream->codecpar, streamContextMapping[i].encoder_ctx);
        }
        else
        {
            ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar); // 复制源轨道的信息到输出轨道
        }
        if (ret < 0)
        {
            fprintf(stderr, "Could not copy codec parameters.");
        }

        outStream->codecpar->codec_tag = 0;
        streamContextMapping[i].outIndex = outStreamIndex++; // 记录源文件轨道序号与输出文件轨道序号的对应关系
    }

    // 打开输出文件
    ret = avio_open(&ofmt_ctx->pb, url, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot open output. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }

    /* write the stream header */
    if ((ret = avformat_write_header(ofmt_ctx, NULL)) < 0)
    {
        fprintf(stderr, "Could not write stream header to out: %s\n", my_av_err2str(ret));
        return ret;
    }

    return 0;
}

static int open_encoder()
{
    int ret = 0;
    AVDictionaryEntry *e = NULL;
    AVDictionary *opts = NULL;
    AVStream *ost;

    // STEP::根据解码器创建编码器，因为单纯的转编码无法改变音视频基础参数，如分辨率、采样率等，所以这些参数只能复制
    for (int i = 0; i < streamContextLength; i++)
    {
        if (!streamContextMapping[i].decoder_ctx)
        {
            continue;
        }

        AVCodecContext *encoder_ctx = NULL;
        const AVCodec *encoder = NULL;

        if (streamContextMapping[i].codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (hw_device_ctx)
            {
                if (!(encoder = avcodec_find_encoder_by_name("h264_qsv")))
                {
                    fprintf(stderr, "Could not find encoder\n");
                    return -1;
                }
            }
            else if (!(encoder = avcodec_find_encoder(streamContextMapping[i].decoder_ctx->codec_id)))
            {
                fprintf(stderr, "Could not find encoder\n");
                return -1;
            }

            if (!(encoder_ctx = avcodec_alloc_context3(encoder)))
            {
                return AVERROR(ENOMEM);
            }

            AVCodecContext *decoder_ctx = streamContextMapping[i].decoder_ctx;
            encoder_ctx->height = decoder_ctx->height;
            encoder_ctx->width = decoder_ctx->width;
            encoder_ctx->framerate = decoder_ctx->framerate;
            encoder_ctx->sample_aspect_ratio = decoder_ctx->sample_aspect_ratio;
            if (encoder->pix_fmts)
            {
                encoder_ctx->pix_fmt = encoder->pix_fmts[0];
            }
            else
            {
                encoder_ctx->pix_fmt = decoder_ctx->pix_fmt;
            }
            encoder_ctx->time_base = av_inv_q(streamContextMapping[i].decoder_ctx->framerate);

            // 可以设置与编码相关的参数，如gop、去除b帧、码率等
            // You can set and related parameters such as gop, removing b frames, and bitrate
            //  encoder->gop_size = 40;
            //  encoder->max_b_frames = 0;
            // encoder->bit_rate = 2000000;

            // we need to ref hw_frames_ctx of decoder to initialize encoder's codec.
            // Only after we get a decoded frame, can we obtain its hw_frames_ctx

            if (hw_device_ctx)
            {
                encoder_ctx->hw_frames_ctx = av_buffer_ref(streamContextMapping[i].decoder_ctx->hw_frames_ctx);
                if (!encoder_ctx->hw_frames_ctx)
                {
                    return AVERROR(ENOMEM);
                }

                encoder_ctx->pix_fmt = AV_PIX_FMT_QSV;
            }

            // if ((ret = str_to_dict(optstr, &opts)) < 0) {
            //     fprintf(stderr, "Failed to set encoding parameter.\n");
            //     goto fail;
            // }
            /* There is no "framerate" option in commom option list. Use "-r" to
             * set framerate, which is compatible with ffmpeg commandline. The
             * video is assumed to be average frame rate, so set time_base to
             * 1/framerate. */
            // e = av_dict_get(opts, "r", NULL, 0);
            // if (e) {
            //     encoder_ctx->framerate = av_d2q(atof(e->value), INT_MAX);
            //     encoder_ctx->time_base = av_inv_q(encoder_ctx->framerate);
            // }
            if ((ret = avcodec_open2(encoder_ctx, encoder, &opts)) < 0)
            {
                fprintf(stderr, "Failed to open encode codec. Error code: %s\n", my_av_err2str(ret));
                av_dict_free(&opts);

                return -1;
            }
            av_dict_free(&opts);
        }
        if (streamContextMapping[i].codec_type == AVMEDIA_TYPE_AUDIO)
        {
        }

        streamContextMapping[i].encoder_ctx = encoder_ctx;
        streamContextMapping[i].encoder = encoder;
    }

    return 0;
}

static int encode_write(AVCodecContext *encoder_ctx, AVPacket *enc_pkt, AVFrame *frame)
{
    int ret = 0;

    av_packet_unref(enc_pkt);

    if ((ret = avcodec_send_frame(encoder_ctx, frame)) < 0)
    {
        fprintf(stderr, "Error during encoding. Error code: %s\n", my_av_err2str(ret));
        goto end;
    }
    while (1)
    {
        if (ret = avcodec_receive_packet(encoder_ctx, enc_pkt))
            break;
        enc_pkt->stream_index = 0;
        av_packet_rescale_ts(enc_pkt, encoder_ctx->time_base, ofmt_ctx->streams[0]->time_base);
        if ((ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt)) < 0)
        {
            fprintf(stderr, "Error during writing data to output file. Error code: %s\n", my_av_err2str(ret));
            return ret;
        }
    }

end:
    if (ret == AVERROR_EOF)
        return 0;
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);

    return ret;
}

static int dec_enc(AVPacket *pkt, char *optstr)
{
    int ret = 0;
    static int count = 0;

    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    AVCodecContext *decoder_ctx = streamContextMapping[pkt->stream_index].decoder_ctx;
    AVCodecContext *encoder_ctx = streamContextMapping[pkt->stream_index].encoder_ctx;

    ret = avcodec_send_packet(decoder_ctx, pkt);
    if (ret < 0)
    {
        fprintf(stderr, "Error during decoding. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }

    while (ret >= 0)
    {

        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frame);
            return 0;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "Error while decoding. Error code: %s\n", my_av_err2str(ret));
            break;
        }
        if (encoder_ctx == NULL)
        {
            if (open_encoder() != 0)
            {
                break;
            }
            encoder_ctx = streamContextMapping[pkt->stream_index].encoder_ctx;
        }

        frame->pts = av_rescale_q(frame->pts, decoder_ctx->pkt_timebase, encoder_ctx->time_base);
        printf(">>>>>>>>>>>>>>%ld %ld\n", pkt->pts, frame->pts);

        cv::Mat img = AVFrameToCvMat(frame);
        DetectorMat(img);
        AVFrame *ai_frame = CVMatToAVFrame(img);

        // save a frame into a .PNG file
        // char frame_filename[1024];
        // sprintf(frame_filename, "frame-%d.png", count++);
        // ret = frame_to_png(ai_frame, frame_filename);
        // if (ret < 0)
        // {
        //     fprintf(stderr, "Failed to write PNG file\n");
        //     return -1;
        // }

       ai_frame->pts = frame->pts;
        if ((ret = encode_write(encoder_ctx, pkt, ai_frame)) < 0)
            fprintf(stderr, "Error during encoding and writing.\n");

        av_frame_free(&ai_frame);
    }

    av_frame_free(&frame);
    if (ret < 0)
        return ret;

    return 0;
}

static int flushEnd(AVPacket *packet)
{
    int ret = 0;
    AVFrame *frame = NULL;
    av_packet_unref(packet);

    for (unsigned int i = 0; i < streamContextLength; i++)
    {
        if (!streamContextMapping[i].decoder_ctx)
        {
            continue;
        }

        /* flush decoder */
        av_packet_unref(packet);
        if ((ret = dec_enc(packet, NULL)) < 0)
        {
            fprintf(stderr, "Failed to flush decoder %s\n", my_av_err2str(ret));
            return ret;
        }

        /* flush encoder */
        if ((ret = encode_write(streamContextMapping[i].encoder_ctx, packet, NULL)) < 0)
        {
            fprintf(stderr, "Failed to flush encoder %s\n", my_av_err2str(ret));
            return ret;
        }
    }

    return 0;
}

int transcode(const char *in, const char *out)
{
    int ret = 0;
    pthread_t thread_stat_id;
    struct timeval tv_begin, tv_end;

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        fprintf(stderr, "Failed to allocate decode packet\n");
        return -1;
    }

    // 启动统计线程
    int err = pthread_create(&thread_stat_id, NULL, stat_thread, NULL);
    if (err != 0)
    {
        printf("stat_thread err %s\n", strerror(err));
        exit(-1);
    }

    // 打开源文件并获取源文件信息
    if ((ret = open_input_stream(in)) < 0)
        goto end;

    // 打开硬解码
    open_hw_device();

    // 初始化解码器
    if ((ret = open_decoder()) < 0)
        goto end;

    printf("open_decoder ok.\n\n");

    // 打开输出文件
    if ((ret = open_output_stream(out)) < 0)
        goto end;

    // 循环处理数据
    while (ret >= 0)
    {
        if ((ret = av_read_frame(ifmt_ctx, packet)) < 0)
            break;

        dec_pos = (double)packet->pts * av_q2d(ifmt_ctx->streams[packet->stream_index]->time_base);

        // 根据之前的关联关系，判断是否舍弃此packet
        if (streamContextMapping[packet->stream_index].outIndex < 0)
        {
            av_packet_unref(packet);
            continue;
        }

        if (streamContextMapping[packet->stream_index].decoder_ctx)
        {

            gettimeofday(&tv_begin, NULL);
            // 需要转编码的轨道
            ret = dec_enc(packet, NULL);
            gettimeofday(&tv_end, NULL);
            printf("tv_begin_sec:%ld\n", tv_end.tv_usec - tv_begin.tv_usec);
        }
        else
        {
            // 不需要转编码的轨道，no need to transcoding
            // 转换timebase（时间基），一般不同的封装格式下，时间基是不一样的
            // AVStream *inStream = ifmt_ctx->streams[packet->stream_index];
            // AVStream *outStream = ofmt_ctx->streams[streamContextMapping[packet->stream_index].outIndex];
            // av_packet_rescale_ts(packet, inStream->time_base, outStream->time_base);

            // // 将轨道序号修改为对应的输出文件轨道序号
            // packet->stream_index = streamContextMapping[packet->stream_index].outIndex;

            // // 封装packet，并写入输出文件
            // ret = av_interleaved_write_frame(ofmt_ctx, packet);
            // if (ret < 0)
            // {
            //     fprintf(stderr, "Could not mux packet.");
            // }

            // av_packet_unref(packet);
        }

        av_packet_unref(packet);
    }

    // flush编解码器
    ret = flushEnd(packet);
    if (ret < 0)
    {
        goto end;
    }
    dec_pos = duration;

    /* write the trailer for output stream */
    if ((ret = av_write_trailer(ofmt_ctx)) < 0)
        fprintf(stderr, "Failed to write trailer %s\n", my_av_err2str(ret));

end:
    // 释放编码器、解码器
    for (int i = 0; i < streamContextLength; i++)
    {
        if (streamContextMapping[i].decoder_ctx)
        {
            avcodec_free_context(&streamContextMapping[i].decoder_ctx);
            streamContextMapping[i].decoder_ctx = NULL;
        }
        if (streamContextMapping[i].encoder_ctx)
        {
            avcodec_free_context(&streamContextMapping[i].encoder_ctx);
            streamContextMapping[i].encoder_ctx = NULL;
        }
    }

    avformat_close_input(&ifmt_ctx);
    avformat_close_input(&ofmt_ctx);
    av_buffer_unref(&hw_device_ctx);
    av_packet_free(&packet);

    return ret;
}

int main(int argc, char **argv)
{
    const char *in = "./datasets/input.mp4"; //"rtsp://admin:qwer1234@172.29.251.10:554/h264/ch33/main/av_stream";
    const char *out = "./datasets/a.m3u8";
    InitDetect();

    // av_log_set_level(AV_LOG_DEBUG);
    transcode(in, out);
}
