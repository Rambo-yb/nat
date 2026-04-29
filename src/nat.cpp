#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <string>

#include "rtc/rtc.h"
#include "openssl/pem.h"

#include "cjson_common.h"
#include "check_common.h"

#include "nat.h"

#define NAT_LIB_VERSION ("V1.0.0")
#define NAT_DEFAULT_LOGS_PATH "/data/logs"

#define NAT_SIGNALING_SERVER_URL "ws://10.42.0.201:8765"

typedef struct {
	int pc;
	int dc;
}NatClientInfo;

typedef struct {
	char serial[128];
	int server_websocket;
	std::map<std::string, NatClientInfo> client_map;
    pthread_mutex_t mutex;
}NatMng;
static NatMng kNatMng = {.mutex = PTHREAD_MUTEX_INITIALIZER};

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


static void NatRtcDescriptionCb(int pc, const char *sdp, const char *type, void *ptr) {
	LOG_INFO("Description %s :\n %s", type, sdp);
	pthread_mutex_lock(&kNatMng.mutex);
	for(auto &i : kNatMng.client_map ) {
		if (i.second.pc == pc) {
			cJSON* json = cJSON_CreateObject();
			CHECK_POINTER(json, break);

			CJSON_SET_STRING(json, "type", type, cJSON_free(json);break);
			CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);break);
			CJSON_SET_STRING(json, "target", i.first.c_str(), cJSON_free(json);break);

			char encrypt_sdp[1024*4] = {0};
			NatEncryptBase64((const unsigned char*)sdp, strlen(sdp), encrypt_sdp, sizeof(encrypt_sdp));
			CJSON_SET_STRING(json, "sdp", encrypt_sdp, cJSON_free(json);break);

			char* buff = cJSON_Print(json);
			CHECK_POINTER(buff, cJSON_free(json);break);
			rtcSendMessage(kNatMng.server_websocket, buff, strlen(buff));
			free(buff);
			cJSON_free(json);
			
			break;
		}
	}
	pthread_mutex_unlock(&kNatMng.mutex);
}

static void NatRtcCandidateCb(int pc, const char *cand, const char *mid, void *ptr) {
	LOG_INFO("Candidate : %s [%s]", cand, mid);
	pthread_mutex_lock(&kNatMng.mutex);
	for(auto &i : kNatMng.client_map ) {
		if (i.second.pc == pc) {
			cJSON* json = cJSON_CreateObject();
			CHECK_POINTER(json, break);

			CJSON_SET_STRING(json, "type", "candidate", cJSON_free(json);break);
			CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);break);
			CJSON_SET_STRING(json, "target", i.first.c_str(), cJSON_free(json);break);
			CJSON_SET_STRING(json, "candidate", cand, cJSON_free(json);break);
			CJSON_SET_STRING(json, "mid", mid != NULL ? mid : "", cJSON_free(json);break);

			char* buff = cJSON_Print(json);
			CHECK_POINTER(buff, cJSON_free(json);break);
			rtcSendMessage(kNatMng.server_websocket, buff, strlen(buff));
			free(buff);
			cJSON_free(json);
			
			break;
		}
	}
	pthread_mutex_unlock(&kNatMng.mutex);
}

static void NatRtcStateChangeCb(int pc, rtcState state, void *ptr) {
	LOG_INFO("State : %d", state);
}

static void NatRtcIceStateChangeCb(int pc, rtcIceState state, void *ptr) {
	LOG_INFO("Ice state : %d", state);
}

static void NatRtcGatheringStateCb(int pc, rtcGatheringState state, void *ptr) {
	LOG_INFO("Gathering state : %d", state);
}

static void RTC_API dataChannelCallback(int pc, int dc, void *ptr) {
	pthread_mutex_lock(&kNatMng.mutex);
	for(auto &i : kNatMng.client_map ) {
		if (i.second.pc == pc) {
			i.second.dc = dc;
			rtcSetClosedCallback(dc, NatRtcCloseCb);
			rtcSetMessageCallback(dc, NatRtcMessageCb);
			break;
		}
	}
	pthread_mutex_unlock(&kNatMng.mutex);
}

static void NatRtcOpenCb(int id, void* user_ptr) {
	cJSON* json = cJSON_CreateObject();
	CHECK_POINTER(json, return );

	CJSON_SET_STRING(json, "type", "register", cJSON_free(json);return );
	CJSON_SET_STRING(json, "serial", kNatMng.serial, cJSON_free(json);return );

	char* buff = cJSON_Print(json);
    CHECK_POINTER(buff, cJSON_free(json);return );

	// todo 注册失败处理
	rtcSendMessage(id, buff, -1);
	free(buff);
	cJSON_free(json);
}

static void NatRtcCloseCb(int id, void* user_ptr) {

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
	rtcSetIceStateChangeCallback(cli_info->pc, NatRtcIceStateChangeCb);
	rtcSetGatheringStateChangeCallback(cli_info->pc, NatRtcGatheringStateCb);
	rtcSetDataChannelCallback(cli_info->pc, dataChannelCallback);	

}

