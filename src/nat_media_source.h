#ifndef __NAT_MEDIA_SOURCE_H__
#define __NAT_MEDIA_SOURCE_H__


#include "net/UsageEnvironment.h"

int NatMediaVideoPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts);

int NatMediaVideoPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts);

int NatMediaAudioPush(int chn, uint8_t* pkt, uint32_t size, uint64_t pts);

int NatMediaAudioPop(int chn, uint8_t* pkt, uint32_t size, uint64_t* pts);


#define NAT_FRAME_MAX_SIZE (1024*500)
#define NAT_FRAME_NUM (6)

class NatAvFrame
{
public:
    NatAvFrame() :
        m_buffer(new uint8_t[NAT_FRAME_MAX_SIZE]),
        m_frame_size(0)
    { }

    ~NatAvFrame()
    { delete m_buffer; }

    uint8_t* m_buffer;
    uint8_t* m_frame;
    int m_frame_size;
	uint64_t m_pts;
};


class MediaSource
{
public:
    virtual ~MediaSource();

    NatAvFrame* getFrame();
    void putFrame(NatAvFrame* frame);
    int getFps() const { return mFps; }

protected:
    MediaSource(UsageEnvironment* env);
    virtual void readFrame() = 0;
    void setFps(int fps) { mFps = fps; }

private:
    static void taskCallback(void*);

protected:
    UsageEnvironment* mEnv;
    NatAvFrame mAVFrames[NAT_FRAME_NUM];
    std::queue<NatAvFrame*> mAVFrameInputQueue;
    std::queue<NatAvFrame*> mAVFrameOutputQueue;
    Mutex* mMutex;
    ThreadPool::Task mTask;
    int mFps;
};

class VideoSource : public MediaSource
{
public:
    static VideoSource* createNew(UsageEnvironment* env, int chn, int fps);
    
    VideoSource(UsageEnvironment* env, int chn, int fps);
    ~VideoSource();

protected:
    virtual void readFrame();
	uint8_t* FindNalu(uint8_t *buff, int len, int *size);
	
private:
	int channel;
};

class AudioSource : public MediaSource
{
public:
    static AudioSource* createNew(UsageEnvironment* env, int chn);
    
    AudioSource(UsageEnvironment* env, int chn);
    ~AudioSource();

protected:
    virtual void readFrame();
	
private:
	int channel;
};

#endif