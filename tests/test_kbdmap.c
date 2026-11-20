/* ASCII / 键名 -> set-1 (core/kbdmap.h) */
#include "tcheck.h"
#include "../core/kbdmap.h"

int main(void){
    CHECK(km_ascii('a') == 0x1E); CHECK(km_ascii('A') == (KM_SHIFT | 0x1E));
    CHECK(km_ascii('1') == 0x02); CHECK(km_ascii('0') == 0x0B); CHECK(km_ascii('!') == (KM_SHIFT | 0x02));
    CHECK(km_ascii('q') == 0x10); CHECK(km_ascii('p') == 0x19); CHECK(km_ascii(']') == 0x1B);
    CHECK(km_ascii('l') == 0x26); CHECK(km_ascii(';') == 0x27); CHECK(km_ascii(':') == (KM_SHIFT | 0x27));
    CHECK(km_ascii('\'') == 0x28); CHECK(km_ascii('`') == 0x29);
    CHECK(km_ascii('\\') == 0x2B); CHECK(km_ascii('z') == 0x2C); CHECK(km_ascii('m') == 0x32);
    CHECK(km_ascii(',') == 0x33); CHECK(km_ascii('.') == 0x34); CHECK(km_ascii('/') == 0x35); CHECK(km_ascii('?') == (KM_SHIFT | 0x35));
    CHECK(km_ascii('-') == 0x0C); CHECK(km_ascii('=') == 0x0D); CHECK(km_ascii('_') == (KM_SHIFT | 0x0C));
    CHECK(km_ascii(' ') == 0x39); CHECK(km_ascii('\n') == 0x1C); CHECK(km_ascii('~') == (KM_SHIFT | 0x29));
    CHECK(km_ascii('\x7f') == 0); CHECK(km_ascii(0) == 0);
    CHECK(km_name("enter") == 0x1C); CHECK(km_name("f1") == 0x3B); CHECK(km_name("f10") == 0x44);
    CHECK(km_name("up") == (KM_E0 | 0x48)); CHECK(km_name("del") == (KM_E0 | 0x53));
    CHECK(km_name("x") == 0x2D); CHECK(km_name("nope") == 0);
    return tc_done("kbdmap");
}
