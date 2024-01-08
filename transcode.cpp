#include <stdlib.h>
#include <unistd.h>
#include <iostream>

extern "C"
{
#include <libavutil/macros.h>
#include <libavutil/timestamp.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
}

#include "transcode.h"
// #include "detector.h"

static inline char *my_av_err2str(int errnum)
{
    char tmp[AV_ERROR_MAX_STRING_SIZE] = {0};
    return av_make_error_string(tmp, AV_ERROR_MAX_STRING_SIZE, errnum);
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

Transcode::Transcode(const char *in, const char *out) : inUrl(in), outUrl(out)
{
}

Transcode::~Transcode()
{

    if (streamContextMap)
    {
        for (int i = 0; i < streamContextLength; i++)
        {
            if (streamContextMap[i].decoderCtx)
            {
                avcodec_free_context(&streamContextMap[i].decoderCtx);
                streamContextMap[i].decoderCtx = NULL;
            }
            if (streamContextMap[i].encoderCtx)
            {
                avcodec_free_context(&streamContextMap[i].encoderCtx);
                streamContextMap[i].encoderCtx = NULL;
            }
        }

        av_free(streamContextMap);
    }

    avformat_free_context(outFmtCtx);
    outFmtCtx = nullptr;
    avformat_free_context(inFmtCtx);
    inFmtCtx = nullptr;
}

int Transcode::Init()
{
    int ret = 0;

    // 打开硬件上下文
    // ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_QSV, NULL, NULL, 0);
    // if (ret < 0)
    // {
    //     hwDeviceCtx = nullptr;
    //     fprintf(stderr, "!!!Failed to create a QSV device. Error: %d %s\n", ret, my_av_err2str(ret));
    // }

    ret = OpenInStream();
    if (ret)
    {
        return ret;
    }

    ret = OpenOutStream();
    if (ret)
    {
        return ret;
    }

    // 初始化bsf filter
    if (is_annexb)
    {
        // if (ret = open_bitstream_filter(remuxer->video_stream, &remuxer->bsf_ctx, "h264_mp4toannexb") < 0)
        // {
        //     fprintf(stderr, "open_bitstream_filter failed, ret=%d\n", ret);
        //     return ret;
        // }
    }

    return 0;
}

int Transcode::OpenInStream()
{
    if (!inUrl)
    {
        return -1;
    }

    AVDictionary *optionsDict = NULL;                   // 设置输入源封装参数
    av_dict_set(&optionsDict, "rw_timeout", "2000", 0); // 设置网络超时
    av_dict_set(&optionsDict, "sdp_flags", "custom_io", 0);
    av_dict_set_int(&optionsDict, "reorder_queue_size", 0, 0);
    av_dict_set(&optionsDict, "rtsp_transport", "tcp", 0);
    av_dict_set(&optionsDict, "buffer_size", "102400", 0); //设置缓存大小，1080p可将值调大
    int ret = avformat_open_input(&inFmtCtx, inUrl, NULL, &optionsDict);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot open input '%s', Error: %s\n", inUrl, my_av_err2str(ret));
        return ret;
    }

    // 获取源视频流信息
    ret = avformat_find_stream_info(inFmtCtx, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to retrieve input stream information, Error: %s\n", my_av_err2str(ret));
        return ret;
    }
    av_dump_format(inFmtCtx, 0, inUrl, 0);

    video_stream_index = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0)
    {
        fprintf(stderr, "Cannot find a video stream in the input file. Error code: %s\n", my_av_err2str(ret));
        return video_stream_index;
    }
    // is_annexb = strcmp(av_fourcc2str(inFmtCtx->streams[video_stream_index]->codecpar->codec_tag), "avc1") >= 0 ? 1 : 0;

    // 根据源轨道信息创建streamContextMapping
    streamContextLength = inFmtCtx->nb_streams;
    streamContextMap = (StreamContext *)av_malloc_array(streamContextLength, sizeof(*streamContextMap));
    if (!streamContextMap)
    {
        streamContextLength = 0;
        fprintf(stderr, "Could not allocate stream context map\n");
        return -1;
    }
    for (int i = 0; i < streamContextLength; i++)
    {
        streamContextMap[i].codec_type = inFmtCtx->streams[i]->codecpar->codec_type;
        streamContextMap[i].outIndex = -1;
        streamContextMap[i].decoderCtx = nullptr;
        streamContextMap[i].encoderCtx = nullptr;
    }

    // 根据输入文件的流信息创建解码器
    for (int i = 0; i < streamContextLength; i++)
    {
        const AVCodec *decoder = nullptr;
        AVCodecContext *decoder_ctx = nullptr;
        AVStream *inStream = inFmtCtx->streams[i];

        if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // 根据输入文件的流信息寻找解码器
            if (hwDeviceCtx == nullptr)
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

            if (!decoder)
            {
                fprintf(stderr, "Could not find decoder for stream: %d\n", inStream->codecpar->codec_id);
                return AVERROR(ENOMEM);
            }

            // 创建解码器上下文
            decoder_ctx = avcodec_alloc_context3(decoder);
            if (!decoder_ctx)
            {
                fprintf(stderr, "Could not allocate decoder context.\n");
                return AVERROR(ENOMEM);
            }

            // 从流信息拷贝参数到解码器上下文
            ret = avcodec_parameters_to_context(decoder_ctx, inStream->codecpar);
            if (ret < 0)
            {
                fprintf(stderr, "Could not copy parameters from the stream information to the decoder context.\n");
                return AVERROR(ENOMEM);
            }
            decoder_ctx->framerate = av_guess_frame_rate(inFmtCtx, inStream, NULL);
            decoder_ctx->pkt_timebase = inStream->time_base;
            if (hwDeviceCtx)
            {
                decoder_ctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
                if (!decoder_ctx->hw_device_ctx)
                {
                    fprintf(stderr, "A hardware device reference create failed.\n");
                    return AVERROR(ENOMEM);
                }
                decoder_ctx->get_format = get_format;
            }

            AVDictionary *optionsDict = NULL;
            // av_dict_set(&optionsDict, "threads", "2", 0); //可设置解码器的一些参数，如处理线程数，Set some parameters of the decoder, such as the number of processing threads
            ret = avcodec_open2(decoder_ctx, decoder, &optionsDict);
            if (ret < 0)
            {
                fprintf(stderr, "Failed to open decoder. Error: %s\n", my_av_err2str(ret));
                return ret;
            }
            printf("input decoder_ctx %d time_base: %d; framerate: %d pkt_timebase: %d\n",
                   i,
                   decoder_ctx->time_base.den,
                   decoder_ctx->framerate.den, decoder_ctx->pkt_timebase.den);
        }

        streamContextMap[i].decoderCtx = decoder_ctx;
        streamContextMap[i].codec_type = inStream->codecpar->codec_type;
    }

    return 0;
}

