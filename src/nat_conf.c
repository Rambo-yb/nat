#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "nat_conf.h"
#include "cjson_common.h"

#define PROTOCOL_CONFIG_FILE "protocol.conf"

typedef struct {
	char conf_path[256];

	cJSON* protocol_conf;
	pthread_t pthread_conf_monitor;
	pthread_mutex_t mutex;
}NatConfMng;
static NatConfMng kNatConfMng = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static cJSON* NatConfLoadFile(char* file) {
    FILE* fp = fopen(file, "r");
    CHECK_POINTER(fp, return NULL);

    CHECK_LT(fseek(fp, 0, SEEK_END), 0, fclose(fp);return NULL);
    int file_len = ftell(fp);
    CHECK_LT(fseek(fp, 0, SEEK_SET), 0, fclose(fp);return NULL);

    char* file_data = (char*)malloc(file_len+1);
    CHECK_POINTER(file_data, fclose(fp);return NULL);

    CHECK_EQ(fread(file_data, 1, file_len+1, fp), file_len, free(file_data);fclose(fp);return NULL);

    cJSON* json = cJSON_Parse(file_data);
    CHECK_POINTER(json, free(file_data);fclose(fp);return NULL);

    free(file_data);
    fclose(fp);
    return json;
}

static int NatConfSaveFile(char* file, cJSON* json) {
	char* conf = cJSON_Print(json);
	CHECK_POINTER(conf, return -1);

	FILE* fp = fopen(file, "w+");
	CHECK_POINTER(fp, free(conf);return -1);

	CHECK_EQ(fwrite(conf, 1, strlen(conf), fp), strlen(conf), fclose(fp);free(conf);return -1);

	fflush(fp);
	fclose(fp);
	free(conf);
	return 0;
}

static void NatConfigUserDefault(cJSON* json) {
	cJSON* user_json = cJSON_CreateObject();
	CHECK_POINTER(user_json, return );

	CJSON_SET_STRING(user_json, "username", "admin", free(user_json); return );
	CJSON_SET_STRING(user_json, "password", "123456", free(user_json); return );
	CJSON_SET_NUMBER(user_json, "user_type", 0, free(user_json); return );

	cJSON* sub_json = cJSON_CreateArray();
	CHECK_POINTER(sub_json, return );
	CHECK_BOOL(cJSON_AddItemToArray(sub_json, user_json), cJSON_free(user_json); cJSON_free(sub_json); return );

	CHECK_BOOL(cJSON_AddItemToObject(json, "users", sub_json), cJSON_free(sub_json); return );
}

static void NatConfDefaultProtocol() {
	cJSON* json = cJSON_CreateObject();
	CHECK_POINTER(json, return );

	NatConfigUserDefault(json);

	pthread_mutex_lock(&kNatConfMng.mutex);
	kNatConfMng.protocol_conf = json;

	char path[512] = {0};
	snprintf(path, sizeof(path), "%s/%s", kNatConfMng.conf_path, PROTOCOL_CONFIG_FILE);
	NatConfSaveFile(path, json);
	pthread_mutex_unlock(&kNatConfMng.mutex);
	return ;
}

static void* NatConfMonitorProc(void* arg) {
	char protocol_file[512] = {0};
	snprintf(protocol_file, sizeof(protocol_file), "%s/%s", kNatConfMng.conf_path, PROTOCOL_CONFIG_FILE);

	struct stat pre_protocol_file_stat = {0};
	stat(protocol_file, &pre_protocol_file_stat);

	while (1) {
		struct stat cur_file_stat = {0};
		if (stat(protocol_file, &cur_file_stat) == -1) {
			sleep(1);
			continue;
		}

		if (cur_file_stat.st_mtime != pre_protocol_file_stat.st_mtime) {
			cJSON* json = NatConfLoadFile(protocol_file);
			CHECK_POINTER(json, sleep(1);continue);

			pthread_mutex_lock(&kNatConfMng.mutex);
			if (kNatConfMng.protocol_conf != NULL) {
				cJSON_free(kNatConfMng.protocol_conf);
			}
			kNatConfMng.protocol_conf = json;
			pthread_mutex_unlock(&kNatConfMng.mutex);
			memcpy(&pre_protocol_file_stat, &cur_file_stat, sizeof(struct stat));
		}

		usleep(1000*1000);
	}
	
	return NULL;
}

int NatConfInit(const char* conf_path) {
	strcpy(kNatConfMng.conf_path, conf_path);

	char protocol_file[512] = {0};
	snprintf(protocol_file, sizeof(protocol_file), "%s/%s", conf_path, PROTOCOL_CONFIG_FILE);
	kNatConfMng.protocol_conf = NatConfLoadFile(protocol_file);
	if (kNatConfMng.protocol_conf == NULL) {
        LOG_WRN("load protocol.conf fail, use memory config !");
        NatConfDefaultProtocol();
    }

	int ret = pthread_create(&kNatConfMng.pthread_conf_monitor, NULL, NatConfMonitorProc, NULL);
	CHECK_EQ(ret, 0, return -1);

	return 0;
}

int NatConfUninit() {
    if (kNatConfMng.pthread_conf_monitor) {
        pthread_cancel(kNatConfMng.pthread_conf_monitor);
        pthread_join(kNatConfMng.pthread_conf_monitor, NULL);
    }

	if (kNatConfMng.protocol_conf) {
		cJSON_free(kNatConfMng.protocol_conf);
	}

	return 0;
}

void* NatConfGetConfig(const char* key) {
	cJSON* sub = cJSON_GetObjectItemCaseSensitive(kNatConfMng.protocol_conf, key);
	CHECK_POINTER(sub, return NULL);

	return (void*)cJSON_Duplicate(sub, cJSON_True);
}