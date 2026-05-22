#ifndef __NAT_MEDIA_H__
#define __NAT_MEDIA_H__


#include <list>
#include "nat_rtp.h"

int NatMediaVideoPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts);

int NatMediaVideoPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts);

int NatMediaAudioPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts);

int NatMediaAudioPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts);


#define NAT_MEDIA_MAX_TRACK_NUM 2

typedef int (*NatSendPacketCb)(int, unsigned char*, int);

class NatMediaSession
{
public:
    enum TrackId
    {
        TrackIdNone = -1,
        TrackId0    = 0,
        TrackId1    = 1,
    };

	NatMediaSession(int chn);
	~NatMediaSession();

	bool addRtpSink(NatMediaSession::TrackId track_id, NatRtpSink* rtp_sink);
	bool addTrInstance(NatMediaSession::TrackId track_id, int tr);
	bool removeTrInstance(int tr);
	void setSendPacketCb(NatSendPacketCb cb) { m_cb = cb;};

	void frameHandle(NatMediaSession::TrackId track_id, NatAvFrame* frame);
	bool isSupport(NatMediaSession::TrackId track_id);
	std::string getDescription(NatMediaSession::TrackId track_id);

private:
    class NatTrack
    {
    public:
        NatRtpSink* m_rtp_sink;
        int m_track_id;
        bool m_is_alive;
        std::list<int> m_trs;
    };
	NatTrack* getTrack(NatMediaSession::TrackId track_id);
	static int sendPacketCallback(void* arg1, void* arg2, void* packet);
    int sendPacket(NatMediaSession::NatTrack* tarck, NatRtpPacket* rtpPacket);

private:
	NatTrack m_tracks[NAT_MEDIA_MAX_TRACK_NUM];
	int m_chn;
	NatSendPacketCb m_cb;
};


#endif