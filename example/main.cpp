#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <list>

#include "nat.h"


static long GetTime() {
    struct timeval time_;
    memset(&time_, 0, sizeof(struct timeval));

    gettimeofday(&time_, NULL);
    return time_.tv_sec*1000 + time_.tv_usec/1000;
}

#ifdef FFMPEG_ENABLE

#ifdef __cplusplus
extern "C" {
#endif

#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/intreadwrite.h>

#ifdef __cplusplus
}
#endif

const int ADTS_HEADER_LEN = 7;
 
const int sampling_frequencies[] = {
    96000,  // 0x0
    88200,  // 0x1
    64000,  // 0x2
    48000,  // 0x3
    44100,  // 0x4
    32000,  // 0x5
    24000,  // 0x6
    22050,  // 0x7
    16000,  // 0x8
    12000,  // 0x9
    11025,  // 0xa
    8000   // 0xb
    // 0xc d e f是保留的
};
 
int adts_header(char* const p_adts_header, const int data_length,
    const int profile, const int samplerate,
    const int channels)
{
    int sampling_frequency_index = 3; //默认48000HZ
    int adtsLen = data_length + ADTS_HEADER_LEN;
    //获取采样率最大索引数以便后续遍历
    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for (i = 0; i < frequencies_size; i++)
    {
        if (sampling_frequencies[i] == samplerate)
        {
            sampling_frequency_index = i;
            break;
        }
    }
    if (i >= frequencies_size)
    {
		printf("unsupport samplerate: %d\n", samplerate);
        return -1;
    }
    //根据传入参数配置adts header
    p_adts_header[0] = 0xff;         //syncword:0xfff                          高8bits
    p_adts_header[1] = 0xf0;         //syncword:0xfff                          低4bits
    p_adts_header[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    p_adts_header[1] |= (0 << 1);    //Layer:0                                 2bits
    p_adts_header[1] |= 1;           //protection absent:1                     1bit
 
    p_adts_header[2] = (profile) << 6;            //profile:profile               2bits
    p_adts_header[2] |= (sampling_frequency_index & 0x0f) << 2; //sampling frequency index:sampling_frequency_index  4bits
    p_adts_header[2] |= (0 << 1);             //private bit:0                   1bit
    p_adts_header[2] |= (channels & 0x04) >> 2; //channel configuration:channels  高1bit
 
    p_adts_header[3] = (channels & 0x03) << 6; //channel configuration:channels 低2bits
    p_adts_header[3] |= (0 << 5);               //original：0                1bit
    p_adts_header[3] |= (0 << 4);               //home：0                    1bit
    p_adts_header[3] |= (0 << 3);               //copyright id bit：0        1bit
    p_adts_header[3] |= (0 << 2);               //copyright id start：0      1bit
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits
 
    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    p_adts_header[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    p_adts_header[6] = 0xfc;      //11111100       //buffer fullness:0x7ff 低6bits
    // number_of_raw_data_blocks_in_frame：
    //    表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧。
 
    return 0;
 
}

static void FfmpegPushRecord(const char* file) {
    // 初始化 FFmpeg 库
    avdevice_register_all();
    avformat_network_init();

    // 打开输入文件并获取格式上下文
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, file, NULL, NULL) < 0) {
        printf("Could not open input file %s\n", file);
        return ;
    }

    // 获取流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        printf("Could not find stream information\n");
        return ;
    }

    // 找到视频流和音频流的索引
    int video_stream_index = -1;
    int aac_audio_stream_index = -1;
    int g711a_audio_stream_index = -1;
    AVCodecParameters *video_codecpar = NULL;
    AVCodecParameters *aac_audio_codecpar = NULL;
    AVCodecParameters *g711a_audio_codecpar = NULL;

    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVCodecParameters *codecpar = fmt_ctx->streams[i]->codecpar;

        // 查找 H264 视频流
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && codecpar->codec_id == AV_CODEC_ID_H264) {
            video_stream_index = i;
            video_codecpar = codecpar;
        }

        // 查找 AAC 音频流
        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && codecpar->codec_id == AV_CODEC_ID_AAC) {
            aac_audio_stream_index = i;
            aac_audio_codecpar = codecpar;
        }

        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && codecpar->codec_id == AV_CODEC_ID_PCM_ALAW) {
            g711a_audio_stream_index = i;
            g711a_audio_codecpar = codecpar;
        }
    }

    if (video_stream_index == -1) {
        printf("H264 video stream not found\n");
        return ;
    }

    if (aac_audio_stream_index == -1 && g711a_audio_stream_index == -1) {
        printf("AAC/G711A audio stream not found\n");
        return ;
    }

    // 解码器上下文
    AVCodecContext *video_codec_ctx = avcodec_alloc_context3(NULL);
    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(NULL);

    // 打开视频解码器
    const AVCodec *video_codec = avcodec_find_decoder(video_codecpar->codec_id);
    if (!video_codec || avcodec_open2(video_codec_ctx, video_codec, NULL) < 0) {
        printf("Failed to open video codec\n");
        return ;
    }

    // 打开音频解码器
    const AVCodec *audio_codec = NULL;
	if (aac_audio_stream_index != -1) {
		audio_codec = avcodec_find_decoder(aac_audio_codecpar->codec_id);
		if (!audio_codec || avcodec_open2(audio_codec_ctx, audio_codec, NULL) < 0) {
			printf("Failed to open audio codec\n");
			return ;
		}
	}

    AVPacket packet;
    av_init_packet(&packet);

	const AVBitStreamFilter* bsf_filter = av_bsf_get_by_name("h264_mp4toannexb");
	AVBSFContext* bsf_ctx = NULL;
	av_bsf_alloc(bsf_filter, &bsf_ctx);

	avcodec_parameters_copy(bsf_ctx->par_in, fmt_ctx->streams[video_stream_index]->codecpar);
	av_bsf_init(bsf_ctx);

	// 读取并处理数据包
	while (av_read_frame(fmt_ctx, &packet) >= 0) {
		if (packet.stream_index == video_stream_index) {
			long cur_time = GetTime();

			if (av_bsf_send_packet(bsf_ctx, &packet) != 0) 
			{
				av_packet_unref(&packet);
				continue;
			}
			av_packet_unref(&packet);

			while(av_bsf_receive_packet(bsf_ctx, &packet) == 0)
			{
				NatPushStreamInfo info = {0};
				info.chn = 0;
				info.frame_type = NAT_FRAME_VIDEO;
				info.buff = packet.data + 6;
				info.size = packet.size - 6;
				info.pts = packet.pts;
				NatPushStream(&info);

				av_packet_unref(&packet);
			}
			
			while(cur_time + 33 > GetTime()) {
				usleep(1*1000);
			}

		} else if (packet.stream_index == aac_audio_stream_index) {
			// 提取 AAC 音频数据
			char pkt[1024] = {0};
			adts_header(pkt, packet.size, fmt_ctx->streams[aac_audio_stream_index]->codecpar->profile, 
				fmt_ctx->streams[aac_audio_stream_index]->codecpar->sample_rate,fmt_ctx->streams[aac_audio_stream_index]->codecpar->ch_layout.nb_channels);
			memcpy(pkt+7, packet.data, packet.size);

			NatPushStreamInfo info = {0};
			info.chn = 0;
			info.frame_type = NAT_FRAME_AUDIO;
			info.buff = (unsigned char*)pkt;
			info.size = packet.size + 7;
			info.pts = packet.pts;
			NatPushStream(&info);

			av_packet_unref(&packet);  // 清理包数据
		} else if (packet.stream_index == g711a_audio_stream_index) {
			// long cur_time = GetTime();
			// NatPushStreamInfo info = {0};
			// info.chn = -1;
			// info.frame_type = RTSP_SERVER_FRAME_AUDIO;
			// info.stream_type = RTSP_SERVER_STREAMING_MAIN;
			// info.buff = packet.data;
			// info.size = packet.size;
			// info.pts = packet.pts;
			// RtspServerPushStream(&info);

			// av_packet_unref(&packet);  // 清理包数据
			// while(cur_time + 67 > GetTime()) {
			// 	usleep(1*1000);
			// }
		}
	}

