/*
 * R05 导入的函数原型 —— 每个参数都对照过原厂 app 的调用点（见 docs/r05-notes.md）。
 * 桩由 gen_stubs.py 按 imports_<app>.txt 生成, loader 按 sha256 前 4 字节解析。
 */
#ifndef R05_API_H
#define R05_API_H
#include <stdint.h>

/* ---- pin / gpio (同原厂 hal_init) ---- */
extern int  ql_pin_set_func(uint32_t pin, uint32_t func);
extern int  ql_gpio_init(uint32_t gpio, uint32_t dir, uint32_t pull, uint32_t level);
extern int  ql_gpio_deinit(uint32_t gpio);
extern int  ql_gpio_set_level(uint32_t gpio, uint32_t level);
extern int  ql_gpio_set_direction(uint32_t gpio, uint32_t dir);
#define GPIO_INPUT  0u
#define GPIO_OUTPUT 1u
#define PULL_NONE   0u
#define PULL_DOWN   1u   /* 内核 ql_gpio_set_pull: 1 -> pad 下拉位, 其它非 0 -> 上拉位 */
#define PULL_UP     2u
/* 原厂按键回调里: ql_gpio_get_level(gpio, &u8); 内核按方向读输入/输出寄存器, 写 1 字节 0/1 */
extern int  ql_gpio_get_level(uint32_t gpio, uint8_t *lvl);

/* ---- 喇叭 PCM 播放 (fw v13, io/audio_hw.h; 依据见 docs/audio.md) ----
 * 原厂 app 的 TTS 播放: cfg = {channels 1, samplerate 16000, 其余 0} (9 个 u32), ql_pcm_open(&cfg, 9)
 *   -> ql_pcm_write(h, buf, ≤16384) 循环 -> ql_aud_data_done() -> 等播完 -> ql_pcm_close(h)。
 * flags: 1 BLOCK, 2 NONBLOCK, 4 READ, 8 WRITE (内核要求 &12 非 0; &4 走录音分支, 那条会写 NV, 不用)。
 * ql_pcm_write: count 必须 1..16384; BLOCK 时等内核管道 (16 KiB) 有空, 否则只收得下多少收多少 (可以是 0); <0 出错。
 * ql_set_audio_path_speaker: 无参; 输出 / 输入设备和 NV 里的相同就不写 NV (原厂每次开机都调)。 */
typedef struct { uint32_t channels, samplerate, rsv[7]; } r05_pcm_cfg_t;
#define R05_PCM_BLOCK    1u
#define R05_PCM_NONBLOCK 2u
#define R05_PCM_WRITE    8u
extern void *ql_pcm_open(r05_pcm_cfg_t *cfg, uint32_t flags);   /* 0 = 失败 */
extern int   ql_pcm_write(void *h, const void *data, uint32_t count);
extern int   ql_pcm_close(void *h);
extern void  ql_aud_data_done(void);
extern int   ql_set_audio_path_speaker(void);

/* ---- PWRKEY (原厂按键模块照此顺序注册; 回调无参, 原厂回调里只 ql_rtos_event_send) ----
 * 长按到 shutdown_time 后, 注册了 callback_register 的回调就只调它, 不走默认关机 (实机: 长按不关机)。
 * shutdown_time 下限 420 ms (更小返回错误)。实机: 长按之后收不到 release 回调 (见 link/keys.h)。 */
typedef void (*r05_pwrkey_cb_t)(void);
extern int  ql_pwrkey_shutdown_time_set(uint32_t ms);
extern int  ql_pwrkey_callback_register(r05_pwrkey_cb_t cb);
extern int  ql_pwrkey_press_cb_register(r05_pwrkey_cb_t cb);
extern int  ql_pwrkey_release_cb_register(r05_pwrkey_cb_t cb);

/* ---- rtos ----
 * 原厂: ql_rtos_task_create(&t, 0x4000, 12, "ql_init", fn, NULL, 1)
 *       ql_rtos_task_create(&t, 0x2000, 15, "led_fg00_task", fn, 0, 50)
 * 第 7 个参数 event_count 必须给; 内核会检查 prio / stack 上限 (见 docs/r05-notes.md §9)。 */
