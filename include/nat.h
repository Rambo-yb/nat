#ifndef __NAT_H__
#define __NAT_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char* log_path;			///< 日志路径, 【NULL:"/data/logs"】
	const char* conf_path;			///< 配置路径, 【NULL:"/data/confs"】
}NatInitialInfo;
/**
 * @brief NAT初始化
 * @param [in] info NAT初始化需要的初始信息
 * @return 成功返回 0
 *         失败返回 其他值
*/
int NatInit(NatInitialInfo* info);

/**
 * @brief NAT反初始化
*/
void NatUnInit();

#ifdef __cplusplus
}
#endif

#endif