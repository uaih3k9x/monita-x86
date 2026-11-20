/*
 * R05 外挂 flash (EFS:) 只读接口。存在性: R05 导出表按名字命中, 原厂 app 在用;
 * 参数按 Quectel QuecOpen ql_api_fs.h 惯例, cs80-extflash 的 extdump 在 R05 实机上用这几个读完了整个 EFS (两遍一致)。
 * 这里故意只声明读类接口: 写 / 建目录 / 改名 / 删除 / 格式化一个都没有 (validate.py 也拦)。
 */
#ifndef R05_FS_RO_H
#define R05_FS_RO_H
#include <stdint.h>

extern int ql_spi6_ext_nor_flash_init(void);              /* 0 = 成功 */
extern int ql_spi6_ext_nor_flash_sffs_mount(void);        /* 0 = 成功; 内部 mount(0), 挂不上也不格式化。同次上电只调一次 */

typedef int QFILE;                                        /* > 0 有效 */
extern QFILE ql_fopen(const char *path, const char *mode);    /* 本固件只用 "rb" */
extern int   ql_fread(void *buf, uint32_t size, uint32_t count, QFILE fd);  /* size = 1 时返回字节数 */
extern int   ql_fseek(QFILE fd, int32_t offset, int32_t whence);           /* 0 = 成功 */
extern int   ql_fclose(QFILE fd);
extern int   ql_fsize(QFILE fd);
#define QL_SEEK_SET 0
#endif
