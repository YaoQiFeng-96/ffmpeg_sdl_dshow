// call_usb_camera.cpp : 定义控制台应用程序的入口点。
//
#include <cstdio>
#include <iostream>
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

#define __STDC_CONSTANT_MACROS
#define SDL_MAIN_HANDLED

extern "C"
{
#include "libavformat\avformat.h"
#include "libavcodec/avcodec.h"
#include "libavdevice\avdevice.h"
#include "libswscale\swscale.h"
#include "libavutil\imgutils.h"
#include "SDL2\SDL.h"
};

int thread_exit = 0;
int thread_pause = 0;
int sfp_refresh_thread(void *opaque) 
{
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) 
	{
		if (!thread_pause) 
		{
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(1000/30);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);
	return 0;
}

int main()
{
	avformat_network_init();
	avdevice_register_all();

	AVFormatContext		*pFormatCtx;
	AVInputFormat		*iformat;
	AVCodecContext		*pCodecCtx;
	AVCodec				*pCodec;
	AVFrame				*pFrame, *pFrameYUV;
	AVPacket			*packet;
	unsigned char*		out_buffer;
	int					videoindex;
	//--------------SDL------------
	int					screen_w, screen_h;
	SDL_Window			*screen;
	SDL_Renderer		*sdlRenderer;
	SDL_Texture			*sdlTexture;
	SDL_Rect			sdlRect;
	SDL_Thread			*video_tid;
	SDL_Event			event;
	struct SwsContext	*img_convert_ctx;

	pFormatCtx = avformat_alloc_context();
	iformat = av_find_input_format("dshow");
	if (avformat_open_input(&pFormatCtx, "video=PC Camera", iformat, NULL) != 0)
	{
		std::cout << "avforamt open input failed." << std::endl;
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		std::cout << "avformat find stream info failed." << std::endl;
		return -1;
	}
	av_dump_format(pFormatCtx, 0, "PC Camera", 0);
	videoindex = -1;
	for (auto i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}
	if (-1 == videoindex)
	{
		std::cout << "video stream not found." << std::endl;
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(NULL);
	if (pCodecCtx == NULL)
	{
		std::cout << "AVCodecContext allocate failed." << std::endl;
		return -1;
	}
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		std::cout << "AVCodec not found." << std::endl;
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		std::cout << "codec open failed." << std::endl;
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	packet = av_packet_alloc();
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1));
	
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
	
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);

	if (!screen) 
	{
		std::cout << "SDL: could not create window - exiting: " << SDL_GetError() << std::endl;
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_SOFTWARE);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
	int ret, got_picture;
	for (;;)
	{
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT)
		{
			while (true)
			{
				if (av_read_frame(pFormatCtx, packet) < 0)
					thread_exit = 1;
				if (packet->stream_index == videoindex)
					break;
			}
			ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret < 0)
			{
				std::cout << "decode error." << std::endl;
				return -1;
			}
			got_picture = avcodec_receive_frame(pCodecCtx, pFrame);
			if (!got_picture)
			{
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
					pFrameYUV->data[0], pFrameYUV->linesize[0],
					pFrameYUV->data[1], pFrameYUV->linesize[1],
					pFrameYUV->data[2], pFrameYUV->linesize[2]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
			}
			av_packet_unref(packet);
		}
		else if (event.type == SDL_KEYDOWN)
		{
			if (event.key.keysym.sym == SDLK_SPACE)
				thread_pause = !thread_pause;
		}
		else if (event.type == SDL_QUIT)
		{
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT)
		{
			break;
		}
	}

	sws_freeContext(img_convert_ctx);
	if (screen)
		SDL_DestroyWindow(screen);
	if (sdlRenderer)
		SDL_DestroyRenderer(sdlRenderer);
	if (sdlTexture)
		SDL_DestroyTexture(sdlTexture);
	SDL_Quit();
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
	av_packet_free(&packet);
	avcodec_close(pCodecCtx);
	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFormatCtx);
    return 0;
}

