#include <stdbool.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>

void adts_header(char *szAdtsHeader, int dataLen){
    int audio_object_type = 2;
    int sampling_frequency_index = 4;
    int channel_config = 2;

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;
    szAdtsHeader[1] = 0xf0;
    szAdtsHeader[1] |= (0 << 3);
    szAdtsHeader[1] |= (0 << 1);
    szAdtsHeader[1] |= 1;

    szAdtsHeader[2] = (audio_object_type - 1) << 6;
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)<<2;
    szAdtsHeader[2] |= (0<<1);
    szAdtsHeader[2] |= (channel_config & 0x04)>>2;

    szAdtsHeader[3] = (channel_config & 0x03)<<6;
    szAdtsHeader[3] |= (0 << 5);
    szAdtsHeader[3] |= (0 << 4);
    szAdtsHeader[3] |= (0 << 3);
    szAdtsHeader[3] |= (0 << 2);
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);

    szAdtsHeader[4] =(uint8_t)((adtsLen & 0x7f8) >> 3);
    szAdtsHeader[5] =(uint8_t)((adtsLen & 0x7) << 5);
    szAdtsHeader[5] |= 0x1f;
    szAdtsHeader[6] = 0xfc;
}

int main(int argc, char *argv[]){
    int ret = 0;
    char *src = NULL;
    char *dst = NULL;

    AVPacket *pPacket = NULL; 
    AVCodecParameters *pCodecParameters = NULL;
    int audio_index = 0;
    int len = 0;
    bool audio_stream_found = false;

    if(argc < 3){
        av_log(NULL, AV_LOG_ERROR, 
        "Please input source media file url and output audio file url");
        return -1;
    }

    AVFormatContext *pFormatContext = NULL;
    
    av_log_set_level(AV_LOG_INFO);

    // av_register_all();

    src = argv[1];
    dst = argv[2];

    if(!src || !dst){
        av_log(NULL, AV_LOG_ERROR, "src or dst is NULL");
        return -1;
    }

    ret = avformat_open_input(&pFormatContext, src, NULL, NULL);

    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "avformat open failed %s\n", av_err2str(ret));
        return -1;
    }

    FILE *dst_fd = fopen(dst, "wb");
    if(!dst_fd){
        av_log(NULL, AV_LOG_ERROR, "failed to open dst file\n");
        goto __FAIL;
    }

    av_dump_format(pFormatContext, 1, argv[1], 0);

    if (avformat_find_stream_info(pFormatContext,  NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to find any stream");
        return -1;
    }

    for(int i = 0; i<pFormatContext->nb_streams; i++){
        pCodecParameters = pFormatContext->streams[i]->codecpar;
        if(pCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO){
            //if first audio stream found, break and use it
            audio_index = i;
            audio_stream_found = true;
            break;
        }
    }

    av_log(NULL, AV_LOG_INFO, "audio index is %d\n", audio_index);

    if(!audio_stream_found){
        av_log(NULL, AV_LOG_ERROR, "no audio stream found from input media file!");
        goto __FAIL;
    }

    // //get stream
    // ret = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0 );
    // if(ret < 0){
    //     av_log(NULL, AV_LOG_ERROR, "cannot find best audio stream\n");
    //     goto __FAIL;
    // }
    // audio_index = ret;

    pPacket = av_packet_alloc();
    av_init_packet(pPacket);

    // av_log(NULL, AV_LOG_INFO, "before read frame");

    while(av_read_frame(pFormatContext,pPacket) >= 0){
        // av_log(NULL, AV_LOG_INFO, "stream index is %d\n", pPacket->stream_index);
        if(pPacket->stream_index == audio_index){
            //add adts header for aac
            char adts_header_buf[7];
            adts_header(adts_header_buf, pPacket->size);
            fwrite(adts_header_buf, 1, 7, dst_fd);

            len = fwrite(pPacket->data, 1, pPacket->size, dst_fd);
            // av_log(NULL, AV_LOG_INFO, "pPacket length is %d", len);
            if(len != pPacket->size){
                av_log(NULL, AV_LOG_WARNING, "len not equal to packet size");
            }
        }

        av_packet_unref(pPacket);
    }

__FAIL:
    if(pFormatContext){
        avformat_close_input(&pFormatContext);
    }

    if(dst_fd){
        fclose(dst_fd);
    }

    if(pPacket){
        av_packet_free(&pPacket);
    }
}