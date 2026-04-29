#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include "http_server.h"
#include "mongoose.h"
#include "list.h"
#include "cjson_common.h"

typedef struct 
{
    char method[16];
    char url[256];
    HttpServerUrlProcCb cb;
}HttpServerUrlInfo;

typedef struct {
    pthread_mutex_t mutex;
    pthread_t pthread_id;
    void* url_info_list;
    struct mg_mgr mgr;
    struct mg_connection *con;
}HttpServerMng;
static HttpServerMng kHttpServerMng = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static void cb(struct mg_connection *c, int ev, void *ev_data) {
    
    if (ev == MG_EV_HTTP_MSG) {
        int i = 0;
        struct mg_http_message *hm = ev_data;

        int list_size = ProtListSize(kHttpServerMng.url_info_list);
        pthread_mutex_lock(&kHttpServerMng.mutex);
        for(; i < list_size; i++) {
            HttpServerUrlInfo* info = ProtListGet(kHttpServerMng.url_info_list, i);
            if (info != NULL 
                && mg_match(hm->uri, mg_str(info->url), NULL) 
                && mg_match(hm->method, mg_str(info->method), NULL)) {
                LOG_INFO("method:%.*s, uri:%.*s, query:%.*s, proto:%.*s, body:%.*s", 
                    hm->method.len, hm->method.buf, hm->uri.len, hm->uri.buf, 
                    hm->query.len, hm->query.buf, hm->proto.len, hm->proto.buf, hm->body.len, hm->body.buf);
                info->cb(c, ev_data);
                
                break;
            }
        }
        
        pthread_mutex_unlock(&kHttpServerMng.mutex);

        if(i >= list_size) {
            struct mg_http_serve_opts opts;
            memset(&opts, 0, sizeof(opts));
            opts.root_dir = "web_root";
            mg_http_serve_dir(c, ev_data, &opts);
            // mg_http_reply(c, 501, "", "{\"code\":501, \"message\":\"request not support\", \"data\":\"\"}");
        }
    }
}

static void* HttpServerProc(void* arg) {
    mg_mgr_init(&kHttpServerMng.mgr);
    kHttpServerMng.con = mg_http_listen(&kHttpServerMng.mgr, arg, cb, &kHttpServerMng.mgr);

    while(1){
        mg_mgr_poll(&kHttpServerMng.mgr, 1000);
    }
}

int HttpServerInit(char *addr) {
    kHttpServerMng.url_info_list = ProtListCreate();
    if(kHttpServerMng.url_info_list == NULL) {
        return -1;
    }

    int ret = pthread_create(&kHttpServerMng.pthread_id, NULL, HttpServerProc, addr);
    return ret;
}

void HttpServerUnInit() {
    pthread_cancel(kHttpServerMng.pthread_id);
    pthread_join(kHttpServerMng.pthread_id, NULL);

    mg_mgr_free(&kHttpServerMng.mgr);
    ProtListDestory(kHttpServerMng.url_info_list);
}

void HttpServerUrlRegister(char* method, char* url, HttpServerUrlProcCb cb) {
    HttpServerUrlInfo info;
    snprintf(info.method, sizeof(info.method), "%s", method);
    snprintf(info.url, sizeof(info.url), "%s", url);
    info.cb = cb;

    pthread_mutex_lock(&kHttpServerMng.mutex);
    ProtListPush(kHttpServerMng.url_info_list, &info, sizeof(HttpServerUrlInfo));
    pthread_mutex_unlock(&kHttpServerMng.mutex);
}

void HttpServerGetMethod(void* data, char* method, int size) {
    CHECK_POINTER(data, return);
    CHECK_POINTER(method, return);
    struct mg_http_message *hm = (struct mg_http_message *)data;
    snprintf(method, size, "%.*s", hm->method.len, hm->method.buf);
}

void HttpServerGetUri(void* data, char* uri, int size) {
    CHECK_POINTER(data, return);
    CHECK_POINTER(uri, return);
    struct mg_http_message *hm = (struct mg_http_message *)data;
    snprintf(uri, size, "%.*s", hm->uri.len, hm->uri.buf);
}

void HttpServerGetHead(void* data, char* head, int size) {
    CHECK_POINTER(data, return);
    CHECK_POINTER(head, return);
    struct mg_http_message *hm = (struct mg_http_message *)data;
    snprintf(head, size, "%.*s", hm->head.len, hm->head.buf);
}

void HttpServerGetBody(void* data, char* body, int size) {
    CHECK_POINTER(data, return);
    CHECK_POINTER(body, return);
    struct mg_http_message *hm = (struct mg_http_message *)data;
    snprintf(body, size, "%.*s", hm->body.len, hm->body.buf);
}

void HttpServerGetQuery(void* data, char* query, int size) {
    CHECK_POINTER(data, return);
    CHECK_POINTER(query, return);
    struct mg_http_message *hm = (struct mg_http_message *)data;
    snprintf(query, size, "%.*s", hm->query.len, hm->query.buf);
}

int HttpServerGetMultipart(void* data, HttpServerMultipart** part, int* len) {
    CHECK_POINTER(data, return -1);
    CHECK_POINTER(part, return -1);
    CHECK_POINTER(len, return -1);
    struct mg_http_message *hm = (struct mg_http_message *)data;

	int l = 0;
	int pos = 0;
	struct mg_http_part p;
	while((pos = mg_http_next_multipart(hm->body, pos, &p)) > 0) {
		l++;
	}

	HttpServerMultipart* _part = (HttpServerMultipart*)malloc(sizeof(HttpServerMultipart)*l);
	CHECK_POINTER(_part, return -1);

	pos = 0;
	int i = 0;
	while((pos = mg_http_next_multipart(hm->body, pos, &p)) > 0) {
    	snprintf(_part[i].name, sizeof(_part[i].name), "%.*s", p.name.len, p.name.buf);
    	snprintf(_part[i].filename, sizeof(_part[i].filename), "%.*s", p.filename.len, p.filename.buf);
		_part[i].value = (char*)malloc(p.body.len + 1);
		memset(_part[i].value, 0, p.body.len + 1);
		CHECK_POINTER(_part[i].value, goto end);
		memcpy(_part[i].value, p.body.buf, p.body.len);
		_part[i].value_size = p.body.len;
		i++;
	}
	*len = i;
	*part = _part;

	return 0;
end:
	if (_part != NULL)
	{
		for(; i >= 0; i--) {
			if (_part[i].value != NULL) {
				free(_part[i].value);
			}
		}

		free(_part);
	}
	return -1;
}

void HttpServerReplay(void* c, int code, char* header, char* body){
    mg_http_reply(c, code, header, body);
}

void HttpServerReplayFile(void* c, void* data, char* path) {
	struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    mg_http_serve_file(c, data, path, &opts);
}

void HttpServerGetClientAddr(void* c, char* addr, int size) {
	CHECK_POINTER(c, return);
	CHECK_POINTER(addr, return);
	
	struct mg_connection *conn = (struct mg_connection *)c;
	snprintf(addr, size, "%d.%d.%d.%d", conn->rem.ip[0], conn->rem.ip[1], conn->rem.ip[2], conn->rem.ip[3]);
}