

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "base/New.h"

#include "nat_rtp.h"
#include "log.h"

NatRtpSink::NatRtpSink(UsageEnvironment* env, MediaSource* media_source, int payload_type) :
	m_env(env), m_media_source(media_source),
	m_csrc_len(0), m_extension(0), m_padding(0), m_version(2), m_payload_type(payload_type), 
	m_marker(0), m_seq(0), m_timestamp(0) 
{
    m_timer_event = TimerEvent::createNew(this);
    m_timer_event->setTimeoutCallback(timeoutCallback);

	m_ssrc = rand();
}

void NatRtpSink::setSendFrameCallback(SendPacketCallback cb, void* arg1, void* arg2) {
    m_send_packet_cb = cb;
    m_arg1 = arg1;
    m_arg2 = arg2;
}

int NatRtpSink::sendRtpPacket(NatRtpPacket* packet, uint16_t seq, uint32_t ts, uint8_t marker) {
	NatRtpHeader* rtpHead = packet->m_rtp_headr;
    rtpHead->csrcLen = m_csrc_len;
    rtpHead->extension = m_extension;
    rtpHead->padding = m_padding;
    rtpHead->version = m_version;
    rtpHead->payloadType = m_payload_type;
    rtpHead->marker = marker;
    rtpHead->seq = htons(seq);
    rtpHead->timestamp = htonl(ts);
    rtpHead->ssrc = htonl(m_ssrc);
    packet->m_size += NAT_RTP_HEADER_SIZE;
	
    if(m_send_packet_cb)
        return m_send_packet_cb(m_arg1, m_arg2, packet);

	return -1;
}

void NatRtpSink::start(int ms)
{
    m_timer_id = m_env->scheduler()->addTimedEventRunEvery(m_timer_event, ms);
}

void NatRtpSink::stop()
{
    m_env->scheduler()->removeTimedEvent(m_timer_id);
}

void NatRtpSink::timeoutCallback(void* arg)
{
    NatRtpSink* rtpSink = (NatRtpSink*)arg;
    NatAvFrame* frame = rtpSink->m_media_source->getFrame();
    if(!frame)
    {
        return;
    }

    rtpSink->handleFrame(frame);

    rtpSink->m_media_source->putFrame(frame);
}

#include <sys/time.h>
static long GetTime() {
    struct timeval time_;
    memset(&time_, 0, sizeof(struct timeval));

    gettimeofday(&time_, NULL);
    return time_.tv_sec*1000 + time_.tv_usec/1000;
}


NatH264RtpSink* NatH264RtpSink::createNew(UsageEnvironment* env, MediaSource* media_source)
{
    if(!media_source)
        return NULL;

    return New<NatH264RtpSink>::allocate(env, media_source);
}

NatH264RtpSink::NatH264RtpSink(UsageEnvironment* env, MediaSource* media_source) : 
	NatRtpSink(env, media_source, 96), m_clock_rate(90000), m_fps(media_source->getFps())
{
    start(10);
}

NatH264RtpSink::~NatH264RtpSink()
{
}

std::string NatH264RtpSink::getDescription() {
	char buff[1024] = {0};
	snprintf(buff, sizeof(buff), "m=video 9 RTP/AVP %d\r\n", m_payload_type);
	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), 
		"a=mid:video\r\n"
		"a=sendonly\r\n"
		"a=ssrc:%s\r\n", 
		std::to_string(m_ssrc).c_str());

    snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=rtpmap:%d H264/%d\r\n", m_payload_type, m_clock_rate);
    snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=framerate:%d", m_fps);
	
	return std::string(buff);
}

