#include <libavutil/log.h>
#include <libavformat/avformat.h>

int main(int argc, char *argv[]){

    if(argc < 2){
        av_log(NULL, AV_LOG_ERROR, "Please input media file url");
        return -1;
    }

    int ret = 0;
    AVFormatContext *pFormatContext = NULL;
    
    av_log_set_level(AV_LOG_INFO);

    // av_register_all();

    ret = avformat_open_input(&pFormatContext, argv[1], NULL, NULL);

    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "avformat open failed %s\n", av_err2str(ret));
        return -1;
    }

    av_dump_format(pFormatContext, 1, argv[1], 0);

    avformat_close_input(&pFormatContext);
}