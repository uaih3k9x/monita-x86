/* fw/app_x86.c 和 fw/x86_plat.c 之间的几个接口 */
#ifndef X86FW_H
#define X86FW_H
#include <stdint.h>

void x86fw_set_time(uint32_t unix_s, int tz_min);   /* HELLO 带的时间 */
extern char x86fw_last_log[32];                      /* 最近一条日志 (PC_STAT) */
void x86fw_dead(void);                               /* 客户机没法继续: 停在这里只应答链路, 不返回 */
#endif
