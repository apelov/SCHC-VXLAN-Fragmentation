#include "schcvx/rx_common.h"
int schcvx_rx_validate(uint8_t cd, const uint32_t *vnis, size_t count, const uint8_t *p, size_t len, struct schcvx_delivery *out) {
    if (!p || !out || len<23 || p[0]!=cd || !(p[1]&0x08)) return SCHCVX_RX_MALFORMED;
    uint32_t vni=((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];
    for (size_t i=0;i<count;i++) if (vnis[i]==vni) { *out=(struct schcvx_delivery){p+9,len-9,vni}; return SCHCVX_RX_VALID; }
    return SCHCVX_RX_UNAUTHORISED;
}