end:
    // 清理
    avcodec_free_context(&video_codec_ctx);
    avcodec_free_context(&audio_codec_ctx);
    avformat_close_input(&fmt_ctx);
}

static void* FfmpegProc(void* arg) {
	pthread_setname_np(pthread_self(), __func__);
	while (1) {
		FfmpegPushRecord("./test.mp4");
	}
	
}

#endif

typedef struct {
    int size;
    unsigned char* data;
}__Frame;

static unsigned char* media;
static std::list<__Frame> media_list;

static unsigned char* FindFrame(const unsigned char* buff, int len, int* size) {
	unsigned char *s = NULL;
	while (len >= 3) {
		if (buff[0] == 0 && buff[1] == 0 && buff[2] == 1 && (buff[3] == 0x41 || buff[3] == 0x67 || buff[3] == 0x40 || buff[3] == 0x02)) {
			if (!s) {
				if (len < 4)
					return NULL;
				s = (unsigned char *)buff;
			} else {
				*size = (buff - s);
				return s;
			}
			buff += 3;
			len  -= 3;
			continue;
		}
		if (len >= 4 && buff[0] == 0 && buff[1] == 0 && buff[2] == 0 && buff[3] == 1 && (buff[4] == 0x41 || buff[4] == 0x67 || buff[4] == 0x40 || buff[4] == 0x02)) {
			if (!s) {
				if (len < 5)
					return NULL;
				s = (unsigned char *)buff;
			} else {
				*size = (buff - s);
				return s;
			}
			buff += 4;
			len  -= 4;
			continue;
		}
		buff ++;
		len --;
	}
	if (!s)
		return NULL;
	*size = (buff - s + len);
	return s;
}