int Transcode::OpenOutStream()
{
    // 创建输出文件句柄outFileHandle
    // 设置输出的格式，如下设置等于ffmpeg ... -f flv rtmp://xxx
    // avformat_alloc_output_context2(&outFileHandle, NULL, "flv", outFilePath);
    int ret = avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, outUrl);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to open output stream. Error: %d %s\n", ret, my_av_err2str(ret));
        return ret;
    }

    // 根据源轨道信息创建输出文件的音视频轨道
    // 由于仅做转封装是无法改变音视频数据的，所以轨道信息只能复制
    unsigned int outStreamIndex = 0;
    for (unsigned int i = 0; i < streamContextLength; i++)
    {
        AVStream *inStream = inFmtCtx->streams[i];

        // 过滤除video、audio、subtitle以外的轨道
        if (streamContextMap[i].codec_type != AVMEDIA_TYPE_AUDIO &&
            streamContextMap[i].codec_type != AVMEDIA_TYPE_VIDEO &&
            streamContextMap[i].codec_type != AVMEDIA_TYPE_SUBTITLE)
        {
            streamContextMap[i].outIndex = -1;
            continue;
        }

        // 创建输出的轨道
        AVStream *outStream = avformat_new_stream(outFmtCtx, NULL);
        if (streamContextMap[i].encoderCtx)
        {
            // 尝试从编码器复制输出轨道信息
            ret = avcodec_parameters_from_context(outStream->codecpar, streamContextMap[i].encoderCtx);
        }
        else
        {
            // 复制源轨道的信息到输出轨道
            ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        }
        if (ret < 0)
        {
            fprintf(stderr, "Could not copy codec parameters to out stream.");
        }

        outStream->codecpar->codec_tag = 0;
        if (outStream->codecpar->codec_id == AV_CODEC_ID_HEVC)
        {
            outStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        }
        streamContextMap[i].outIndex = outStreamIndex++; // 记录源文件轨道序号与输出文件轨道序号的对应关系
    }

    // 打开输出文件
    ret = avio_open(&outFmtCtx->pb, outUrl, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot open output. Error: %d %s\n", ret, my_av_err2str(ret));
        return ret;
    }

    /* write the stream header */
    AVDictionary *options = NULL;
    if ((ret = av_dict_set(&options, "hls_time", "3", 0)) < 0)
    {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", my_av_err2str(ret));
        return ret;
    }
    if ((ret = av_dict_set(&options, "hls_list_size", "0", 0)) < 0)
    {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", my_av_err2str(ret));
        return ret;
    }
    if ((ret = av_dict_set(&options, "hls_wrap", "0", 0)) < 0)
    {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", my_av_err2str(ret));
        return ret;
    }
    if ((ret = av_dict_set(&options, "v", "hvc1", 0)) < 0)
    {
        fprintf(stderr, "av_dict_set Error: %s\n", my_av_err2str(ret));
        return ret;
    }
    if ((ret = avformat_write_header(outFmtCtx, &options)) < 0)
    {
        fprintf(stderr, "Could not write stream header to out file: %s\n", my_av_err2str(ret));
        return ret;
    }

    return 0;
}

