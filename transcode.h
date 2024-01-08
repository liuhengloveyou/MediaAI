#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libavfilter/avfilter.h>
#include <libavcodec/bsf.h>
#include "libswscale/swscale.h"

#ifdef __cplusplus
}
#endif

// 轨道上下文结构体，存放解码器、编码器、输出轨道序号、轨道类型等
typedef struct StreamContext
{
    AVMediaType codec_type;     // 轨道类型
    unsigned int outIndex;      // 输出轨道序号
    AVCodecContext *decoderCtx; // 解码器
    AVCodecContext *encoderCtx; // 编码器
} StreamContext;

class Transcode
{
public:
    Transcode() = delete;
    Transcode(const char *in, const char *out);
    ~Transcode();

public:
    int Init(); // 初始化
    int Run();  // 开始处理数据

private:
    int OpenInStream();                                // 打开输入流
    int OpenOutStream();                               // 打开输出流
    int OpenEncoder();                                 // 打开编码器
    int DecodePacket(AVPacket *packet);                // 解码
    int EncodeWrite(AVPacket *packet, AVFrame *frame); // 编码
    int TransCodePkt(AVPacket *packet, AVFrame *frame);
    int OpenBitstreamFilter(AVStream *stream, AVBSFContext **bsf_ctx, const char *name);
    int FilterStream(AVBSFContext *bsf_ctx, AVFormatContext *ofmt_ctx, AVStream *in_stream, AVStream *out_stream, AVPacket *pkt);

private:
    // 输入输出文件地址
    const char *inUrl = nullptr;
    const char *outUrl = nullptr;

    // 输入输出文件句柄
    AVFormatContext *inFmtCtx = nullptr;
    AVFormatContext *outFmtCtx = nullptr;

    // 编码码上下文
    AVBufferRef *hwDeviceCtx = nullptr;

    // 轨道上下文关联表
    StreamContext *streamContextMap = nullptr;
    int streamContextLength = 0;
    int video_stream_index = -1;
    bool is_annexb = false;
};
