/*
 * 没编进来的外设 (adlib / ne2000 / sb16) 的空实现: pc.c 原样调用它们, 这里全部当"没插卡"。
 * 端口读回 0xff (ISA 总线上没人应答的样子), 写忽略。第一期不做 SB16 (见 README 的路线)。
 */
#include "pc.h"

AdlibState *adlib_new(){ return 0; }
void adlib_free(AdlibState *s){ (void)s; }
void adlib_callback(void *opaque, uint8_t *stream, int free){ (void)opaque; (void)stream; (void)free; }
uint32_t adlib_read(void *opaque, uint32_t nport){ (void)opaque; (void)nport; return 0xff; }
void adlib_write(void *opaque, uint32_t nport, uint32_t val){ (void)opaque; (void)nport; (void)val; }

void ne2000_set_config_file(const char *file){ (void)file; }
NE2000State *isa_ne2000_init(int base, int irq, void *pic, void (*set_irq)(void *pic, int irq, int level)){
    (void)base; (void)irq; (void)pic; (void)set_irq; return 0;
}
void ne2000_step(NE2000State *s){ (void)s; }
uint32_t ne2000_ioport_read(void *opaque, uint32_t addr){ (void)opaque; (void)addr; return 0xff; }
void ne2000_ioport_write(void *opaque, uint32_t addr, uint32_t val){ (void)opaque; (void)addr; (void)val; }
uint32_t ne2000_asic_ioport_read(void *opaque, uint32_t addr){ (void)opaque; (void)addr; return 0xff; }
void ne2000_asic_ioport_write(void *opaque, uint32_t addr, uint32_t val){ (void)opaque; (void)addr; (void)val; }
uint32_t ne2000_reset_ioport_read(void *opaque, uint32_t addr){ (void)opaque; (void)addr; return 0xff; }
void ne2000_reset_ioport_write(void *opaque, uint32_t addr, uint32_t val){ (void)opaque; (void)addr; (void)val; }

SB16State *sb16_new(int port, int irq, void *isa_hdma, void *isa_dma,
                    void *pic, void (*set_irq)(void *pic, int irq, int level)){
    (void)port; (void)irq; (void)isa_hdma; (void)isa_dma; (void)pic; (void)set_irq; return 0;
}
void sb16_audio_callback(void *opaque, uint8_t *stream, int free){ (void)opaque; (void)stream; (void)free; }
uint32_t sb16_mixer_read(void *opaque, uint32_t nport){ (void)opaque; (void)nport; return 0xff; }
void sb16_mixer_write_indexb(void *opaque, uint32_t nport, uint32_t val){ (void)opaque; (void)nport; (void)val; }
void sb16_mixer_write_datab(void *opaque, uint32_t nport, uint32_t val){ (void)opaque; (void)nport; (void)val; }
uint32_t sb16_dsp_read(void *opaque, uint32_t nport){ (void)opaque; (void)nport; return 0xff; }
void sb16_dsp_write(void *opaque, uint32_t nport, uint32_t val){ (void)opaque; (void)nport; (void)val; }
