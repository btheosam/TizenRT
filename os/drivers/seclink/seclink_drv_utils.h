#ifndef _SECLINK_DRIVER_UTILS_H__
#define _SECLINK_DRIVER_UTILS_H__
#ifndef LINUX
#include <debug.h>
#endif

#ifndef LINUX
#define SLDRV_LOG vdbg
#else
#define SLDRV_LOG printf
#endif

#define SLDRV_TAG "[SECLINK_DRV]"

#define SLDRV_ERR(fd)													\
	do {																\
		SLDRV_LOG(SL_TAG"[ERR:%s] %s %s:%d ret(%d) code(%s)\n",			\
				  SL_TAG, __FUNCTION__, __FILE__, __LINE__, fd, strerror(errno)); \
	} while (0)

#define SLDRV_ENTER														\
	do {																\
		SLDRV_LOG(SLDRV_TAG"%s\t%s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
	} while (0)


#endif // _SECLINK_DRIVER_UTILS_H__