void NatH264RtpSink::handleFrame(NatAvFrame* frame)
{
	if (frame->m_pts != -1) {
		m_timestamp = frame->m_pts;
	}
	uint8_t naluType = frame->m_frame[0];

	if (frame->m_frame_size <= NAT_RTP_MAX_PKT_SIZE) {
		NatRtpData rtp_data;
		memset(&rtp_data, 0, sizeof(NatRtpData));
		rtp_data.rtp_header.timestamp = m_timestamp;
		rtp_data.rtp_header.marker = 1;
		memcpy(rtp_data.frame, frame->m_frame, frame->m_frame_size);
		rtp_data.size = frame->m_frame_size;
		rtp_data.rtp_header.seq = m_seq;
		NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

		m_seq++;
	} else {
		int pkt_num = frame->m_frame_size / NAT_RTP_MAX_PKT_SIZE;
		int remain_pkt_size = frame->m_frame_size % NAT_RTP_MAX_PKT_SIZE;
		int pos = 1;

		for(int i = 0; i < pkt_num; i++) {
			NatRtpData rtp_data;
			memset(&rtp_data, 0, sizeof(NatRtpData));
			rtp_data.rtp_header.timestamp = m_timestamp;
			rtp_data.frame[0] = (naluType & 0xe0) | 28;
			rtp_data.frame[1] = naluType & 0x1F;
			if (i == 0) {
				rtp_data.frame[1] |= 0x80;
			} else if (remain_pkt_size == 0 && i == pkt_num - 1) {
				rtp_data.rtp_header.marker = 1;
				rtp_data.frame[1] |= 0x40;
			}

			memcpy(rtp_data.frame+2, frame->m_frame+pos, NAT_RTP_MAX_PKT_SIZE);
			rtp_data.size = NAT_RTP_MAX_PKT_SIZE + 2;
			rtp_data.rtp_header.seq = m_seq;		
			NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

			m_seq++;
			pos += NAT_RTP_MAX_PKT_SIZE;
		}

		if (remain_pkt_size > 0) {
			NatRtpData rtp_data;
			memset(&rtp_data, 0, sizeof(NatRtpData));
			rtp_data.rtp_header.timestamp = m_timestamp;
			rtp_data.rtp_header.marker = 1;
			rtp_data.frame[0] = (naluType & 0xe0) | 28;
			rtp_data.frame[1] = naluType & 0x1F;
			rtp_data.frame[1] |= 0x40;

			memcpy(rtp_data.frame+2, frame->m_frame+pos, remain_pkt_size);
			rtp_data.size = remain_pkt_size + 2;	
			rtp_data.rtp_header.seq = m_seq;
			NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

			m_seq++;
		}
	}
	
	while(!m_rtp_queue.empty()) {
		NatRtpData item = m_rtp_queue.front();
		NatRtpHeader* rtpHeader = m_rtp_packet.m_rtp_headr;
		memcpy(rtpHeader->payload, item.frame, item.size);
		m_rtp_packet.m_size = item.size;
		if (sendRtpPacket(&m_rtp_packet, item.rtp_header.seq, item.rtp_header.timestamp, item.rtp_header.marker) < 0) {
			break;
		}

		m_rtp_queue.pop();
	}

	if ((naluType & 0x1F) == 7 || (naluType & 0x1F) == 8 || (naluType & 0x1F) == 6)
		return;

	if(frame->m_pts == -1) {
		m_timestamp += m_clock_rate/m_fps;
	}
}


NatH265RtpSink* NatH265RtpSink::createNew(UsageEnvironment* env, MediaSource* media_source)
{
    if(!media_source)
        return NULL;

    return New<NatH265RtpSink>::allocate(env, media_source);
}

NatH265RtpSink::NatH265RtpSink(UsageEnvironment* env, MediaSource* media_source) : 
	NatRtpSink(env, media_source, 96), m_clock_rate(90000), m_fps(media_source->getFps())
{
    start(10);
}

NatH265RtpSink::~NatH265RtpSink()
{
}

std::string NatH265RtpSink::getDescription() {
	char buff[1024] = {0};
	snprintf(buff, sizeof(buff), "m=video 9 RTP/AVP %d\r\n", m_payload_type);
	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), 
		"a=mid:video\r\n"
		"a=sendonly\r\n"
		"a=ssrc:%s\r\n", 
		std::to_string(m_ssrc).c_str());

    snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=rtpmap:%d H265/%d\r\n", m_payload_type, m_clock_rate);
    snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=framerate:%d", m_fps);
	
	return std::string(buff);
}

