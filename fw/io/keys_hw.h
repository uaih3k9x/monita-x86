/*
 * 前面板 4 键的硬件层 (R05)。去抖/长按/PWRKEY 计数转电平都在 link/keys.h (主机单测)。
 * 实机证实 (2026-11-18, keydiag):
 *   键 | 位置      | 接法                          | 原厂用途
 *   1  | 左上 查询 | PWRKEY (ql_pwrkey_* 回调)     | 查询收款记录; 长按 3 s 关机请求 (注册 callback_register 后不关机)
 *   2  | 左下 ≡    | 脚14 func4 -> GPIO21, 低有效  | 配网/设置
 *   3  | 右上 +    | 脚99 func5 -> GPIO8,  低有效  | 音量 +
 *   4  | 右下 −    | 脚13 func4 -> GPIO23, 低有效  | 音量 −
 * GPIO 键有硬件去抖。原厂用边沿中断 + 100 ms 轮询标志; 这里主循环轮询电平, 不进中断上下文。
 * PWRKEY 回调只给 volatile 计数 +1, 不调任何 RTOS 接口。只供 monita 任务调用。
 */
#ifndef MONITA_KEYS_HW_H
#define MONITA_KEYS_HW_H
#include "r05_api.h"
#include "keys.h"

#ifndef KEY_PULL
#define KEY_PULL PULL_UP     /* 原厂是 PULL_NONE (板上有上拉); keydiag 用内部上拉实机正常 */
#endif

typedef struct { uint8_t pin, func, gpio, key; } key_pin_t;
static const key_pin_t key_pins[3] = {
    { 99, 5,  8, 3 },   /* +  */
    { 13, 4, 23, 4 },   /* −  */
    { 14, 4, 21, 2 },   /* ≡  */
};

static volatile uint32_t key_pwr_press_n, key_pwr_release_n;   /* 回调计数, 主循环 pwr_sample */
static volatile uint32_t key_pwr_longreq;                       /* 内核长按关机请求次数 (已截住) */
static void key_pwr_press(void)   { key_pwr_press_n++; }
static void key_pwr_release(void) { key_pwr_release_n++; }
static void key_pwr_shutdown(void){ key_pwr_longreq++; }

/* 返回: bit i = key_pins[i] 初始化失败 (i=0..2), bit 4..7 = pwrkey 4 个注册调用失败 */
static uint32_t keys_hw_init(void){
    uint32_t err = 0;
    /* 原厂开机引脚表把 GPIO8/21/23 的另一个可选脚 (9/124/121) 设成 func1, 保证同一 GPIO 只从一个脚进来。
       我们不跑整张表, 这里照抄这三项。 */
    ql_pin_set_func(9, 1); ql_pin_set_func(124, 1); ql_pin_set_func(121, 1);
    for (int i = 0; i < 3; i++){
        const key_pin_t *p = &key_pins[i];
        if (ql_pin_set_func(p->pin, p->func)) err |= 1u << i;
        ql_gpio_deinit(p->gpio);
        if (ql_gpio_init(p->gpio, GPIO_INPUT, KEY_PULL, 1)) err |= 1u << i;
    }
    /* 同原厂顺序与参数: 关机长按 3000 ms; 注册 callback_register 后内核不再自己关机 (实机证实) */
    if (ql_pwrkey_shutdown_time_set(3000))               err |= 1u << 4;
    if (ql_pwrkey_callback_register(key_pwr_shutdown))   err |= 1u << 5;
    if (ql_pwrkey_press_cb_register(key_pwr_press))      err |= 1u << 6;
    if (ql_pwrkey_release_cb_register(key_pwr_release))  err |= 1u << 7;
    return err;
}

/* 原始按下位图 (bit k-1 = 键 k); GPIO 低电平 = 按下; PWRKEY 由计数经 pwr_sample 得到 */
static uint8_t keys_hw_raw(pwr_t *pw, uint32_t now){
    uint8_t m = pwr_sample(pw, key_pwr_press_n, key_pwr_release_n, now) ? KEY_BIT(1) : 0;
    for (int i = 0; i < 3; i++){
        uint8_t lv = 1;
        if (ql_gpio_get_level(key_pins[i].gpio, &lv) == 0 && lv == 0) m |= KEY_BIT(key_pins[i].key);
    }
    return m;
}

#endif
