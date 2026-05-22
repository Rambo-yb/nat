#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <string>
#include <vector>

#include "rtc/rtc.h"
#include "openssl/pem.h"
#include "openssl/rand.h"

#include "cjson_common.h"
#include "check_common.h"
#include "http_client.h"

#include "nat.h"
#include "nat_conf.h"
#include "nat_media.h"

#define NAT_LIB_VERSION ("V1.0.0")
#define NAT_DEFAULT_LOGS_PATH "/data/logs"
#define NAT_DEFAULT_CONFS_PATH "/data/confs"

// #define NAT_SIGNALING_SERVER_URL "ws://10.42.0.201:8765"
#define NAT_SIGNALING_SERVER_URL "ws://8.136.196.77:8765"

#define NAT_MAX_CHN_NUM 2

typedef struct {
	int pc;
	int dc;
	int tr[NAT_MAX_CHN_NUM][2];
}NatClientInfo;

typedef struct {
	char serial[128];
	int ws;
	int ws_connect_status;
	int ws_connect_fail;
	pthread_t pthread_server_mng;

	std::map<std::string, NatClientInfo> client_map;
    pthread_mutex_t client_mutex;

	pthread_t pthread_media_proc;
    pthread_mutex_t media_mutex;
	std::map<int, NatMediaSession*> media_map; 
}NatMng;
static NatMng kNatMng = {.client_mutex = PTHREAD_MUTEX_INITIALIZER};

static void NatRtcMessageCb(int id, const char* message, int size, void* user_ptr);
static void NatRtcCloseCb(int id, void* user_ptr);

static int NatDecryptBase64(const char* input, unsigned char* output, int out_size) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(input, -1);
    bmem = BIO_push(b64, bmem);
    
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    size_t len = strlen(input);
    unsigned char* buffer = (unsigned char*) malloc(len);
    len = BIO_read(bmem, buffer, strlen(input));

	CHECK_LT(out_size, len, free(buffer);BIO_free_all(bmem);return -1);
	memcpy(output, buffer, len);

	free(buffer);
    BIO_free_all(bmem);
    return len;
}

static int NatEncryptBase64(const unsigned char* in, int in_size, char* out, int out_size) {
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    b64 = BIO_push(b64, bmem);
    
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, in, in_size);
    BIO_flush(b64);
    
    BUF_MEM* bptr = NULL;
    BIO_get_mem_ptr(b64, &bptr);
    
	CHECK_LT(out_size, bptr->length, BIO_free_all(b64);return -1);
    memcpy(out, bptr->data, bptr->length);
    int len = bptr->length;
	
    BIO_free_all(b64);
    return len;
}

static int NatUuid(char* uuid, int size) {
	unsigned char buff[16] = {0};
	CHECK_EQ(RAND_bytes(buff, sizeof(buff)), 1, return -1);
	
	buff[6] = (buff[6] & 0x0F) | 0x40;
	buff[8] = (buff[8] & 0x3F) | 0x80;

	snprintf(uuid, size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             buff[0], buff[1], buff[2], buff[3],
             buff[4], buff[5], buff[6], buff[7],
             buff[8], buff[9], buff[10], buff[11],
             buff[12], buff[13], buff[14], buff[15]);
	return 0;
}

static void NatRtcDescriptionCb(int pc, const char *sdp, const char *type, void *ptr) {
	LOG_INFO("Description %s :\n %s", type, sdp);
	pthread_mutex_lock(&kNatMng.client_mutex);
	for(auto &i : kNatMng.client_map ) {
		if (i.second.pc == pc) {
			cJSON* json = cJSON_CreateObject();
			CHECK_POINTER(json, break);

			char session[64] = {0};
			NatUuid(session, sizeof(session));

			CJSON_SET_STRING(json, "type", type, cJSON_free(json);break);
			CJSON_SET_STRING(json, "session_id", session, cJSON_free(json);break);
			CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);break);
			CJSON_SET_STRING(json, "target", i.first.c_str(), cJSON_free(json);break);

			char encrypt_sdp[1024*4] = {0};
			NatEncryptBase64((const unsigned char*)sdp, strlen(sdp), encrypt_sdp, sizeof(encrypt_sdp));
			CJSON_SET_STRING(json, "sdp", encrypt_sdp, cJSON_free(json);break);

			char* buff = cJSON_PrintUnformatted(json);
			CHECK_POINTER(buff, cJSON_free(json);break);
			rtcSendMessage(kNatMng.ws, buff, strlen(buff));
			free(buff);
			cJSON_free(json);
			
			break;
		}
	}
	pthread_mutex_unlock(&kNatMng.client_mutex);
}