void NatH265RtpSink::handleFrame(NatAvFrame* frame)
{
	if (frame->m_pts != -1) {
		m_timestamp = frame->m_pts;
	}
	uint8_t naluType[2] = {frame->m_frame[0], frame->m_frame[1]};

	if (frame->m_frame_size <= NAT_RTP_MAX_PKT_SIZE) {
		NatRtpData rtp_data;
		memset(&rtp_data, 0, sizeof(NatRtpData));
		rtp_data.rtp_header.timestamp = m_timestamp;
		rtp_data.rtp_header.marker = 1;
		memcpy(rtp_data.frame, frame->m_frame, frame->m_frame_size);
		rtp_data.size = frame->m_frame_size;
		rtp_data.rtp_header.seq = m_seq;
		NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

		m_seq++;
	} else {
		int pkt_num = frame->m_frame_size / NAT_RTP_MAX_PKT_SIZE;
		int remain_pkt_size = frame->m_frame_size % NAT_RTP_MAX_PKT_SIZE;
		int pos = 1;

		for(int i = 0; i < pkt_num; i++) {
			NatRtpData rtp_data;
			memset(&rtp_data, 0, sizeof(NatRtpData));
			rtp_data.rtp_header.timestamp = m_timestamp;
			rtp_data.frame[0] = (naluType[0] & 0x81) | (49 << 1);
			rtp_data.frame[1] = naluType[1];
            rtp_data.frame[2] = (naluType[0] >> 1) & 0x3f;
			if (i == 0) {
				rtp_data.frame[2] |= 0x80;
			} else if (remain_pkt_size == 0 && i == pkt_num - 1) {
				rtp_data.rtp_header.marker = 1;
				rtp_data.frame[2] |= 0x40;
			}

			memcpy(rtp_data.frame+3, frame->m_frame+pos, NAT_RTP_MAX_PKT_SIZE);
			rtp_data.size = NAT_RTP_MAX_PKT_SIZE + 3;
			rtp_data.rtp_header.seq = m_seq;		
			NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

			m_seq++;
			pos += NAT_RTP_MAX_PKT_SIZE;
		}

		if (remain_pkt_size > 0) {
			NatRtpData rtp_data;
			memset(&rtp_data, 0, sizeof(NatRtpData));
			rtp_data.rtp_header.timestamp = m_timestamp;
			rtp_data.rtp_header.marker = 1;
			rtp_data.frame[0] = (naluType[0] & 0x81) | (49 << 1);
			rtp_data.frame[1] = naluType[1];
			rtp_data.frame[2] = (naluType[0] >> 1) & 0x3f;
			rtp_data.frame[2] |= 0x40;

			memcpy(rtp_data.frame+3, frame->m_frame+pos, remain_pkt_size);
			rtp_data.size = remain_pkt_size + 3;	
			rtp_data.rtp_header.seq = m_seq;
			NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);

			m_seq++;
		}
	}
	
	while(!m_rtp_queue.empty()) {
		NatRtpData item = m_rtp_queue.front();
		NatRtpHeader* rtpHeader = m_rtp_packet.m_rtp_headr;
		memcpy(rtpHeader->payload, item.frame, item.size);
		m_rtp_packet.m_size = item.size;
		if (sendRtpPacket(&m_rtp_packet, item.rtp_header.seq, item.rtp_header.timestamp, item.rtp_header.marker) < 0) {
			break;
		}

		m_rtp_queue.pop();
	}

	if (((naluType[0] & 0x7e) >> 1) == 32 || ((naluType[0] & 0x7e) >> 1) == 33 || ((naluType[0] & 0x7e) >> 1) == 34 || ((naluType[0] & 0x7e) >> 1) == 39)
		return;

	if(frame->m_pts == -1) {
		m_timestamp += m_clock_rate/m_fps;
	}
}


NatAACRtpSink* NatAACRtpSink::createNew(UsageEnvironment* env, MediaSource* media_source, int sample_rate, int channels)
{
    return New<NatAACRtpSink>::allocate(env, media_source, sample_rate, channels);
}

NatAACRtpSink::NatAACRtpSink(UsageEnvironment* env, MediaSource* media_source, int sample_rate, int channels) : 
	NatRtpSink(env, media_source, 97), m_sample_rate(sample_rate), m_channels(channels)
{
    start(((1024*1000)/sample_rate) * 0.5);
}

NatAACRtpSink::~NatAACRtpSink()
{
}

std::string NatAACRtpSink::getDescription() {
	char buff[1024] = {0};
	snprintf(buff, sizeof(buff), "m=audio 9 RTP/AVP %d\r\n", m_payload_type);
	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), 
		"a=mid:audio\r\n"
		"a=sendonly\r\n"
		"a=ssrc:%s\r\n", 
		std::to_string(m_ssrc).c_str());

	uint32_t aac_sample_rate[16] =
	{
		97000, 88200, 64000, 48000,
		44100, 32000, 24000, 22050,
		16000, 12000, 11025, 8000,
		7350, 0, 0, 0 /*reserved */
	};

	uint8_t index = 0;
	for (index = 0; index < 16; index++)
	{
		if (aac_sample_rate[index] == m_sample_rate)
			break;
	}

	if (index == 16)
		return "";

	uint8_t profile = 1;
	char config[10] = {0};
	sprintf(config, "%02x%02x", (uint8_t)((profile+1) << 3)|(index >> 1),
			(uint8_t)((index << 7)|(m_channels<< 3)));

	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=rtpmap:97 mpeg4-generic/%u/%u\r\n", m_sample_rate, m_channels);

	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), 
		"a=fmtp:%d profile-level-id=1;"
		"mode=AAC-hbr;"
		"sizelength=13;indexlength=3;indexdeltalength=3;"
		"config=%04u",
		m_payload_type, atoi(config));
	
	return std::string(buff);
}

