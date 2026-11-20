# x86 固件的链路帧

帧格式和 Monita-CS80 固件一样（`fw/link/proto.h`）：`A5 5A | u8 type | u8 seq | u16 len | payload | u32 crc32`，全部小端，
crc 覆盖 type..payload。走 USB 接口 7（固件里是 `ql_uart_open(4)`）。设备只应答，不主动发。

| type | 方向 | 名字 | 载荷 | 应答 |
|---|---|---|---|---|
| 0x01 | → | HELLO | 空，或 `u32 unix_time` `i16 tz_min` | HELLO_ACK |
| 0x7F | → | PING | 空 | ACK |
| 0xA0 | → | PC_KBD | `u8 n`，n 组 `{u8 flags, u8 扫描码}`：flags bit0 按下、bit1 E0 扩展；扫描码是 set-1，不带 0x80 断码位 | 不应答 |
| 0xA1 | → | PC_MOUSE | `s16 dx, s16 dy`（向下为正）`s8 dz` `u8 buttons`（bit0 左、bit1 右、bit2 中） | 不应答 |
| 0xA2 | → | PC_CTRL | `u8 op [, u8 arg]`：1 硬复位（arg bit0 同时清空写覆盖层）、2 Ctrl+Alt+Del、3 暂停/继续、4 状态栏开关、5 静音（没有声卡，空操作） | ACK（status 0 / 1 = EPARAM） |
| 0xA3 | → | PC_STAT | 空 | PC_STAT_R |
| 0x81 | ← | HELLO_ACK | 16 字节：`u16 proto 0, u16 fw_ver, u16 800, u16 1280, u8 1, u8 0, u16 caps = 0x0800, u16 max_payload 256, u16 0` | |
| 0xE3 | ← | PC_STAT_R | 14 个 u32 + 32 字节日志，见 `core/x86proto.h` 的 `x86_stat_t` | |
| 0xFE | ← | ACK | 40 字节，布局同 Monita ACK：`u8 type, u8 status, 0, 0`，9 个 u32（第 7 个 = 坏载荷计数，第 9 个 = extra） | |

- `caps` 只有 bit11（X86_CAP），图像类的位全是 0：主机据此认出是 x86 固件，不要推像素。
- 推像素类的帧（RECT / COPY / IMM_RECT / RECT_LZ4 / AUDIO_DATA）按 Monita 协议本来就不应答，这里直接丢；其它不认识的回 ACK EUNSUP。
- 按键之间设备会自己隔开至少 12 ms 再交给 8042（`MX_KEY_GAP_US`），主机可以一次发一串。
- 输入环 128 个事件，满了丢新的并计数（PC_STAT_R 的 `in_dropped`）。
- 实现：设备侧 `fw/xdisp.h`（单测 `tests/test_xdisp.c`），载荷编解码 `core/x86proto.h`（单测 `tests/test_x86proto.c`），
  Mac 侧 `mac/x86kbd.py`（单测 `tests/test_x86kbd.py`）。
