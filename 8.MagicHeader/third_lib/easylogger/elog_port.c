
#include <elog.h>
#include "tim_delay.h"
#include "console.h"
#include <string.h>
#include <stdio.h>
ElogErrCode elog_port_init(void)
{
    ElogErrCode result = ELOG_NO_ERR;

    return result;
}

void elog_port_output(const char *log, size_t size)
{
    console_write(log, size);
}

void elog_port_output_lock(void)
{
}

void elog_port_output_unlock(void)
{
}

const char *elog_port_get_time(void)
{
    static char time_str[16] = {0};
    uint64_t total_ms = tim_get_ms();
    uint32_t ms = total_ms % (3600 * 1000);      // 1小时内的毫秒数
    uint32_t fmt_mm = ms / (60 * 1000);          // 分钟
    uint32_t fmt_ss = (ms % (60 * 1000)) / 1000; // 秒
    uint32_t fmt_ms = ms % 1000;                 // 毫秒

    snprintf(time_str, sizeof(time_str), "%02u:%02u:%03u", fmt_mm, fmt_ss, fmt_ms);
    return time_str;
}

const char *elog_port_get_p_info(void)
{
    return "";
}

const char *elog_port_get_t_info(void)
{
    return "";
}