int Transcode::OpenEncoder()
{
    int ret = 0;

    // 根据解码器创建编码器，因为单纯的转编码无法改变音视频基础参数，如分辨率、采样率等，所以这些参数只能复制
    for (int i = 0; i < streamContextLength; i++)
    {
        if (streamContextMap[i].decoderCtx == nullptr)
        {
            continue;
        }

        const AVCodec *encoder = nullptr;
        AVCodecContext *encoder_ctx = nullptr;

        if (streamContextMap[i].codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (hwDeviceCtx)
            {
                encoder = avcodec_find_encoder_by_name("h264_qsv");
            }
            else
            {
                encoder = avcodec_find_encoder(streamContextMap[i].decoderCtx->codec_id);
            }

            if (!encoder)
            {
                fprintf(stderr, "Could not find encoder for video stream.");
                return AVERROR(ENOMEM);
            }

            if (!(encoder_ctx = avcodec_alloc_context3(encoder)))
            {
                fprintf(stderr, "avcodec_alloc_context3 Error.\n");
                return AVERROR(ENOMEM);
            }

            printf(">>>>>>>>>>>>>>>>>>>>>>>>>%d %d %s\n", AV_CODEC_ID_HEVC, encoder_ctx->codec_id, encoder_ctx->codec_tag);
            // 从流信息拷贝参数到解码器上下文
            // ret = avcodec_parameters_to_context(encoder_ctx, outFmtCtx->streams[0]->codecpar);
            // if (ret < 0)
            // {
            //     fprintf(stderr, "Could not copy parameters from the stream information to the encoder context.\n");
            //     return ret;
            // }

            if (hwDeviceCtx)
            {
                encoder_ctx->hw_frames_ctx = av_buffer_ref(streamContextMap[i].decoderCtx->hw_frames_ctx);
                if (!encoder_ctx->hw_frames_ctx)
                {
                    return AVERROR(ENOMEM);
                }

                encoder_ctx->pix_fmt = AV_PIX_FMT_QSV;
            }

            AVCodecContext *decoder_ctx = streamContextMap[i].decoderCtx;
            encoder_ctx->height = decoder_ctx->height;
            encoder_ctx->width = decoder_ctx->width;
            encoder_ctx->framerate = decoder_ctx->framerate;
            encoder_ctx->sample_aspect_ratio = decoder_ctx->sample_aspect_ratio;
            encoder_ctx->time_base = av_inv_q(decoder_ctx->framerate);
            if (encoder->pix_fmts)
            {
                encoder_ctx->pix_fmt = encoder->pix_fmts[0];
            }
            else
            {
                encoder_ctx->pix_fmt = decoder_ctx->pix_fmt;
            }

            AVDictionary *opts = NULL;
            // av_dict_set(&optionsDict, "threads", "2", 0);
            int ret = avcodec_open2(encoder_ctx, encoder, &opts);
            if (ret < 0)
            {
                fprintf(stderr, "Failed to open encode. Error: %d %s\n", ret, my_av_err2str(ret));
                av_dict_free(&opts);

                return -1;
            }
        }
        else if (streamContextMap[i].codec_type == AVMEDIA_TYPE_AUDIO)
        {
            // 不处理
        }

        // encoder->time_base = AV_TIME_BASE_Q; // 一些编码器会修改timebase，这里做一次覆盖设置
        streamContextMap[i].encoderCtx = encoder_ctx;
    }

    return 0;
}

