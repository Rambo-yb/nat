#include <queue>
#include <map>
#include <pthread.h>
#include <string.h>
#include <algorithm>

#include "check_common.h"

#include "nat_media.h"

#include "base/New.h"

NatMediaSession* NatMediaSession::createNew(int chn)
{
    return New<NatMediaSession>::allocate(chn);
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

extern void NatMediaMutexLock();
extern void NatMediaMutexUnlock();

int NatMediaSession::sendPacket(NatMediaSession::NatTrack* tarck, NatRtpPacket* rtpPacket) {
	NatMediaMutexLock();
    for(std::list<int>::iterator it = tarck->m_trs.begin(); it != tarck->m_trs.end(); ++it) {
		m_cb(*it, rtpPacket->m_buffer, rtpPacket->m_size);
    }
	NatMediaMutexUnlock();
	return 0;

}