static void NatRtcCandidateCb(int pc, const char *cand, const char *mid, void *ptr) {
	LOG_INFO("Candidate : %s [%s]", cand, mid);
	pthread_mutex_lock(&kNatMng.client_mutex);
	for(auto &i : kNatMng.client_map ) {
		if (i.second.pc == pc) {
			cJSON* json = cJSON_CreateObject();
			CHECK_POINTER(json, break);

			char session[64] = {0};
			NatUuid(session, sizeof(session));

			CJSON_SET_STRING(json, "type", "candidate", cJSON_free(json);break);
			CJSON_SET_STRING(json, "session_id", session, cJSON_free(json);break);
			CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);break);
			CJSON_SET_STRING(json, "target", i.first.c_str(), cJSON_free(json);break);
			CJSON_SET_STRING(json, "candidate", cand, cJSON_free(json);break);
			CJSON_SET_STRING(json, "mid", mid != NULL ? mid : "", cJSON_free(json);break);

			char* buff = cJSON_PrintUnformatted(json);
			CHECK_POINTER(buff, cJSON_free(json);break);
			rtcSendMessage(kNatMng.ws, buff, strlen(buff));
			free(buff);
			cJSON_free(json);
			
			break;
		}
	}
	pthread_mutex_unlock(&kNatMng.client_mutex);
}

static void NatRtcStateChangeCb(int pc, rtcState state, void *ptr) {
	LOG_INFO("State : %d", state);
	if (state == RTC_CLOSED) {
		pthread_mutex_lock(&kNatMng.client_mutex);
		for(auto &i : kNatMng.client_map ) {
			if (i.second.pc == pc) {
				for(int j = 0; j < NAT_MAX_CHN_NUM; j++) {
					if (i.second.tr[j][0] != 0) {
						pthread_mutex_lock(&kNatMng.media_mutex);
						kNatMng.media_map[j]->removeTrInstance(i.second.tr[j][0]);
						pthread_mutex_unlock(&kNatMng.media_mutex);
						rtcDeleteTrack(i.second.tr[j][0]);
					}
					if (i.second.tr[j][1] != 0){
						pthread_mutex_lock(&kNatMng.media_mutex);
						kNatMng.media_map[j]->removeTrInstance(i.second.tr[j][1]);
						pthread_mutex_unlock(&kNatMng.media_mutex);
						rtcDeleteTrack(i.second.tr[j][1]);
					}
				}
				pthread_mutex_unlock(&kNatMng.media_mutex);
				rtcDeleteDataChannel(i.second.dc);
				rtcDeletePeerConnection(i.second.pc);
				kNatMng.client_map.erase(i.first);
				break;
			}
		}
		pthread_mutex_unlock(&kNatMng.client_mutex);
	}
}

static void NatRtcOpenCb(int id, void* user_ptr) {
	if (id == kNatMng.ws) {
		kNatMng.ws_connect_status = 1;

		cJSON* json = cJSON_CreateObject();
		CHECK_POINTER(json, return );

		char session[64] = {0};
		NatUuid(session, sizeof(session));

		CJSON_SET_STRING(json, "type", "register", cJSON_free(json);return );
		CJSON_SET_STRING(json, "session_id", session, cJSON_free(json);return );
		CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);return );

		char* buff = cJSON_PrintUnformatted(json);
		CHECK_POINTER(buff, cJSON_free(json);return );

		rtcSendMessage(id, buff, -1);
		free(buff);
		cJSON_free(json);
	}
}

