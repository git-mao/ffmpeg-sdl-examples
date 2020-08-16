#include <stdbool.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>

int main(int argc, char *argv[]){
    char *src = NULL;
    char *dst = NULL;
    FILE *dest_fd = NULL;

    int ret, i;
    int stream_index = 0;
    bool stream_found = false;

    AVPacket packet;
    AVOutputFormat *pOutputFormat = NULL;
    AVFormatContext *pInputFormatContext = NULL, *pOutputFormatContext = NULL;

    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    
    av_log_set_level(AV_LOG_INFO);

    if(argc < 3){
        av_log(NULL, AV_LOG_ERROR, 
        "Please input source media file url and output video file url\n");
        return -1;
    }

    src = argv[1];
    dst = argv[2];

    if(!src || !dst){
        av_log(NULL, AV_LOG_ERROR, "src or dst is NULL\n");
        return -1;
    }

    ret = avformat_open_input(&pInputFormatContext, src, NULL, NULL);

    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "avformat open failed %s\n", av_err2str(ret));
        goto end;
    }

    dest_fd = fopen(dst, "wb");
    if(!dest_fd){
        av_log(NULL, AV_LOG_ERROR, "failed to open dst file\n");
        goto end;
    }

    av_dump_format(pInputFormatContext, 0, src, 0);

    if (avformat_find_stream_info(pInputFormatContext,  NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to find any stream\n");
        goto end;
    }

    avformat_alloc_output_context2(&pOutputFormatContext, NULL, NULL, dst);
    if(!pOutputFormatContext){
        av_log(NULL, AV_LOG_ERROR, "failed to allocate output context\n");
        goto end;
    }

    stream_mapping_size = pInputFormatContext->nb_streams;
    stream_mapping = av_malloc_array(stream_mapping_size, sizeof(*stream_mapping));
    if(!stream_mapping){
        ret = AVERROR(ENOMEM);
        goto end;
    }

    pOutputFormat = pOutputFormatContext->oformat;

    for(int i = 0; i<pInputFormatContext->nb_streams; i++){
        AVStream *out_stream;
        AVStream *in_stream = pInputFormatContext->streams[i];
        AVCodecParameters *in_codecpar = pInputFormatContext->streams[i]->codecpar;
        if(in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE){
           stream_mapping[i] = -1;
           continue;
        }
        
        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(pOutputFormatContext, NULL);
        if(!out_stream){
            av_log(NULL, AV_LOG_ERROR, "failed to allocate out stream\n");
            goto end;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "failed to copy codec parameters\n");
            goto end;
        }

        out_stream->codecpar->codec_tag = 0;
    }

    av_dump_format(pOutputFormatContext, 0, dst, 1);

    if(!(pOutputFormat->flags & AVFMT_NOFILE)){
        ret = avio_open(&pOutputFormatContext->pb, dst, AVIO_FLAG_WRITE);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "failed to open output file\n");
            goto end;
        }
    }

    ret = avformat_write_header(pOutputFormatContext, NULL);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "failed to write header\n");
        goto end;
    }
    
    //write every packet from input stream to output stream
    //and change timebase related for each packet
    while(1){
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(pInputFormatContext, &packet);
        if(ret < 0)
            break;
        
        in_stream = pInputFormatContext->streams[packet.stream_index];
        if(packet.stream_index >= stream_mapping_size ||
            stream_mapping[packet.stream_index]<0){
            av_packet_unref(&packet);
            continue;
        }

        packet.stream_index = stream_mapping[packet.stream_index];
        out_stream = pOutputFormatContext->streams[packet.stream_index];
        
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base,
                                        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base,
                                        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        packet.pos = -1;

        ret = av_interleaved_write_frame(pOutputFormatContext, &packet);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "failed to mux packet\n");
        }

        av_packet_unref(&packet);
    }

    av_write_trailer(pOutputFormatContext);

end:
    if(pInputFormatContext){
        avformat_close_input(&pInputFormatContext);
    }

    if(pOutputFormatContext){
        avformat_close_input(&pOutputFormatContext);
    }

    if(dest_fd){
        fclose(dest_fd);
    }

    return ret;
}