int Transcode::EncodeWrite(AVPacket *packet, AVFrame *frame)
{
    int ret = 0;
    unsigned int inIndex = packet->stream_index;
    unsigned int outIndex = streamContextMap[inIndex].outIndex;
    AVCodecContext *encoder_ctx = streamContextMap[inIndex].encoderCtx;

    av_packet_unref(packet);

    if ((ret = avcodec_send_frame(encoder_ctx, frame)) < 0)
    {
        fprintf(stderr, "Error during encoding. Error code: %s\n", my_av_err2str(ret));
        goto end;
    }

    while (1)
    {
        if (ret = avcodec_receive_packet(encoder_ctx, packet))
            break;

        packet->stream_index = 0;
        av_packet_rescale_ts(packet, encoder_ctx->time_base, outFmtCtx->streams[outIndex]->time_base);
        if (packet->duration == 0)
        {
            packet->duration = av_rescale_q(1, encoder_ctx->time_base, outFmtCtx->streams[outIndex]->time_base);
        }

        if ((ret = av_interleaved_write_frame(outFmtCtx, packet)) < 0)
        {
            fprintf(stderr, "Error during writing data to output file. Error: %s\n", my_av_err2str(ret));
            return ret;
        }
    }

end:
    if (ret == AVERROR_EOF)
        return 0;
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);

    return ret;
}

