#include <stdbool.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#ifndef AV_WB32
#define AV_WB32(p, val)                  \
do {                                     \
    uint32_t d = (val);                  \
    ((uint8_t *)(p))[3] = (d);           \
    ((uint8_t *)(p))[2] = (d)>>8;        \
    ((uint8_t *)(p))[1] = (d)>>16;       \
    ((uint8_t *)(p))[0] = (d)>>24;       \
}while(0)
#endif

#ifndef AV_RB16
#define AV_RB16(x)                       \
    ((((const uint8_t *)(x))[0] << 8) |   \
    ((const uint8_t *)(x))[1])
#endif

//add start code
static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size){
    uint32_t offset = out->size;
    uint8_t nal_header_size = offset ? 3 : 4;
    int ret;

    ret = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if(ret < 0){
        return ret;
    }

    if(sps_pps){
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    }

    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);

    if(!offset){
        //set 1 byte after sps pps to "00000001" as start code
        AV_WB32(out->data + sps_pps_size, 1);
    }else{
        (out->data + offset + sps_pps_size)[0] = 0;
        (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

//add sps pps
int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding){
    uint16_t unit_size;
    uint64_t total_size = 0;
    uint8_t *out = NULL, unit_nb, sps_done = 0, sps_seen = 0, pps_seen = 0,
            sps_offset = 0, pps_offset = 0;
    
    //in extra data, first 4 bytes not used
    const uint8_t *extradata = codec_extradata + 4;
    static const uint8_t nalu_header[4] = {0, 0, 0, 1};
    //sps pps length
    int length_size = (*extradata++ & 0x3) + 1;

    sps_offset = pps_offset = -1;

    //retrieve sps, pps unit(s)
    unit_nb = *extradata++ & 0x1f;

    if(!unit_nb){
        goto pps;
    }else{
        sps_offset = 0;
        sps_seen = 1;
    }

//traverse each sps/pps
    while(unit_nb--){
        int err;
        
        //this macro reads 2 bytes as sps pps unit length
        unit_size = AV_RB16(extradata);
        //add 4 for start code
        total_size += unit_size + 4;
        if(total_size > INT_MAX - padding){
            av_log(NULL, AV_LOG_ERROR, "too big extradata size, corrupted or invalid stream");
            av_free(out);
            return AVERROR(EINVAL);
        }

        if(extradata + 2 + unit_size > codec_extradata + codec_extradata_size){
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata");
            av_free(out);
            return AVERROR(EINVAL);
        }

        if((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        //start code
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        //sps pps data
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;

pps:
        if(!unit_nb && !sps_done++) {
            unit_nb = *extradata++;
            if(unit_nb){
                pps_offset = total_size;
                pps_seen = 1;
            }
        }
    }

    if(out){
        memset(out + total_size, 0, padding);
    }

    if(!sps_seen){
        av_log(NULL, AV_LOG_WARNING, "Warning: SPS missing");
    }

    if(!pps_seen){
        av_log(NULL, AV_LOG_WARNING, "Warning: PPS missing");
    }

    out_extradata->data = out;
    out_extradata->size = total_size;

    return length_size;
}
int h264_mp4toannexb(AVFormatContext *pFormatContext, AVPacket *in, FILE *dest_fd){
    AVPacket *out = NULL;
    AVPacket sps_pps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf = in->data;
    buf_size = in->size;
    buf_end = in->data + buf_size;

    do{
        ret = AVERROR(EINVAL);
        if(buf + 4 > buf_end)
            goto fail;
        
        for(nal_size = 0, i = 0; i < 4; i++)
            nal_size = (nal_size << 8) | buf[i];
        
        buf += 4;
        unit_type = *buf & 0x1f;

        if(nal_size > buf_end - buf || nal_size < 0)
            goto fail;

        if(unit_type == 5){
            h264_extradata_to_annexb(pFormatContext->streams[in->stream_index]->codec->extradata,
                                     pFormatContext->streams[in->stream_index]->codec->extradata_size,
                                     &sps_pps_pkt,
                                     AV_INPUT_BUFFER_PADDING_SIZE);
            
            if((ret = alloc_and_copy(out, sps_pps_pkt.data, sps_pps_pkt.size, buf, nal_size)) < 0){
                goto fail;
            }
        }else{
            if((ret = alloc_and_copy(out, NULL, 0, buf, nal_size))<0){
                goto fail;
            }
        }

        len = fwrite(out->data, 1, out->size, dest_fd);
        if(len != out->size){
            av_log(NULL, AV_LOG_WARNING, "warning, length of writed data not equal to packet size");
        }

        fflush(dest_fd);
next_nal:
        buf += nal_size;
        cumul_size += nal_size + 4; 
    }while(cumul_size < buf_size);
fail:
    av_packet_free(&out);

    return ret;
}

int main(int argc, char *argv[]){
    int ret = 0;
    char errors[1024];

    char *src = NULL;
    char *dst = NULL;
    FILE *dst_fd = NULL;

    int len = 0;
    int video_stream_index = -1;
    bool video_stream_found = false;

    AVPacket *pPacket = NULL;
    AVCodecParameters *pCodecParameters = NULL;
    AVFormatContext *pFormatContext = NULL;
    
    av_log_set_level(AV_LOG_INFO);

    if(argc < 3){
        av_log(NULL, AV_LOG_ERROR, 
        "Please input source media file url and output video file url");
        return -1;
    }

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

    dst_fd = fopen(dst, "wb");
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
        if(pCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO){
            //if first audio stream found, break and use it
            video_stream_index = i;
            video_stream_found = true;
            break;
        }
    }

    if(!video_stream_found){
        av_log(NULL, AV_LOG_ERROR, "no video stream found from input media file!");
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
    pPacket->data = NULL;
    pPacket->size = 0;

    // av_log(NULL, AV_LOG_INFO, "before read frame");

    while(av_read_frame(pFormatContext, pPacket) >= 0){
        // av_log(NULL, AV_LOG_INFO, "stream index is %d\n", pPacket->stream_index);
        if(pPacket->stream_index == video_stream_index){
            //set start code and sps/pps here
            h264_mp4toannexb(pFormatContext, pPacket, dst_fd);
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

    return ret;
}