static void NatRtcCloseCb(int id, void* user_ptr) {
	if (kNatMng.ws == id) {
		kNatMng.ws_connect_status = 0;
		rtcDeleteWebSocket(kNatMng.ws);
		kNatMng.ws = 0;
	}
}

static void NatPeerConnection(NatClientInfo* cli_info) {
	rtcConfiguration config;
	memset(&config, 0, sizeof(config));
    const char* stun_server[1] = {"stun:stun.l.google.com:19302"};
    config.iceServers = stun_server;
    config.iceServersCount = 1;
    config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
    config.enableIceTcp = 0;
	cli_info->pc = rtcCreatePeerConnection(&config);
	
	rtcSetLocalDescriptionCallback(cli_info->pc, NatRtcDescriptionCb);
	rtcSetLocalCandidateCallback(cli_info->pc, NatRtcCandidateCb);
	rtcSetStateChangeCallback(cli_info->pc, NatRtcStateChangeCb);

	pthread_mutex_lock(&kNatMng.media_mutex);
	for(auto &i : kNatMng.media_map) {
		for (int j = 0; j < 2; j++) {
			NatMediaSession::TrackId track_id = (j == 0) ? NatMediaSession::TrackId0 : NatMediaSession::TrackId1;
			if(i.second->isSupport(track_id)) {
				std::string sdp = i.second->getDescription(track_id);
				cli_info->tr[i.first][j] = rtcAddTrack(cli_info->pc, sdp.c_str());
				rtcSetOpenCallback(cli_info->tr[i.first][j], NatRtcOpenCb);
				rtcSetClosedCallback(cli_info->tr[i.first][j], NatRtcCloseCb);
				rtcSetMessageCallback(cli_info->tr[i.first][j], NatRtcMessageCb);

				i.second->addTrInstance(track_id, cli_info->tr[i.first][j]);
			}
		}
	}
	pthread_mutex_unlock(&kNatMng.media_mutex);

	cli_info->dc = rtcCreateDataChannel(cli_info->pc, "chat");
	rtcSetOpenCallback(cli_info->dc, NatRtcOpenCb);
	rtcSetClosedCallback(cli_info->dc, NatRtcCloseCb);
	rtcSetMessageCallback(cli_info->dc, NatRtcMessageCb);
}

#define NAT_WS_MESSAGE_ERROR_RESP(r, c, m, s) \
	snprintf(r, sizeof(r), "{\"type\":\"response\", \"code\":%d, \"session_id\":\"%s\", \"message\":\"%s\"}", c, s, m)

