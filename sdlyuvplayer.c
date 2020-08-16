#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>

#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define QUIT_EVENT (SDL_USEREVENT + 2)
#define DELAY 16

int thread_exit = 0;

int refresh_video_timer(void *udata){
    thread_exit = 0;

    while(!thread_exit){
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(DELAY);
    }

    thread_exit = 0;

    SDL_Event event;
    event.type = QUIT_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

int main(int argc, char *argv[]){
    FILE *video_fd = NULL; 

    SDL_Window *pWindow = NULL;
    SDL_Renderer *pRenderer = NULL;
    SDL_Texture *pTexture = NULL;

    Uint32 pixformat = 0;

    SDL_Event event;
    SDL_Rect rect;

    SDL_Thread *pTimer_thread = NULL;

    int w_width = 480, w_height = 272;
    int video_width = 480, video_height = 272;

    Uint8 *video_pos = NULL;
    Uint8 *video_end = NULL;

    unsigned int remain_len = 0;
    size_t video_buff_len = 0;
    size_t blank_space_len = 0;
    Uint8 *video_buf = NULL;

    const char *path = "1.yuv";

    const unsigned int yuv_frame_len = video_width * video_height * 12 / 8;
    unsigned int tmp_yuv_frame_len = yuv_frame_len;

    if(yuv_frame_len & 0xF){
        tmp_yuv_frame_len = (yuv_frame_len & 0xFFF0) + 0x10;
    }

    SDL_Init(SDL_INIT_VIDEO);
    pWindow = SDL_CreateWindow("YUV Player", 
                                SDL_WINDOWPOS_UNDEFINED, 
                                SDL_WINDOWPOS_UNDEFINED, 
                                w_width, w_height, 
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if(!pWindow){
        printf("failed to create window!");
        goto __FAIL;
    }

    pRenderer = SDL_CreateRenderer(pWindow, -1, 0);

    if(!pRenderer){
        printf("failed to create renderer");
        goto __FAIL;
    }

    pixformat = SDL_PIXELFORMAT_IYUV;

    pTexture = SDL_CreateTexture(pRenderer, 
                                pixformat,
                                SDL_TEXTUREACCESS_STREAMING, 
                                video_width, 
                                video_height);

    if(!pTexture){
        printf("failed to create Texture");
        goto __FAIL;
    }

    video_buf = (Uint8 *)malloc(tmp_yuv_frame_len);
    memset(video_buf, 0, tmp_yuv_frame_len);
    
    if(!video_buf){
        fprintf(stderr, "failed to allocate yuv frame buf\n");
        goto __FAIL;
    }

    video_fd = fopen(path, "r");
    if(!video_fd){
        fprintf(stderr, "failed to open yuv file\n");
        goto __FAIL;
    }

    video_pos = video_buf;

    pTimer_thread = SDL_CreateThread(refresh_video_timer,
                                    NULL,
                                    NULL);

    do{
        SDL_WaitEvent(&event);
        if(event.type == REFRESH_EVENT){
            SDL_UpdateTexture(pTexture,
                                NULL,
                                video_pos,
                                video_width);
        

            rect.x = 0;
            rect.y = 0;
            rect.w = w_width;
            rect.h = w_height;

            SDL_RenderCopy(pRenderer, pTexture, NULL, &rect);
            SDL_RenderPresent(pRenderer);

            if((video_buff_len = fread(video_buf,
                                        1,
                                        yuv_frame_len,
                                        video_fd))<=0){
                //a mutex should be added here.
                thread_exit = 1;
            }
        }else if(event.type == SDL_WINDOWEVENT){
            SDL_GetWindowSize(pWindow, &w_width, &w_height);
        }else if(event.type == SDL_QUIT){
            thread_exit = 1;
        }else if(event.type == QUIT_EVENT){
            break;
        }
    }while(1);

__FAIL:
    if(video_buf){
        free(video_buf);
    }

    if(video_fd){
        fclose(video_fd);
    }

    if(pTexture){
        SDL_DestroyTexture(pTexture);
    }

    if(pRenderer){
        SDL_DestroyRenderer(pRenderer);
    }

    if(pWindow){
        SDL_DestroyWindow(pWindow);
    }

    SDL_Quit();
    return 0;
}