static void NatWsMessageProc(int id, const char* message, int size) {
	char resp[256] = {0};
	snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"json parse fail\"}");

	char type[64] = {0};
	char target[128] = {0};

	cJSON* json = NULL;
	json = cJSON_Parse(message);
    CHECK_POINTER(json, goto err);

    CJSON_GET_STRING(json, "type", type, sizeof(type), goto err);
	CJSON_GET_STRING(json, "target", target, sizeof(target), goto err);

	if (strcmp(type, "connect") == 0) {
		NatClientInfo cli_info;
		memset(&cli_info, 0, sizeof(NatClientInfo));

		pthread_mutex_lock(&kNatMng.mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			LOG_ERR("client [%s] exist !", target);
			snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"client exist\"}");
			goto err;
		}
		pthread_mutex_unlock(&kNatMng.mutex);

		NatPeerConnection(&cli_info);

		pthread_mutex_lock(&kNatMng.mutex);
		kNatMng.client_map.insert({target, cli_info});
		pthread_mutex_unlock(&kNatMng.mutex);
	} else if (strcmp(type, "offer") == 0 || strcmp(type, "answer") == 0) {
		char sdp[1024*4] = {0};
	    CJSON_GET_STRING(json, "sdp", sdp, sizeof(sdp), goto err);
		
		char decrypt_sdp[1024*4] = {0};
		int ret = NatDecryptBase64(sdp, (unsigned char*)decrypt_sdp, sizeof(decrypt_sdp));
		CHECK_LE(ret, 0, snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"sdp decrypt fail\"}");goto err);

		pthread_mutex_lock(&kNatMng.mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			rtcSetRemoteDescription(kNatMng.client_map[target].pc, decrypt_sdp, type);
		} else {
			LOG_ERR("client [%s] not found !", target);
			snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"client not found\"}");
			goto unlock_err;
		}
		pthread_mutex_unlock(&kNatMng.mutex);
	} else if (strcmp(type, "candidate") == 0) {
		char mid[16] = {0};
		char candidate[256] = {0};
	    CJSON_GET_STRING(json, "candidate", candidate, sizeof(candidate), goto err);
	    CJSON_GET_STRING(json, "mid", mid, sizeof(mid), goto err);

		pthread_mutex_lock(&kNatMng.mutex);
		if (kNatMng.client_map.find(target) != kNatMng.client_map.end()) {
			rtcAddRemoteCandidate(kNatMng.client_map[target].pc, candidate, strlen(mid) > 0 ? mid : NULL);
		} else {
			LOG_ERR("client [%s] not found !", target);
			snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"client not found\"}");
			goto unlock_err;
		}
		pthread_mutex_unlock(&kNatMng.mutex);
	} else if (strcmp(type, "response") == 0) {
		// todo
		cJSON_free(json);
		return ;
	} else {
		snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":400, \"message\":\"unknown type: %s\"}", type);
		goto err;
	}

	cJSON_free(json);

	snprintf(resp, sizeof(resp), "{\"type\":\"response\", \"code\":200, \"message\":\"success\"}");
	rtcSendMessage(id, resp, -1);
	return ;
unlock_err:
	pthread_mutex_unlock(&kNatMng.mutex);
err:
	if (json != NULL) {
		cJSON_free(json);
	}
	rtcSendMessage(id, resp, -1);
}

static void NatDcMessageProc(int id, const char* message, int size) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		perror("connect: ");
		close(fd);
		return ;
	}

	int len = send(fd, message, strlen(message), 0);
	if (len <= 0) {

	}

	char buff[1024*64] = {0};
	len = recv(fd, buff, sizeof(buff), 0);
	if (len <= 0) {

	}

	close(fd);
	printf("buff: %d\n%s\n",  len, buff);
	rtcSendMessage(id, buff, len);
}

static void NatRtcMessageCb(int id, const char* message, int size, void* user_ptr) {
	LOG_WRN("message:%s", message);
	if (id == kNatMng.server_websocket) {
		NatWsMessageProc(id, message, size);
	} else {
		NatDcMessageProc(id, message, size);
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

int NatInit(NatInitialInfo* info) {
	char file_path[256] = {0};
	const char* log_path = (info == NULL || info->log_path == NULL) ? NAT_DEFAULT_LOGS_PATH : info->log_path;
	if (access(log_path, F_OK)) {
		NatCreatePath(log_path);
	}
	snprintf(file_path, sizeof(file_path), "%s/nat.log", log_path);
	LogInit(file_path, 512*1024, 3, 3);

	rtcInitLogger(RTC_LOG_INFO, NatRtcLogCb);

	snprintf(kNatMng.serial, sizeof(kNatMng.serial), "sn1234567890");

	kNatMng.server_websocket = rtcCreateWebSocket(NAT_SIGNALING_SERVER_URL);
	rtcSetOpenCallback(kNatMng.server_websocket, NatRtcOpenCb);
	rtcSetClosedCallback(kNatMng.server_websocket, NatRtcCloseCb);
	rtcSetMessageCallback(kNatMng.server_websocket, NatRtcMessageCb);

	return 0;
}

void NatUnInit() {

}
