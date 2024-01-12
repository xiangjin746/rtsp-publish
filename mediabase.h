#ifndef MEDIABASE_H
#define MEDIABASE_H

#include <map>
#include <vector>
#include <string>
#include <string.h>

enum RET_CODE
{
    RET_ERR_UNKNOWN = -2,                   // 未知错误
    RET_FAIL = -1,							// 失败
    RET_OK	= 0,							// 正常
    RET_ERR_OPEN_FILE,						// 打开文件失败
    RET_ERR_NOT_SUPPORT,					// 不支持
    RET_ERR_OUTOFMEMORY,					// 没有内存
    RET_ERR_STACKOVERFLOW,					// 溢出
    RET_ERR_NULLREFERENCE,					// 空参考
    RET_ERR_ARGUMENTOUTOFRANGE,				//
    RET_ERR_PARAMISMATCH,					//
    RET_ERR_MISMATCH_CODE,                  // 没有匹配的编解码器
    RET_ERR_EAGAIN,
    RET_ERR_EOF
};

#endif // MEDIABASE_H
