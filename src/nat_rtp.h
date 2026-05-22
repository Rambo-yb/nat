#ifndef __NAT_RTP_H__
#define __NAT_RTP_H__

#include <stdint.h>
#include <string>
#include <queue>

#define NAT_FRAME_MAX_SIZE (1024*500)

class NatAvFrame
{
public:
    NatAvFrame() :
        m_buffer(new uint8_t[NAT_FRAME_MAX_SIZE]),
        m_frame_size(0)
    { }

    ~NatAvFrame()
    { delete m_buffer; }

	void Clear() {memset(m_buffer, 0, NAT_FRAME_MAX_SIZE); m_frame_size = 0;}

    uint8_t* m_buffer;
    uint8_t* m_frame;
    int m_frame_size;
	uint64_t m_pts;
};


#define NAT_RTP_HEADER_SIZE         12
#define NAT_RTP_MAX_PKT_SIZE        1400

struct NatRtpHeader
{
    uint8_t csrcLen:4;
    uint8_t extension:1;
    uint8_t padding:1;
    uint8_t version:2;

    uint8_t payloadType:7;
    uint8_t marker:1;
    
    uint16_t seq;
    
    uint32_t timestamp;
    
    uint32_t ssrc;

    uint8_t payload[0];
};

class NatRtpPacket
{
public:
    NatRtpPacket() :
        _m_buffer(new uint8_t[NAT_RTP_MAX_PKT_SIZE+NAT_RTP_HEADER_SIZE+100]),
        m_buffer(_m_buffer+4),
        m_rtp_headr((NatRtpHeader*)m_buffer),
        m_size(0)
    {
        
    }

    ~NatRtpPacket()
    {
        delete _m_buffer;
    }

    uint8_t* _m_buffer;
    uint8_t* m_buffer;
    NatRtpHeader* const m_rtp_headr;
    int m_size;
};



#define NAT_RTP_QUEUE_MAX_ITEM (400)
#define NAT_RTP_QUEUE_PUSH(queue, item) \
	do { \
		if (queue.size() > NAT_RTP_QUEUE_MAX_ITEM) { \
			queue.pop(); \
		} \
		queue.push(item); \
	} while(0);

class NatRtpSink {
public:
	typedef int (*SendPacketCallback)(void* arg1, void* arg2, void* packet);

	NatRtpSink(int payload_type);
	void setSendFrameCallback(SendPacketCallback cb, void* arg1, void* arg2);

    int sendRtpPacket(NatRtpPacket* packet, uint16_t seq, uint32_t ts, uint8_t marker);
	virtual void handleFrame(NatAvFrame* frame) = 0;
	virtual std::string getDescription() = 0;
protected:
	SendPacketCallback m_send_packet_cb;
    void* m_arg1;
    void* m_arg2;

	uint8_t m_csrc_len;
    uint8_t m_extension;
    uint8_t m_padding;
    uint8_t m_version;
    uint8_t m_payload_type;
    uint8_t m_marker;
    uint16_t m_seq;
    uint32_t m_timestamp;
    uint32_t m_ssrc;

protected:
	struct NatRtpData {
		NatRtpHeader rtp_header;
		uint8_t frame[1500];
		int size;
	};
	
	std::queue<NatRtpData> m_rtp_queue;

};


class NatH264RtpSink : public NatRtpSink {
public:
	NatH264RtpSink(int fps);
	virtual ~NatH264RtpSink();
	void handleFrame(NatAvFrame* frame);
	std::string getDescription();
private:
    NatRtpPacket m_rtp_packet;
    int m_clock_rate;
    int m_fps;
};

class NatH265RtpSink : public NatRtpSink {
public:
	NatH265RtpSink(int fps);
	virtual ~NatH265RtpSink();
	void handleFrame(NatAvFrame* frame);
	std::string getDescription();
private:
    NatRtpPacket m_rtp_packet;
    int m_clock_rate;
    int m_fps;
};

class NatAACRtpSink : public NatRtpSink {
public:
	NatAACRtpSink(int sample_rate, int channels);
	virtual ~NatAACRtpSink();
	void handleFrame(NatAvFrame* frame);
	std::string getDescription();
private:
    NatRtpPacket m_rtp_packet;
	int m_sample_rate;
	int m_channels;
};

class NatG711aRtpSink : public NatRtpSink {
public:
	NatG711aRtpSink(int sample_rate, int channels);
	virtual ~NatG711aRtpSink();
	void handleFrame(NatAvFrame* frame);
	std::string getDescription();
private:
    NatRtpPacket m_rtp_packet;
	int m_sample_rate;
	int m_channels;
};


#endif