int Transcode::DecodePacket(AVPacket *packet)
{
    int ret = 0;
    AVFrame *frame = NULL;
    AVFrame *yoloFrame = NULL;

    unsigned int inIndex = packet->stream_index;
    unsigned int outIndex = streamContextMap[inIndex].outIndex;
    AVStream *inStream = inFmtCtx->streams[inIndex];
    AVStream *outStream = outFmtCtx->streams[outIndex];
    AVCodecContext *encoder_ctx = streamContextMap[inIndex].encoderCtx;
    AVCodecContext *decoder_ctx = streamContextMap[inIndex].decoderCtx;

    ret = avcodec_send_packet(decoder_ctx, packet);
    if (ret < 0)
    {
        fprintf(stderr, "Error during decoding. Error code: %s\n", my_av_err2str(ret));
        return ret;
    }

    while (ret >= 0)
    {
        if (!(frame = av_frame_alloc()))
            return AVERROR(ENOMEM);

        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frame);
            return 0;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "Error while decoding. Error code: %s\n", my_av_err2str(ret));
            goto fail;
        }
        if (!encoder_ctx)
        {
            OpenEncoder();
            encoder_ctx = streamContextMap[inIndex].encoderCtx;
        }

        frame->pts = av_rescale_q(frame->pts, decoder_ctx->pkt_timebase, encoder_ctx->time_base);
        // 调yolo
        // yoloFrame = detect(frame);
        // yoloFrame->pts = frame->pts;
        // yoloFrame->duration = frame->duration;

        if ((ret = EncodeWrite(packet, frame)) < 0)
        {
            fprintf(stderr, "Error during encoding and writing.\n");
        }

    fail:
        av_frame_free(&frame);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int Transcode::TransCodePkt(AVPacket *packet, AVFrame *frame)
{
    unsigned int inIndex = packet->stream_index;
    unsigned int outIndex = streamContextMap[inIndex].outIndex;
    AVStream *inStream = inFmtCtx->streams[inIndex];
    AVStream *outStream = outFmtCtx->streams[outIndex];

    // 根据编码器的timebase换算packet的相关时间戳
    // av_packet_rescale_ts(packet, inStream->time_base, streamContextMap[inIndex].decoderCtx->time_base);

    // 将数据包发送到解码器
    int ret = avcodec_send_packet(streamContextMap[inIndex].decoderCtx, packet);
    if (ret < 0)
    {
        fprintf(stderr, "avcodec_send_packet for decode Error: %d %s\n", ret, my_av_err2str(ret));
        return ret;
    }

    av_packet_unref(packet);

    while (1)
    {
        // 尝试从解码器取出原始帧
        ret = avcodec_receive_frame(streamContextMap[inIndex].decoderCtx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "Could not receive decoding. Error: %d %s\n", ret, my_av_err2str(ret));
            return ret;
        }

        if (streamContextMap[inIndex].encoderCtx == nullptr)
        {
            if (OpenEncoder())
            {
                break;
            }
        }

        frame->pts = av_rescale_q(frame->pts, streamContextMap[inIndex].decoderCtx->pkt_timebase, streamContextMap[inIndex].encoderCtx->time_base);
        // 将原始帧发送到编码器进行编码
        ret = avcodec_send_frame(streamContextMap[inIndex].encoderCtx, frame);
        if (ret < 0)
        {
            fprintf(stderr, "decoding Error: %d %s\n", ret, my_av_err2str(ret));
            return ret;
        }
        av_frame_unref(frame);

        while (1)
        {
            // 尝试从编码器取出编码后的数据包
            ret = avcodec_receive_packet(streamContextMap[inIndex].encoderCtx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                fprintf(stderr, "receive decoding Error: %d %s\n", ret, my_av_err2str(ret));
                return ret;
            }

            // 将轨道序号修改为对应的输出文件轨道序号
            packet->stream_index = outIndex;

            // 根据输出流的timebase换算packet的相关时间戳
            av_packet_rescale_ts(packet, streamContextMap[inIndex].encoderCtx->time_base, outStream->time_base);

            // 封装packet，并写入输出文件
            ret = av_interleaved_write_frame(outFmtCtx, packet);
            if (ret < 0)
            {
                fprintf(stderr, "Could not mux packet. Error: %d %s\n", ret, my_av_err2str(ret));
                return ret;
            }
            av_packet_unref(packet);
        }
    }

    return 0;
}

// 初始化 bsf 过滤器
int Transcode::OpenBitstreamFilter(AVStream *stream, AVBSFContext **bsf_ctx, const char *name)
{
    int ret = 0;

    const AVBitStreamFilter *filter = av_bsf_get_by_name(name);
    if (!filter)
    {
        ret = -1;
        fprintf(stderr, "Unknow bitstream filter.\n");
    }
    if ((ret = av_bsf_alloc(filter, bsf_ctx) < 0))
    {
        fprintf(stderr, "av_bsf_alloc failed\n");
        return ret;
    }
    if ((ret = avcodec_parameters_copy((*bsf_ctx)->par_in, stream->codecpar)) < 0)
    {
        fprintf(stderr, "avcodec_parameters_copy failed, ret=%d\n", ret);
        return ret;
    }
    if ((ret = av_bsf_init(*bsf_ctx)) < 0)
    {
        fprintf(stderr, "av_bsf_init failed, ret=%d\n", ret);
        return ret;
    }

    return ret;
}

