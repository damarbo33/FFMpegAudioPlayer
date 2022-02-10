/**
 * Simplest FFmpeg Audio Player 2 
 *
 * Lei Xiaohua
 * leixiaohua1020@126.com
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * This software decode and play audio streams.
 * Suitable for beginner of FFmpeg.
 *
 * This version use SDL 2.0 instead of SDL 1.2 in version 1
 * Note:The good news for audio is that, with one exception, 
 * it's entirely backwards compatible with 1.2.
 * That one really important exception: The audio callback 
 * does NOT start with a fully initialized buffer anymore. 
 * You must fully write to the buffer in all cases. In this 
 * example it is SDL_memset(stream, 0, len);
 *
 * Version 2.0
 * 
 **********************************************************************************************
 * Daniel Marco
 * 
 * Modifications done to the previous code:
 * - Output frequency and samples set by default with constants. The samples number is computed 
 * to not stress too much the audio device. The previous code, had the samples setted with the 
 * source file samples. Now it doesn't matter the audio format. It will play whitout choppiness.
 * - Corrected error when calling to SDL_OpenAudio in SDL2. Now it works well in SDL1 and SDL2
 * - Implemmented a ring buffer to store the audio in a more convenient way. 
 * - Updated all deprecated ffmpeg code
 * 
 * Version 1.0
 * 
 * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ringbuffer.cpp"

#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswresample/swresample.h>
	#include <SDL2/SDL.h>
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif

//Output PCM
#define OUTPUT_PCM 0
//Use SDL
#define USE_SDL 1

// const int AMPLITUDE = 28000;
//Max of 512KB to store audio conversion buffer from ffmpeg
const int MAX_AUDIO_FRAME_SIZE = 512 * 1024;
/* Sample rate of the device playback*/
const int SAMPLE_RATE = 44100;
/* Minimum SDL audio buffer size, in samples. */
const int SDL_AUDIO_MIN_BUFFER_SIZE = 512;
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
const int SDL_AUDIO_MAX_CALLBACKS_PER_SEC = 30;

struct AudioBuffer {
	
	AudioBuffer(int bufSize){
		out_buffer_size = 0;
		audioRingBuffer = new RingBuffer(bufSize);
		mutex = SDL_CreateMutex();
  		cond = SDL_CreateCond();
	}

	~AudioBuffer(){
		delete audioRingBuffer;
		SDL_DestroyMutex(mutex);
		SDL_DestroyCond(cond);
	}
  	
	SDL_mutex *mutex;	
    SDL_cond *cond;
	int out_buffer_size;
	Uint32  audio_len;
	RingBuffer *audioRingBuffer;
};
	

/* The audio function callback takes the following parameters: 
 * stream: A pointer to the audio buffer to be filled 
 * len: The length (in bytes) of the audio buffer 
*/ 
void  fill_audio(void *udata, Uint8 *stream, int len){ 
	//SDL 2.0
	SDL_memset(stream, 0, len);

	AudioBuffer *audioProps = (AudioBuffer *) udata;
	SDL_LockMutex(audioProps->mutex);
	audioProps->audio_len = audioProps->audioRingBuffer->GetReadAvail();
	
	// printf("len %d audio_len %d\n", len, audio_len);
	if(audioProps->audio_len == 0){		
		/*  Only  play  if  we  have  data  left  */ 
		SDL_CondSignal(audioProps->cond);
		SDL_UnlockMutex(audioProps->mutex);
		return; 
	}
	len = (len > audioProps->audio_len ? audioProps->audio_len : len);	/*  Mix  as  much  data  as  possible  */ 

	audioProps->audioRingBuffer->Read(stream, len);
	//SDL_MixAudio(stream,audio_pos,len,SDL_MIX_MAXVOLUME);
	// audio_pos += len; 
	audioProps->audio_len -= len; 

	//if (audioProps->audio_len <= SDL_AUDIO_MIN_BUFFER_SIZE * 0.5)	
	if (audioProps->audio_len <= audioProps->out_buffer_size * 4)	
		SDL_CondSignal(audioProps->cond);

	SDL_UnlockMutex(audioProps->mutex);
	
	
	// Sint16 *buffer = (Sint16*)stream;
    // int length = len / 2; // 2 bytes per sample for AUDIO_S16SYS
    // //int &sample_nr(*(int*) user_data);
	// static int sample_nr = 0;

    // for(int i = 0; i < length; i++, sample_nr++)
    // {
    //     double time = (double)sample_nr / (double)SAMPLE_RATE;
    //     buffer[i] = (Sint16)(AMPLITUDE * sin(2.0f * M_PI * 441.0f * time)); // render 441 HZ sine wave
    // }
} 
//-----------------