static void* VideoChn1Push(void* arg) {
    while(1) {
        for(__Frame frame : media_list) {
			long cur_time = GetTime();

			NatPushStreamInfo info = {0};
			info.chn = 0;
			info.frame_type = NAT_FRAME_VIDEO;
			info.buff = frame.data;
			info.size = frame.size;
			info.pts = -1;
            NatPushStream(&info);

			while(cur_time + 40 > GetTime()) {
            	usleep(1*1000);
			}
        }
    }
}

static int VideoChn1Init() {
    FILE* fp = fopen("big_bit_video.h264", "r");
    // FILE* fp = fopen("recordfile01.h264", "r");
    if (fp == NULL) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    media = (unsigned char*)malloc(size+1);
    if (media == NULL) {
        fclose(fp);
        return -1;
    }
    memset(media, 0, size+1);
    int ret = fread(media, 1, size, fp);
    if (ret != size) {
        printf("fread fail ,size:%d-%d\n", ret, size);
        fclose(fp);
        free(media);
        return -1;
    }

    fclose(fp);

    int start = 0;
    while(start < size) {
        __Frame frame;
        frame.data = FindFrame(media+start, size-start, &frame.size);
        if (frame.data == NULL) {
            break;
        }

        media_list.push_back(frame);
        start = frame.data - media + frame.size;
    }

    printf("media size:%d len:%d\n", media_list.size(), size);

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, VideoChn1Push, NULL);
    return 0;
}


int main() {
	NatInit(NULL);

	NatStreamingRegisterInfo info[] = {
		// {
		// 	.chn = 0, 
		// 	.video_info = {.use = 1, .video_type = NAT_VIDEO_H264, .fps = 30}, 
		// 	.audio_info = {.use = 1, .audio_type = NAT_AUDIO_AAC, .sample_rate = 16000, .channels = 2}
		// },
		{
			.chn = 0, 
			.video_info = {.use = 1, .video_type = NAT_VIDEO_H264, .fps = 25}
		}
	};
	NatStreamingRegister(info, sizeof(info)/sizeof(NatStreamingRegisterInfo));

#ifdef FFMPEG_ENABLE
	pthread_t thread_id;
	// pthread_create(&thread_id, NULL, FfmpegProc, NULL);
#endif

	VideoChn1Init();

	while (1) {
		sleep(1);
	}
	
}