#ifndef __NAT_CONF_H__
#define __NAT_CONF_H__

#ifdef __cplusplus
extern "C" {
#endif

int NatConfInit(const char* conf_path);

int NatConfUninit();

void* NatConfGetConfig(const char* key);

#ifdef __cplusplus
}
#endif

#endif