int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx,
							  AVCodec **codec_ctx,
							  AVFormatContext *fmt_ctx, enum AVMediaType type)
{
	int ret, stream_index;
	AVStream *st;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file\n",
				av_get_media_type_string(type));
		return ret;
	} else {
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		*codec_ctx = avcodec_find_decoder(st->codecpar->codec_id);
		if (!*codec_ctx) {
			fprintf(stderr, "Failed to find %s codec\n",
					av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Allocate a codec context for the decoder */
		*dec_ctx = avcodec_alloc_context3(*codec_ctx);
		if (!*dec_ctx) {
			fprintf(stderr, "Failed to allocate the %s codec context\n",
					av_get_media_type_string(type));
			return AVERROR(ENOMEM);
		}

		/* Copy codec parameters from input stream to output codec context */
		if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
			fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
					av_get_media_type_string(type));
			return ret;
		}

		if (type == AVMEDIA_TYPE_AUDIO && (*dec_ctx)->sample_fmt == AV_SAMPLE_FMT_S16P)
			(*dec_ctx)->request_sample_fmt = AV_SAMPLE_FMT_S16;

		/* Init the decoders */
		if ((ret = avcodec_open2(*dec_ctx, *codec_ctx, NULL)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
					av_get_media_type_string(type));
			return ret;
		}
		*stream_idx = stream_index;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	int				audioStreamIdx;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	AVPacket		*packet;
	uint8_t			*out_buffer;
	uint8_t			*convert_buffer;
	AudioBuffer		*audioProps;
	AVFrame			*pFrame;
	SDL_AudioSpec wanted_spec;
	SDL_AudioSpec have_spec;
    int ret;
	int convertSamples;
	uint32_t countReadFrames = 0;
	int index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;
	FILE *pFile=NULL;
	char *url;
	
	if (argc > 1) {
		url = argv[1]; 
	} else {
		printf("usage: simplest_ffmpeg_audio_player.exe file.mp3");
		url = new char[50];
		strcpy(url, "WavinFlag.aac");
		// strcpy(url, "F:\\01 - Busted.flac");
		// strcpy(url, "0012613.mp3");
		// strcpy(url, "ff-16b-1c-22050hz.mp3");
		// strcpy(url, "..\\videojaw2\\big_buck_bunny_720p_30mb.mp4");
	}
	
	//avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	
	//Open
	if(avformat_open_input(&pFormatCtx,url,NULL,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return -1;
	}

	// Retrieve stream information
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return -1;
	}
	// Dump valid information onto standard error
	av_dump_format(pFormatCtx, 0, url, false);

	if (open_codec_context(&audioStreamIdx, &pCodecCtx, &pCodec, pFormatCtx, AVMEDIA_TYPE_AUDIO) < 0){
		printf("Couldn't open context.\n");
		return -1;
	}

	
#if OUTPUT_PCM
	pFile=fopen("output.pcm", "wb");
#endif

	packet = av_packet_alloc();
	//Out Audio Param
	uint64_t out_channel_layout=AV_CH_LAYOUT_STEREO;
	int out_sample_rate = SAMPLE_RATE;
	int out_nb_samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(out_sample_rate / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
	//int out_nb_samples = 512;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	//Out Buffer Size
	audioProps = new AudioBuffer(MAX_AUDIO_FRAME_SIZE);
	audioProps->out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

	out_buffer = (uint8_t *) av_malloc (audioProps->out_buffer_size);
	convert_buffer = (uint8_t *) av_malloc (MAX_AUDIO_FRAME_SIZE);
	
	
	pFrame= av_frame_alloc();

	//FIX:Some Codec's Context Information is missing
	in_channel_layout=(pCodecCtx->channels == av_get_channel_layout_nb_channels(pCodecCtx->channel_layout)) ?
             pCodecCtx->channel_layout :
             av_get_default_channel_layout(pCodecCtx->channels);
			
	// check input audio channels correctly retrieved
	if (in_channel_layout <= 0){
		printf("in_channel_layout error.\n");
		return -1;
	}

	//Swr
	au_convert_ctx = swr_alloc();
	au_convert_ctx=swr_alloc_set_opts(au_convert_ctx,out_channel_layout, out_sample_fmt, out_sample_rate,
	 	in_channel_layout,pCodecCtx->sample_fmt , pCodecCtx->sample_rate,0, NULL);
	swr_init(au_convert_ctx);	

//SDL------------------
#if USE_SDL
	//Init
	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
		printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		return -1;
	}
	//SDL_AudioSpec
	wanted_spec.freq = out_sample_rate; 
	wanted_spec.format = AUDIO_S16SYS; 
	wanted_spec.channels = out_channels; 
	wanted_spec.silence = 0; 
	wanted_spec.samples = out_nb_samples; 
	wanted_spec.callback = fill_audio; 
	wanted_spec.userdata = audioProps; 

	if (SDL_OpenAudio(&wanted_spec, &have_spec) < 0){ 
		printf("can't open audio.\n"); 
		return -1; 
	} 
	
	if(wanted_spec.format != have_spec.format) {
		//Workaround to make work SDL2 audio 
		SDL_CloseAudio();
		if (SDL_OpenAudio(&wanted_spec, NULL) < 0){
			printf("Failed to get the desired AudioSpec\n");
			return -1; 
		}
	}
#endif
	//Play
#if USE_SDL
	SDL_PauseAudio(0);
#endif
	int dst_nb_samples, max_dst_nb_samples, dst_linesize;
	ret = 0;

	while(ret >= 0){
		ret = av_read_frame(pFormatCtx, packet);
		if (ret >= 0 && packet->stream_index != audioStreamIdx) {
             av_packet_unref(packet);
             continue;
        }

		if (ret < 0)
             ret = avcodec_send_packet(pCodecCtx, NULL);
         else {
             if (packet->pts == AV_NOPTS_VALUE)
                 packet->pts = packet->dts = countReadFrames;
             ret = avcodec_send_packet(pCodecCtx, packet);
         }
  
         if (ret < 0) {
             av_log(NULL, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
             break;
         }

		while (ret >= 0){
			ret = avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret == AVERROR_EOF){
				ret = -1;
				break;
			} else if (ret == AVERROR(EAGAIN)) {
				ret = 0;
				break;
			} else if ( ret < 0 ) {
				ret = 0;
				printf("Error decoding frame. index:%5d\t pts:%lld\t packet size:%d\n",index,packet->pts,packet->size);
				break;
			}

			convertSamples = swr_convert(au_convert_ctx, &convert_buffer, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pFrame->data , pFrame->nb_samples);
			if (convertSamples < 0){
			  	printf("Error converting samples into buffer\n");
			  	ret = -1;
			}
			
			//printf("samples: %d\n", convertSamples);
			int bytesFileInputBuffer = av_samples_get_buffer_size(NULL, out_channels, convertSamples, out_sample_fmt, 1);

			// printf("index:%5d pts:%lld packet size:%d stream samples: %d in samples: %d wanted samples: %d",
			// 	index,packet->pts,packet->size, convertSamples, pFrame->nb_samples, wanted_spec.samples);
			// printf(" bytes In: %d, bytes out: %d\n", bytesFileInputBuffer, out_buffer_size);
			index++;
			//Need of lock reads and writes?? it seems not
			SDL_LockMutex(audioProps->mutex);			
			audioProps->audioRingBuffer->Write(convert_buffer, bytesFileInputBuffer);
			//printf("audioRingBuffer->GetReadAvail(): %d\n",audioProps.audioRingBuffer->GetReadAvail());
			if (audioProps->audioRingBuffer->GetReadAvail() < audioProps->out_buffer_size * 8){
			//if (audioProps.audioRingBuffer->GetReadAvail() < MAX_AUDIO_FRAME_SIZE){
				av_frame_unref(pFrame);
				// printf("Reading into bufferring\n");
				SDL_UnlockMutex(audioProps->mutex);
				break;
			} 
#if OUTPUT_PCM
				//Write PCM
				fwrite(out_buffer, 1, out_buffer_size, pFile);
#endif
			av_frame_unref(pFrame);
			//SDL_LockMutex(audioProps.mutex);
			//printf("Waiting buffer empty...\n");
			SDL_CondWaitTimeout(audioProps->cond, audioProps->mutex, 5000);
			SDL_UnlockMutex(audioProps->mutex);
			//printf("Frame %d, fetching data...\n", index);
		}
		av_packet_unref(packet);
		countReadFrames++;
	}
	swr_free(&au_convert_ctx);

#if USE_SDL
	SDL_CloseAudio();//Close SDL
	SDL_Quit();
#endif
	// Close file
#if OUTPUT_PCM
	fclose(pFile);
#endif
	av_free(out_buffer);
	av_free(convert_buffer);
	delete audioProps;
	// Close the codec
	avcodec_close(pCodecCtx);
	// Close the video file
	avformat_close_input(&pFormatCtx);
	return 0;
}