static void NatWsMessageProc(int id, const char* message, int size) {
	char resp[256] = {0};
	NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "json parse fail", "unknown session id");


	char type[64] = {0};
	char target[128] = {0};
	char session_id[128] = {0};

	cJSON* json = NULL;
	json = cJSON_Parse(message);
    CHECK_POINTER(json, goto err);

    CJSON_GET_STRING(json, "session_id", type, sizeof(type), goto err);
	NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "json parse fail", session_id);

    CJSON_GET_STRING(json, "type", type, sizeof(type), goto err);
	CJSON_GET_STRING(json, "target", target, sizeof(target), goto err);

	if (strcmp(type, "connect") == 0) {
		NatClientInfo cli_info;
		memset(&cli_info, 0, sizeof(NatClientInfo));

		pthread_mutex_lock(&kNatMng.client_mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			LOG_ERR("client [%s] exist !", target);
			NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "client exist", session_id);
			snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"client exist\"}");
			goto err;
		}
		pthread_mutex_unlock(&kNatMng.client_mutex);

		pthread_mutex_lock(&kNatMng.client_mutex);
		kNatMng.client_map.insert({target, cli_info});
		pthread_mutex_unlock(&kNatMng.client_mutex);

		NatPeerConnection(&kNatMng.client_map[target]);
	} else if (strcmp(type, "offer") == 0 || strcmp(type, "answer") == 0) {
		char sdp[1024*4] = {0};
	    CJSON_GET_STRING(json, "sdp", sdp, sizeof(sdp), goto err);
		
		char decrypt_sdp[1024*4] = {0};
		int ret = NatDecryptBase64(sdp, (unsigned char*)decrypt_sdp, sizeof(decrypt_sdp));
		CHECK_LE(ret, 0, NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "sdp decrypt fail", session_id);goto err);

		pthread_mutex_lock(&kNatMng.client_mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			rtcSetRemoteDescription(kNatMng.client_map[target].pc, decrypt_sdp, type);
		} else {
			LOG_ERR("client [%s] not found !", target);
			NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "client not found", session_id);
			goto unlock_err;
		}
		pthread_mutex_unlock(&kNatMng.client_mutex);
	} else if (strcmp(type, "candidate") == 0) {
		char mid[16] = {0};
		char candidate[256] = {0};
	    CJSON_GET_STRING(json, "candidate", candidate, sizeof(candidate), goto err);
	    CJSON_GET_STRING(json, "mid", mid, sizeof(mid), goto err);

		pthread_mutex_lock(&kNatMng.client_mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			rtcAddRemoteCandidate(kNatMng.client_map[target].pc, candidate, strlen(mid) > 0 ? mid : NULL);
		} else {
			LOG_ERR("client [%s] not found !", target);
			NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "client not found", session_id);
			goto unlock_err;
		}
		pthread_mutex_unlock(&kNatMng.client_mutex);
	} else if (strcmp(type, "response") == 0) {
		// todo
		cJSON_free(json);
		return ;
	} else {
		NAT_WS_MESSAGE_ERROR_RESP(resp, 400, "unknown type", session_id);
		goto err;
	}

	cJSON_free(json);

	NAT_WS_MESSAGE_ERROR_RESP(resp, 200, "success", session_id);
	rtcSendMessage(id, resp, -1);
	return ;
unlock_err:
	pthread_mutex_unlock(&kNatMng.client_mutex);
err:
	if (json != NULL) {
		cJSON_free(json);
	}
	rtcSendMessage(id, resp, -1);
}

static int NatDcMessageProc(const char* in, int in_size, char* out, int out_size) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	CHECK_LE(fd, 0, return -1);

    struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	int ret = connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
	CHECK_LT(ret, 0, close(fd);return -1);

	ret = send(fd, in, in_size, 0);
	CHECK_LT(ret, 0, close(fd);return -1);

	ret = recv(fd, out, out_size, 0);
	CHECK_LT(ret, 0, close(fd);return -1);

	close(fd);
	return ret;
}

static void NatRtcMessageCb(int id, const char* message, int size, void* user_ptr) {
	LOG_DEBUG("%*s", size, message);
	if (id == kNatMng.ws) {
		NatWsMessageProc(id, message, size);
	} else {
		char buff[1024*64] = {0};
		int len = NatDcMessageProc(message, size <= 0 ? strlen(message) : size, buff, sizeof(buff));
		if (len > 0) {
			rtcSendMessage(id, buff, len);
		}
	}
}

static void* NatMediaProc(void* arg) {
	while (1) {
		for(int i = 0; i < NAT_MAX_CHN_NUM; i++) {
			NatAvFrame frame;
			frame.m_frame_size = NatMediaVideoPop(i, frame.m_buffer, NAT_FRAME_MAX_SIZE, &frame.m_pts);
			if (frame.m_frame_size > 0) {
				frame.m_frame = frame.m_buffer;
				
				pthread_mutex_lock(&kNatMng.media_mutex);
				kNatMng.media_map[i]->frameHandle(NatMediaSession::TrackId0, &frame);
				pthread_mutex_unlock(&kNatMng.media_mutex);
			}

			frame.Clear();
			frame.m_frame_size = NatMediaAudioPop(i, frame.m_buffer, NAT_FRAME_MAX_SIZE, &frame.m_pts);
			if (frame.m_frame_size > 0) {
				frame.m_frame = frame.m_buffer;
				pthread_mutex_lock(&kNatMng.media_mutex);
				kNatMng.media_map[i]->frameHandle(NatMediaSession::TrackId1, &frame);
				pthread_mutex_unlock(&kNatMng.media_mutex);
			}	
		}

		usleep(1*1000);
	}
}

