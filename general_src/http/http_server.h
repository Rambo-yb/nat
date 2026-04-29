#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief http url处理回调
 * @param [in] c http 连接句柄
 * @param [in] data http 消息数据
 */
typedef void (*HttpServerUrlProcCb) (void* c, void* data);

/**
 * @brief http server初始化函数
 * @param [in] addr http服务器监听地址，http://addr:port
 * @return 成功返回0
 *         失败返回其他值
*/
int HttpServerInit(char *addr);

/**
 * @brief 释放http server函数
*/
void HttpServerUnInit();

/**
 * @brief http url注册函数
 * @param [in] method http请求类型，"POST" / "GET" ...
 * @param [in] uri http请求地址
 * @param [in] cb http请求处理回调
 * @return
 */
void HttpServerUrlRegister(char* method, char* uri, HttpServerUrlProcCb cb);

/**
 * @brief http 获取请求方式
 * @param [in] data http 回调传出的消息数据
 * @param [out] method 请求方式
 * @param [in] size method buff空间大小
 */
void HttpServerGetMethod(void* data, char* method, int size);

/**
 * @brief http 获取uri
 * @param [in] data http 回调传出的消息数据
 * @param [out] uri uri
 * @param [in] size uri buff空间大小
 */
void HttpServerGetUri(void* data, char* uri, int size);

/**
 * @brief http 获取消息头
 * @param [in] data http 回调传出的消息数据
 * @param [out] body 消息头
 * @param [in] size 消息头buff空间大小
 */
void HttpServerGetHead(void* data, char* head, int size);

/**
 * @brief http 获取消息体
 * @param [in] data http 回调传出的消息数据
 * @param [out] body 消息体
 * @param [in] size 消息体buff空间大小
 */
void HttpServerGetBody(void* data, char* body, int size);

/**
 * @brief http 获取get参数
 * @param [in] data http 回调传出的消息数据
 * @param [out] query get参数
 * @param [in] size 消息体buff空间大小
 */
void HttpServerGetQuery(void* data, char* query, int size);

typedef struct {
	char name[64];
	char filename[128];
	char* value;
	int value_size;
}HttpServerMultipart;

/**
 * @brief http 获取Multipart参数
 * @param [in] data http 回调传出的消息数据
 * @param [out] part Multipart参数，需要先释放part->value空间，在释放part空间
 * @param [out] size Multipart参数个数
 * @return 成功返回0
 *         失败返回其他值
 */
int HttpServerGetMultipart(void* data, HttpServerMultipart** part, int* len);

/**
 * @brief http 回复函数
 * @param [in] c http 回调传出的连接句柄
 * @param [in] code 状态码
 * @param [in] header 回复的自定义头
 * @param [in] body 回复的消息体
 */
void HttpServerReplay(void* c, int code, char* header, char* body);

/**
 * @brief http 回复文件
 * @param [in] c http 回调传出的连接句柄
 * @param [in] data http 回调传出的消息数据
 * @param [in] path 文件路径 {path}/{name}
 */
void HttpServerReplayFile(void* c, void* data, char* path);

/**
 * @brief http 获取客户端ip
 * @param [in] c http 回调传出的连接句柄
 * @param [in] addr 客户端地址
 * @param [in] size 地址空间大小
 */
void HttpServerGetClientAddr(void* c, char* addr, int size);

#ifdef __cplusplus
};
#endif

#endif