extern int  ql_rtos_task_create(void *p_task, uint32_t stack, uint8_t prio,
                                const char *name, void (*entry)(void *),
                                void *arg, uint32_t event_count);
extern int  ql_rtos_task_sleep_ms(uint32_t ms);
extern void ql_delay_us(uint32_t us);
#define APP_PRIORITY_NORMAL 12u   /* = 原厂 ql_init 线程 */

/* ---- MIPI 大屏 (同原厂 App_MipiLCD.c) ---- */
typedef struct { uint32_t width, height; } r05_mipi_lcd_cfg_t;   /* 8 字节, {800,1280} */
extern int  ql_mipi_lcd_init(const r05_mipi_lcd_cfg_t *cfg);      /* <0 = 失败 */
extern int  ql_mipi_lcd_write_screen(void *fb);                   /* 只设扫描基址, 不拷贝 */
/* 名字未解出 (hash ac3ae871): 原厂 lcd_init 成功后紧接着无参调用;
 * 内核里是对某个电源管理对象做一次 PM 锁 (推测: LCD 防休眠 wakelock)。 */
extern int  r05_lcd_pm_lock(void);

/* ---- USB 串口 (port 4 = USB modem 口, 原厂和上位机通信用的就是它) ---- */
extern int  ql_uart_open(uint32_t port);
extern int  ql_uart_write(uint32_t port, const void *data, uint32_t len);
/* 原厂回调里 ql_uart_read(4, buf, size) 返回实际读到的字节数, 循环读到读完 */
extern int  ql_uart_read(uint32_t port, void *data, uint32_t len);
/* cb(ind, port, size): 原厂只在 ind 为 0x82060001 / 0x82060002 (RX 数据类指示) 时读 */
typedef void (*r05_uart_cb_t)(uint32_t ind, uint32_t port, uint32_t size);
extern int  ql_uart_register_cb(uint32_t port, r05_uart_cb_t cb);
#define R05_PORT_USB_MODEM 4u
#define R05_UART_IND_RX    0x82060001u
#define R05_UART_IND_RX2   0x82060002u

/* ---- USB 插拔 (fw v10; 原厂 app 也注册了一个, 回调里只打 "USB plug out" / "USB inserted" 日志) ----
 * 内核在 VBUS / 充电器状态变化时调 cb(state, NULL), 返回值不看; bind 只存一个指针 (再 bind 覆盖), 返回 0。
 * state 同 ql_usb_get_hotplug_state (hash 2fe4cc79): 0 = 拔掉 (没有 VBUS), 1 / 2 = 插着 (2 = USB 状态机在某个状态 4, 推测已枚举)。
 * 只在 VBUS 真的断了时才有 0: 设备另有供电、只断了数据, 或主机 USB 挂起时不报。 */
typedef int (*r05_usb_hotplug_cb_t)(uint32_t state, void *ctx);
extern int  ql_usb_bind_hotplug_cb(r05_usb_hotplug_cb_t cb);
#define R05_USB_HOTPLUG_OUT 0u

/* ---- 信号量 (原厂: create(&s, 0); wait(s, 超时 ms) 返回 0 = 拿到; release(s)) ---- */
extern int  ql_rtos_semaphore_create(void **sem, uint32_t init);
extern int  ql_rtos_semaphore_wait(void *sem, uint32_t timeout_ms);
extern int  ql_rtos_semaphore_release(void *sem);

/* ---- 看门狗 (原厂 enter: ql_dev_cfg_wdt(1); 原厂在周期任务循环里 ql_dev_feed_wdt()) ---- */
extern int  ql_dev_cfg_wdt(uint32_t opt);
extern int  ql_dev_feed_wdt(void);

/* ---- 内核堆 (诊断用) ---- */
extern void *malloc(uint32_t n);
extern void  free(void *p);
/* 输出结构按 Quectel CSDK 推测为 {total, avail, max_block, ...}; 诊断时多给几个字的空间 */
extern int  ql_dev_memory_size_query(uint32_t *out);

/* ---- 时间: 开机以来微秒 (64 位) ---- */
extern int64_t osiUpTimeUS(void);

#endif