static void* NatWebServerConnectMng(void* arg) {
	while (1) {
		if (kNatMng.ws_connect_status == 1) {
			kNatMng.ws_connect_fail = 0;
			sleep(5);
			continue;
		}

		sleep(kNatMng.ws_connect_fail * 60);

		if (kNatMng.ws != 0) {
			rtcDeleteWebSocket(kNatMng.ws);
		}

		kNatMng.ws = rtcCreateWebSocket(NAT_SIGNALING_SERVER_URL);
		rtcSetOpenCallback(kNatMng.ws, NatRtcOpenCb);
		rtcSetClosedCallback(kNatMng.ws, NatRtcCloseCb);
		rtcSetMessageCallback(kNatMng.ws, NatRtcMessageCb);

		sleep(2);
		kNatMng.ws_connect_fail++;
	}
}

static void NatRtcLogCb(rtcLogLevel level, const char* message) {
	switch (level) {
	case RTC_LOG_ERROR:
		LOG_ERR("%s", message);
		break;
	case RTC_LOG_WARNING:
		LOG_WRN("%s", message);
		break;
	case RTC_LOG_INFO:
		LOG_INFO("%s", message);
		break;
	case RTC_LOG_DEBUG:
		LOG_DEBUG("%s", message);
		break;
	default:
		break;
	}
}

static int NatCreatePath(const char* path) {
	char _p[256] = {0};
    strncpy(_p, path, sizeof(_p));
 
    for(int i = 0; i < strlen(_p); i++) {
		if (_p[i] == '/') {
			_p[i] = '\0';
			if (strlen(_p) > 0 && access(_p, F_OK) != 0) {
				mkdir(_p, 0755);
			}
			_p[i] = '/';
		}
    }
 
	if (strlen(_p) > 0 && access(_p, F_OK) != 0) {
		mkdir(_p, 0755);
	}
 
    return 0;
}

static int NatGetSerial() {
	cJSON* users_json = (cJSON*)NatConfGetConfig("users");
	CHECK_POINTER(users_json, return -1);
	CHECK_BOOL(cJSON_IsArray(users_json), cJSON_free(users_json);return -1);

	cJSON* item = cJSON_GetArrayItem(users_json, 0);

	char username[64] = {0};
	CJSON_GET_STRING(item, "username", username, sizeof(username), cJSON_free(users_json);return -1);
	char password[64] = {0};
	CJSON_GET_STRING(item, "password", password, sizeof(password), cJSON_free(users_json);return -1);
	cJSON_free(users_json);

	char auth[128+2] = {0};
	snprintf(auth, sizeof(auth), "%s:%s", username, password);

	char auth_enc[256] = {0};
	NatEncryptBase64((unsigned char*)auth, strlen(auth), auth_enc, sizeof(auth_enc));

	char header_auth[512] = {0};
	snprintf(header_auth, sizeof(header_auth), "Authorization: Basic %s", auth_enc);

	char* header[1] = {header_auth};

	char res[1024*5] = {0};
	int ret = HttpRequest("GET", "http://127.0.0.1:8080/dev_api/system_request?type=device_info", (const char**)header, 1, NULL, res, sizeof(res), 10*1000);
	CHECK_LT(ret, 0, return -1);

	cJSON* json = cJSON_Parse(res);
	CHECK_POINTER(json, return -1);
	
	int code = 0;
	char msg[256] = {0};
	CJSON_GET_NUMBER(json, "code", code, sizeof(code), cJSON_free(json);return -1);
	CJSON_GET_STRING(json, "message", msg, sizeof(msg), cJSON_free(json);return -1);
	CHECK_EQ(code, 200, LOG_ERR("%s", msg);cJSON_free(json);return -1);

	cJSON* info = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json, "data"), "device_info");
	CHECK_POINTER(info, cJSON_free(json);return -1);
	CJSON_GET_STRING(info, "serial_number", kNatMng.serial, sizeof(kNatMng.serial), cJSON_free(json);return -1);
	cJSON_free(json);
	return 0;
}