void NatAACRtpSink::handleFrame(NatAvFrame* frame)
{
	if (frame->m_pts != -1) {
		m_timestamp = frame->m_pts;
	}

	NatRtpData rtp_data;
	memset(&rtp_data, 0, sizeof(NatRtpData));
	rtp_data.rtp_header.timestamp = m_timestamp;
	rtp_data.rtp_header.marker = 1;

	rtp_data.frame[0] = 0x00;
	rtp_data.frame[1] = 0x10;
	rtp_data.frame[2] = ((frame->m_frame_size - 7) & 0x1FE0) >> 5;
	rtp_data.frame[3] = ((frame->m_frame_size - 7) & 0x1F) << 3;

	memcpy(rtp_data.frame+4, frame->m_frame+7, frame->m_frame_size - 7);
	rtp_data.size = frame->m_frame_size - 7 + 4;
	rtp_data.rtp_header.seq = m_seq;
	NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);
	
	while(!m_rtp_queue.empty()) {
		NatRtpData item = m_rtp_queue.front();
		NatRtpHeader* rtpHeader = m_rtp_packet.m_rtp_headr;
		memcpy(rtpHeader->payload, item.frame, item.size);
		m_rtp_packet.m_size = item.size;
		if (sendRtpPacket(&m_rtp_packet, item.rtp_header.seq, item.rtp_header.timestamp, item.rtp_header.marker) < 0) {
			break;
		}

		m_rtp_queue.pop();
	}

	m_seq++;

	if(frame->m_pts == -1) {
		m_timestamp += 1024;
	}
}



NatG711aRtpSink* NatG711aRtpSink::createNew(UsageEnvironment* env, MediaSource* media_source, int sample_rate, int channels)
{
    return New<NatG711aRtpSink>::allocate(env, media_source, sample_rate, channels);
}

NatG711aRtpSink::NatG711aRtpSink(UsageEnvironment* env, MediaSource* media_source, int sample_rate, int channels) : 
	NatRtpSink(env, media_source, 97), m_sample_rate(sample_rate), m_channels(channels)
{
    start(10);
}

NatG711aRtpSink::~NatG711aRtpSink()
{
}

std::string NatG711aRtpSink::getDescription() {
	char buff[1024] = {0};
	snprintf(buff, sizeof(buff), "m=audio 9 RTP/AVP %d\r\n", m_payload_type);
	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), 
		"a=mid:audio\r\n"
		"a=sendonly\r\n"
		"a=ssrc:%s\r\n", 
		std::to_string(m_ssrc).c_str());

	snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff), "a=rtpmap:8 PCMA/%u/%u\r\n", m_sample_rate, m_channels);
	
	return std::string(buff);
}

void NatG711aRtpSink::handleFrame(NatAvFrame* frame)
{
	if (frame->m_pts != -1) {
		m_timestamp = frame->m_pts;
	}

	NatRtpData rtp_data;
	memset(&rtp_data, 0, sizeof(NatRtpData));
	rtp_data.rtp_header.timestamp = m_timestamp;
	rtp_data.rtp_header.marker = 1;

	memcpy(rtp_data.frame, frame->m_frame, frame->m_frame_size);
	rtp_data.size = frame->m_frame_size;
	rtp_data.rtp_header.seq = m_seq;
	NAT_RTP_QUEUE_PUSH(m_rtp_queue, rtp_data);
	
	while(!m_rtp_queue.empty()) {
		NatRtpData item = m_rtp_queue.front();
		NatRtpHeader* rtpHeader = m_rtp_packet.m_rtp_headr;
		memcpy(rtpHeader->payload, item.frame, item.size);
		m_rtp_packet.m_size = item.size;
		if (sendRtpPacket(&m_rtp_packet, item.rtp_header.seq, item.rtp_header.timestamp, item.rtp_header.marker) < 0) {
			break;
		}

		m_rtp_queue.pop();
	}

	m_seq++;

	if(frame->m_pts == -1) {
		m_timestamp += 160;
	}
}