#include <queue>
#include <map>
#include <pthread.h>
#include <string.h>
#include <algorithm>

#include "base/New.h"

#include "check_common.h"
#include "nat_media_source.h"


#define NAT_MEDIA_CHN_MAX (2)
#define NAT_MEDIA_QUEUE_MAX (6)

#define NAT_MEDIA_VIDEO_SIZE_MAX (512*1024)
#define NAT_MEDIA_AUDIO_SIZE_MAX (2*1024)

typedef struct {
	uint8_t pkt[NAT_MEDIA_VIDEO_SIZE_MAX];
	uint32_t size;
	uint64_t pts;
}NatMediaVideoInfo;

typedef struct {
	uint8_t pkt[NAT_MEDIA_AUDIO_SIZE_MAX];
	uint32_t size;
	uint64_t pts;
}NatMediaAudioInfo;

typedef struct {
	pthread_mutex_t mutex;
	std::map<int, std::queue<NatMediaVideoInfo>> video_src;
	std::map<int, std::queue<NatMediaAudioInfo>> audio_src;
}NatMediaMng;
static NatMediaMng kNatMediaMng = {.mutex = PTHREAD_MUTEX_INITIALIZER};

int NatMediaVideoPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts) {
	CHECK_POINTER(pkt, return -1);
	CHECK_LE(size, 0, return -1);
	CHECK_GT(size, NAT_MEDIA_VIDEO_SIZE_MAX, return -1);

	NatMediaVideoInfo video_info;
	memset(&video_info, 0, sizeof(NatMediaVideoInfo));
	video_info.pts = pts;
	video_info.size = size;
	memcpy(video_info.pkt, pkt, size);

	pthread_mutex_lock(&kNatMediaMng.mutex);
	if (kNatMediaMng.video_src.find(chn) != kNatMediaMng.video_src.end()) {
		if (kNatMediaMng.video_src[chn].size() >= NAT_MEDIA_QUEUE_MAX) {
			kNatMediaMng.video_src[chn].pop();
			LOG_WRN("chn[%d] video queue greater than queue max [%d], remove old video src !!!", chn, NAT_MEDIA_QUEUE_MAX);
		}
		kNatMediaMng.video_src[chn].push(video_info);
	} else {
		std::queue<NatMediaVideoInfo> video_queue;
		video_queue.push(video_info);
		kNatMediaMng.video_src.insert({chn, video_queue});
	}
	pthread_mutex_unlock(&kNatMediaMng.mutex);

	return 0;
}

int NatMediaVideoPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts) {
	CHECK_POINTER(pkt, return -1);
	CHECK_LE(size, 0, return -1);

	int len = 0;
	pthread_mutex_lock(&kNatMediaMng.mutex);
	if (kNatMediaMng.video_src.find(chn) != kNatMediaMng.video_src.end() && !kNatMediaMng.video_src[chn].empty()) {
		len = kNatMediaMng.video_src[chn].front().size;
		if (len > size) {
			LOG_ERR("insufficient space, need: %d", len);
			pthread_mutex_unlock(&kNatMediaMng.mutex);
			return -1;
		}

		*pts = kNatMediaMng.video_src[chn].front().pts;
		memcpy(pkt, kNatMediaMng.video_src[chn].front().pkt, len);
		kNatMediaMng.video_src[chn].pop();
	}
	pthread_mutex_unlock(&kNatMediaMng.mutex);

	return len;
}


int NatMediaAudioPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts) {
	CHECK_POINTER(pkt, return -1);
	CHECK_LE(size, 0, return -1);
	CHECK_GT(size, NAT_MEDIA_AUDIO_SIZE_MAX, return -1);

	NatMediaAudioInfo audio_info;
	memset(&audio_info, 0, sizeof(NatMediaAudioInfo));
	audio_info.pts = pts;
	audio_info.size = size;
	memcpy(audio_info.pkt, pkt, size);

	pthread_mutex_lock(&kNatMediaMng.mutex);
	if (kNatMediaMng.audio_src.find(chn) != kNatMediaMng.audio_src.end()) {
		if (kNatMediaMng.audio_src[chn].size() >= NAT_MEDIA_QUEUE_MAX) {
			kNatMediaMng.audio_src[chn].pop();
			LOG_WRN("chn[%d] audio queue greater than queue max [%d], remove old audio src !!!", chn, NAT_MEDIA_QUEUE_MAX);
		}
		kNatMediaMng.audio_src[chn].push(audio_info);
	} else {
		std::queue<NatMediaAudioInfo> audio_queue;
		audio_queue.push(audio_info);
		kNatMediaMng.audio_src.insert({chn, audio_queue});
	}
	pthread_mutex_unlock(&kNatMediaMng.mutex);

	return 0;
}

int NatMediaAudioPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts) {
	CHECK_POINTER(pkt, return -1);
	CHECK_LE(size, 0, return -1);

	int len = 0;
	pthread_mutex_lock(&kNatMediaMng.mutex);
	if (kNatMediaMng.audio_src.find(chn) != kNatMediaMng.audio_src.end() && !kNatMediaMng.audio_src[chn].empty()) {
		len = kNatMediaMng.audio_src[chn].front().size;
		if (len > size) {
			LOG_ERR("insufficient space, need: %d", len);
			pthread_mutex_unlock(&kNatMediaMng.mutex);
			return -1;
		}

		*pts = kNatMediaMng.audio_src[chn].front().pts;
		memcpy(pkt, kNatMediaMng.audio_src[chn].front().pkt, len);
		kNatMediaMng.audio_src[chn].pop();
	}
	pthread_mutex_unlock(&kNatMediaMng.mutex);

	return len;
}


MediaSource::MediaSource(UsageEnvironment* env) :
    mEnv(env)
{
    mMutex = Mutex::createNew();
    for(int i = 0; i < NAT_FRAME_NUM; ++i)
        mAVFrameInputQueue.push(&mAVFrames[i]);
    
    mTask.setTaskCallback(taskCallback, this);
}

MediaSource::~MediaSource()
{
    Delete::release(mMutex);
}

NatAvFrame* MediaSource::getFrame()
{
    MutexLockGuard mutexLockGuard(mMutex);

    if(mAVFrameOutputQueue.empty())
    {
    	mEnv->threadPool()->addTask(mTask);
        return NULL;
    }

    NatAvFrame* frame = mAVFrameOutputQueue.front();    
    mAVFrameOutputQueue.pop();

    return frame;
}

void MediaSource::putFrame(NatAvFrame* frame)
{
    MutexLockGuard mutexLockGuard(mMutex);

    mAVFrameInputQueue.push(frame);
    
    mEnv->threadPool()->addTask(mTask);
}

void MediaSource::taskCallback(void* arg)
{
    MediaSource* source = (MediaSource*)arg;
    source->readFrame();
}


VideoSource* VideoSource::createNew(UsageEnvironment* env, int chn, int fps) {
	return New<VideoSource>::allocate(env, chn, fps);
}

VideoSource::VideoSource(UsageEnvironment* env, int chn, int fps) : MediaSource(env) {
	setFps(fps);

	channel = chn;

	for(int i = 0; i < NAT_FRAME_NUM; ++i)
		mEnv->threadPool()->addTask(mTask);
}

VideoSource::~VideoSource() {

}

void VideoSource::readFrame() {
	MutexLockGuard mutexLockGuard(mMutex);

	if(mAVFrameInputQueue.empty())
		return;

	uint64_t pts = 0; 
	uint8_t buffer[NAT_FRAME_MAX_SIZE] = {0};
	int frame_size = NatMediaVideoPop(channel, buffer, NAT_FRAME_MAX_SIZE, &pts);
	if (frame_size < 0) {
		return ;
	}

	int offset = 0;
	while (offset < frame_size) {
		int size = 0;
		uint8_t* p = FindNalu(buffer + offset, frame_size - offset, &size);

		if(mAVFrameInputQueue.empty())
			break;

		NatAvFrame* frame = mAVFrameInputQueue.front();
		memcpy(frame->m_buffer, p, size);
		frame->m_frame_size = size;
		frame->m_pts = pts;

		if(frame->m_buffer[0] == 0 && frame->m_buffer[1] == 0 && frame->m_buffer[2] == 1) {
			frame->m_frame = frame->m_buffer+3;
			frame->m_frame_size -= 3;
		} else {
			frame->m_frame = frame->m_buffer+4;
			frame->m_frame_size -= 4;
		}

		mAVFrameInputQueue.pop();
		mAVFrameOutputQueue.push(frame);

		offset = p - buffer + size;
	}
}

uint8_t* VideoSource::FindNalu(uint8_t *buff, int len, int *size) {
	uint8_t *s = NULL;
	while (len >= 3) {
		if (buff[0] == 0 && buff[1] == 0 && buff[2] == 1) {
			if (!s) {
				if (len < 4)
					return NULL;
				s = buff;
			} else {
				*size = (buff - s);
				return s;
			}
			buff += 3;
			len  -= 3;
			continue;
		}
		if (len >= 4 && buff[0] == 0 && buff[1] == 0 && buff[2] == 0 && buff[3] == 1) {
			if (!s) {
				if (len < 5)
					return NULL;
				s = buff;
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


AudioSource* AudioSource::createNew(UsageEnvironment* env, int chn) {
	return New<AudioSource>::allocate(env, chn);
}

AudioSource::AudioSource(UsageEnvironment* env, int chn) : MediaSource(env) {
	channel = chn;

	for(int i = 0; i < NAT_FRAME_NUM; ++i)
		mEnv->threadPool()->addTask(mTask);
}

AudioSource::~AudioSource() {

}

void AudioSource::readFrame() {
	MutexLockGuard mutexLockGuard(mMutex);

	if(mAVFrameInputQueue.empty())
		return;

	NatAvFrame* frame = mAVFrameInputQueue.front();

	frame->m_frame_size = NatMediaAudioPop(channel, frame->m_buffer, NAT_FRAME_MAX_SIZE, &frame->m_pts);
	if(frame->m_frame_size < 0)
		return;
    frame->m_frame = frame->m_buffer;

	mAVFrameInputQueue.pop();
	mAVFrameOutputQueue.push(frame);
}