int NatInit(NatInitialInfo* info) {
	char file_path[256] = {0};
	const char* log_path = (info == NULL || info->log_path == NULL) ? NAT_DEFAULT_LOGS_PATH : info->log_path;
	if (access(log_path, F_OK)) {
		NatCreatePath(log_path);
	}
	snprintf(file_path, sizeof(file_path), "%s/nat.log", log_path);
	LogInit(file_path, 512*1024, 3, 3);

	rtcInitLogger(RTC_LOG_VERBOSE, NatRtcLogCb);

	NatConfInit((info == NULL || info->conf_path == NULL) ? NAT_DEFAULT_CONFS_PATH : info->conf_path);
	NatGetSerial();

	pthread_create(&kNatMng.pthread_server_mng, NULL, NatWebServerConnectMng, NULL);

	pthread_create(&kNatMng.pthread_media_proc, NULL, NatMediaProc, NULL);

	return 0;
}

void NatUnInit() {
    if (kNatMng.pthread_media_proc) {
        pthread_cancel(kNatMng.pthread_media_proc);
        pthread_join(kNatMng.pthread_media_proc, NULL);
    }

    if (kNatMng.pthread_server_mng) {
        pthread_cancel(kNatMng.pthread_server_mng);
        pthread_join(kNatMng.pthread_server_mng, NULL);
    }

	NatConfUninit();

	if (kNatMng.ws != 0) {
		rtcDeleteWebSocket(kNatMng.ws);
	}

	for(auto &i : kNatMng.client_map ) {
		rtcDeleteDataChannel(i.second.dc);
		rtcDeletePeerConnection(i.second.pc);
		kNatMng.client_map.erase(i.first);
	}

	for (auto &i : kNatMng.media_map) {
		delete i.second;
		kNatMng.media_map.erase(i.first);
	}
}

int NatSendMedia(int id, unsigned char *data, int size) {
	if (rtcIsOpen(id)) {
		rtcSendMessage(id, (const char*)data, size);
	}
	return 0;
}

void NatStreamingRegister(NatStreamingRegisterInfo* info, unsigned int size) {
	CHECK_LT(NAT_MAX_CHN_NUM, size, return );

	for(int i = 0; i < size; i++) {
		NatMediaSession* session = new NatMediaSession(info[i].chn);
		if (info[i].video_info.use) {
			NatRtpSink* sink = NULL;
			if (info[i].video_info.video_type == NAT_VIDEO_H264) {
				sink = new NatH264RtpSink(info[i].video_info.fps);
			} else if (info[i].video_info.video_type == NAT_VIDEO_H265) {
				sink = new NatH265RtpSink(info[i].video_info.fps);
			}
			session->addRtpSink(NatMediaSession::TrackId0, sink);
		}

		if (info[i].audio_info.use) {
			NatRtpSink* sink = NULL;
			if (info[i].audio_info.audio_type == NAT_AUDIO_AAC) {
				sink = new NatAACRtpSink(info[i].audio_info.sample_rate, info[i].audio_info.channels);
			} else if (info[i].audio_info.audio_type == NAT_AUDIO_G711A) {
				sink = new NatG711aRtpSink(info[i].audio_info.sample_rate, info[i].audio_info.channels);
			}
			session->addRtpSink(NatMediaSession::TrackId1, sink);
		}
		session->setSendPacketCb(NatSendMedia);
		kNatMng.media_map.insert({info[i].chn, session});
	}
}

int NatPushStream(NatPushStreamInfo* info) {
	if (info->frame_type == NAT_FRAME_VIDEO) {
		return NatMediaVideoPush(info->chn, info->buff, info->size, info->pts);
	} else if (info->frame_type == NAT_FRAME_AUDIO) {
		return NatMediaAudioPush(info->chn, info->buff, info->size, info->pts);
	}
	return 0;
}