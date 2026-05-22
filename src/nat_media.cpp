#include <queue>
#include <map>
#include <pthread.h>
#include <string.h>
#include <algorithm>

#include "check_common.h"

#include "nat_media.h"

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


NatMediaSession::NatMediaSession(int chn) : 
	m_chn(chn)
{
	m_tracks[0].m_track_id = TrackId0;
	m_tracks[1].m_track_id = TrackId1;
	m_tracks[0].m_rtp_sink = NULL;
	m_tracks[1].m_rtp_sink = NULL;
	m_tracks[0].m_is_alive = 0;
	m_tracks[1].m_is_alive = 0;
}

NatMediaSession::~NatMediaSession()
{
}

bool NatMediaSession::addRtpSink(NatMediaSession::TrackId track_id, NatRtpSink* rtp_sink)
{
    NatTrack* track = getTrack(track_id);
    if(!track)
        return false;

    track->m_rtp_sink = rtp_sink;
    track->m_is_alive = true;

    rtp_sink->setSendFrameCallback(sendPacketCallback, this, track);

    return true;

}

bool NatMediaSession::addTrInstance(NatMediaSession::TrackId track_id, int tr) {
    NatTrack* track = getTrack(track_id);
    if(!track || track->m_is_alive != true)
        return false;

	track->m_trs.push_back(tr);

	return true;
}

bool NatMediaSession::removeTrInstance(int tr) {
	for(int i = 0; i <  NAT_MEDIA_MAX_TRACK_NUM; ++i) {
		if(m_tracks[i].m_is_alive == false)
			continue;

		std::list<int>::iterator it = std::find(m_tracks[i].m_trs.begin(), m_tracks[i].m_trs.end(), tr);
		if (it == m_tracks[i].m_trs.end())
			continue;
		
		m_tracks[i].m_trs.erase(it);
		return true;
	}

	return false;
}

void NatMediaSession::frameHandle(NatMediaSession::TrackId track_id, NatAvFrame* frame) {
	NatTrack* track = getTrack(track_id);
    if(!track)
        return ;
	
	track->m_rtp_sink->handleFrame(frame);
}

bool NatMediaSession::isSupport(NatMediaSession::TrackId track_id) {
	NatTrack* track = getTrack(track_id);
    if(!track)
        return false;
	
	return track->m_rtp_sink != NULL;
}

std::string NatMediaSession::getDescription(NatMediaSession::TrackId track_id) {
	NatTrack* track = getTrack(track_id);
    if(!track)
        return "";
	
	return track->m_rtp_sink->getDescription();
}

NatMediaSession::NatTrack* NatMediaSession::getTrack(NatMediaSession::TrackId track_id)
{
    for(int i = 0; i < NAT_MEDIA_MAX_TRACK_NUM; ++i)
    {
        if(m_tracks[i].m_track_id == track_id)
            return &m_tracks[i];
    }

    return NULL;
}

int NatMediaSession::sendPacketCallback(void* arg1, void* arg2, void* packet) {

    NatMediaSession* mediaSession = (NatMediaSession*)arg1;
    NatMediaSession::NatTrack* track = (NatMediaSession::NatTrack*)arg2;
    
	return mediaSession->sendPacket(track, (NatRtpPacket*)packet);
}

int NatMediaSession::sendPacket(NatMediaSession::NatTrack* tarck, NatRtpPacket* rtpPacket) {
    for(std::list<int>::iterator it = tarck->m_trs.begin(); it != tarck->m_trs.end(); ++it) {
		m_cb(*it, rtpPacket->m_buffer, rtpPacket->m_size);
    }
	return 0;

}