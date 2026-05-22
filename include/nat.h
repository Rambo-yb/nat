#ifndef __NAT_H__
#define __NAT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char* log_path;			///< 日志路径, 【NULL:"/data/logs"】
	const char* conf_path;			///< 配置路径, 【NULL:"/data/confs"】
}NatInitialInfo;
/**
 * @brief NAT初始化
 * @param [in] info NAT初始化需要的初始信息
 * @return 成功返回 0
 *         失败返回 其他值
*/
int NatInit(NatInitialInfo* info);

/**
 * @brief NAT反初始化
*/
void NatUnInit();

typedef enum {
	NAT_VIDEO_H264,
	NAT_VIDEO_H265,
}NatVideoType;

typedef struct {
	int use;					///< 信息是否使用
	unsigned int video_type;	///< 传输视频格式, NatVideoType
	unsigned int fps;			///< 帧率
}NatVideoInfo;

typedef enum {
	NAT_AUDIO_AAC,
	NAT_AUDIO_G711A,
}NatAudioType;

typedef struct {
	int use;					///< 信息是否使用
	unsigned int audio_type;	///< NatAudioType
	unsigned int sample_rate;	///< 采样率
	unsigned int channels;		///< 通道数, 单通道/双通道
}NatAudioInfo;

typedef struct {
	int chn;						///< 音视频通道
	NatVideoInfo video_info;		///< 视频信息
	NatAudioInfo audio_info;		///< 音频信息
}NatStreamingRegisterInfo;

/**
 * @brief 码流注册函数
 * @param [in] info 码流注册信息数组 (目前仅支持一路码流)
 * @param [in] size 码流注册信息个数
 */
void NatStreamingRegister(NatStreamingRegisterInfo* info, unsigned int size);

typedef enum {
	NAT_FRAME_VIDEO,		///< 视频帧
	NAT_FRAME_AUDIO,		///< 音频帧
}NatFrameType;

typedef struct {
	int chn;					///< 音视频通道
	int frame_type;				///< 帧类型, NatFrameType
	unsigned int stream_type;	///< 码流类型, NatStreamingType
	uint64_t pts;				///< 【-1：内部计算pts】【 >= 0：使用pts】
	unsigned int size;			///< 音频单次传输大小需要小于1400字节
	unsigned char* buff;
}NatPushStreamInfo;

/**
 * @brief 推流函数
 * @param [in] info 码流信息
 * @return 成功返回 0
 *         失败返回 其他值
 */
int NatPushStream(NatPushStreamInfo* info);

#ifdef __cplusplus
}
#endif

#endif