// bsf 过滤器
int Transcode::FilterStream(AVBSFContext *bsf_ctx, AVFormatContext *ofmt_ctx, AVStream *in_stream, AVStream *out_stream, AVPacket *pkt)
{
    int ret = 0;
    if (ret = av_bsf_send_packet(bsf_ctx, pkt) < 0)
    {
        fprintf(stderr, "av_bsf_send_packet failed, ret=%d\n", ret);
        return ret;
    }

    while ((ret = av_bsf_receive_packet(bsf_ctx, pkt) == 0))
    {
        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        if (ret < 0)
            return ret;

        av_packet_unref(pkt);
    }

    if (ret == AVERROR(EAGAIN))
        return 0;

    return ret;
}

int Transcode::Run()
{
    int ret = 0;

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        fprintf(stderr, "Could not allocate AVPacket.");
        return -1;
    }
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate AVFrame.");
        return -1;
    }

    // av_read_frame会将源文件解封装，并将数据放到packet
    // 数据包一般是按dts（解码时间戳）顺序排列的
    while (av_read_frame(inFmtCtx, packet) >= 0)
    {
        // 根据之前的关联关系，判断是否舍弃此packet
        if (streamContextMap[packet->stream_index].outIndex < 0)
        {
            av_packet_unref(packet);
            continue;
        }

        // 需要转编码的轨道
        if (streamContextMap[packet->stream_index].decoderCtx)
        {
            // printf("packet in: %d %ld %ld duration:%ld; time_base: %d\n", packet->stream_index, packet->pts, packet->dts, packet->duration, streamContextMap[packet->stream_index].decoderCtx->time_base.den);
            DecodePacket(packet);
        }
        else // 不需要转编码的轨道
        {
            // 转换timebase（时间基），一般不同的封装格式下，时间基是不一样的
            AVStream *inStream = inFmtCtx->streams[packet->stream_index];
            AVStream *outStream = outFmtCtx->streams[streamContextMap[packet->stream_index].outIndex];

            av_packet_rescale_ts(packet, inStream->time_base, outStream->time_base);

            // 将轨道序号修改为对应的输出文件轨道序号
            packet->stream_index = streamContextMap[packet->stream_index].outIndex;

            // 封装packet，并写入输出文件
            ret = av_interleaved_write_frame(outFmtCtx, packet);
            if (ret < 0)
            {
                fprintf(stderr, "Could not mux packet.  Error: %s\n", my_av_err2str(ret));
            }
        }
        av_packet_unref(packet);
    }

    // 文件读取完成，但是编解码器中的数据未必全部处理完毕
    /* flush decoder */
    av_packet_unref(packet);
    if ((ret = DecodePacket(packet)) < 0)
    {
        fprintf(stderr, "Failed to flush decoder %s\n", my_av_err2str(ret));
    }

    /* flush encoder */
    if ((ret = EncodeWrite(packet, NULL)) < 0)
    {
        fprintf(stderr, "Failed to flush encoder %s\n", my_av_err2str(ret));
    }

    // 写入输出文件尾信息
    ret = av_write_trailer(outFmtCtx);
    if (ret < 0)
    {
        fprintf(stderr, "Could not write the stream trailer to out file. Error: %s\n", my_av_err2str(ret));
    }

    // 关闭输出文件，并销毁具柄
    ret = avio_closep(&outFmtCtx->pb);
    if (ret < 0)
    {
        fprintf(stderr, "Could not close out file. Error: %s\n", my_av_err2str(ret));
    }

    // 关闭输入文件，并销毁具柄
    avformat_close_input(&inFmtCtx);
    fprintf(stderr, "\n\nEND\n\n");
    av_packet_free(&packet);
    av_frame_free(&frame);

    return 0;
}

/*
g++  transcode.cpp -D__STDC_CONSTANT_MACROS -I. -ID:/dev/av/ffmpeg-6.1-full_build-shared/include -LD:/dev/av/ffmpeg-6.1-full_build-shared/lib -lavcodec -lavformat -lswscale -lswresample -lavutil

*/

int main(int argc, char *argv[])
{
    const char *in = "rtsp://admin:qwer1234@172.29.251.10:554/h264/ch33/main/av_stream"; // "D:/input.mp4";  //
    const char *out = "./a.m3u8";

    Transcode tran(in, out);
    int ret = tran.Init();
    printf("init: %d\n", ret);
    tran.Run();
}
