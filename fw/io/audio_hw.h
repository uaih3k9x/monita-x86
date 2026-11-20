/*
 * 喇叭硬件 (fw v13): 功放使能 + 内核 PCM 播放器, 给 link/audio.h 的 au_io_t 用。依据和等级见 docs/audio.md。
 *   功放: pin 15 -> func 4 = GPIO22, 输出, 高 = 开 (同原厂: 开机设成输出低, 开流前拉高, 关流后拉低)。
 *   PCM: ql_pcm_open({1 ch, 16000 Hz}, WRITE | NONBLOCK); 原厂用 WRITE | BLOCK, 我们在链路主循环里泵, 不能等,
 *        所以用非阻塞 (内核 ql_pcm_write 里 BLOCK 位只决定管道写是否等空位, 其余同一条路径)。
 *   ql_set_audio_path_speaker 第一次开流时调一次 (NV 里已经是喇叭时内核不写 NV)。
 *   不导入 ql_aud_set_volume 等会写内部 flash 的接口 (fw/sdk/validate.py 永久禁止), 音量在 audio.h 软件乘。
 */
#ifndef MONITA_IO_AUDIO_HW_H
#define MONITA_IO_AUDIO_HW_H
#include "r05_api.h"
#include "audio.h"

#define AU_PA_PIN   15u
#define AU_PA_FUNC  4u
#define AU_PA_GPIO  22u

static void *g_au_h;
static int   g_au_path;

/* 开机: 功放脚设成输出低。返回 0 = 成功 */
static int audio_hw_init(void){
    if (ql_pin_set_func(AU_PA_PIN, AU_PA_FUNC)) return 1;
    ql_gpio_deinit(AU_PA_GPIO);
    return ql_gpio_init(AU_PA_GPIO, GPIO_OUTPUT, PULL_NONE, 0) ? 2 : 0;
}

static void au_hw_pa(void *c, uint32_t on){ (void)c; ql_gpio_set_level(AU_PA_GPIO, on ? 1u : 0u); }
static int au_hw_open(void *c, uint32_t rate, uint32_t ch){
    (void)c;
    if (!g_au_path){ ql_set_audio_path_speaker(); g_au_path = 1; }
    r05_pcm_cfg_t cfg = { ch, rate, { 0 } };
    g_au_h = ql_pcm_open(&cfg, R05_PCM_WRITE | R05_PCM_NONBLOCK);
    return g_au_h ? 0 : 1;
}
static int32_t au_hw_write(void *c, const uint8_t *p, uint32_t n){
    (void)c;
    if (!g_au_h) return -1;
    int r = ql_pcm_write(g_au_h, p, n);
    if (r < 0) g_au_h = 0;                             /* 内核第一次写失败时自己已经 close 过; 别再 close 一次 */
    return r;
}
static void au_hw_close(void *c){
    (void)c;
    if (!g_au_h) return;
    ql_aud_data_done();                                /* 同原厂: 先告诉播放器没有更多数据, 再关 */
    ql_pcm_close(g_au_h);
    g_au_h = 0;
}
#endif
