#ifndef _LOGGING_H_
#define _LOGGING_H_

#include "log.h"

#define LOG_ERROR(format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define LOG_WARNING(format, ...) LOG_WRN(format, ##__VA_ARGS__)

#define LOG_DEBUGGING(format, ...) LOG_DEBUG(format, ##__VA_ARGS__)


#endif //_LOGING_H_