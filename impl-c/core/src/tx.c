#include "schcvx/tx.h"
#include "schcvx/crc32c.h"
#include <string.h>
int schcvx_tx_config_check(const struct schcvx_tx_cfg *c)
{
    size_t a; uint64_t budget, tile, total;
    if (!c || !(a=schcvx_all1_header_len(c->variant)) || c->max_fragments<2 || c->max_fragments>8 || c->min_frame_len<14 || c->min_frame_len>c->max_frame_len || (uint64_t)c->pmtu < (uint64_t)c->ip_header_len+8+a+9) return -SCHCVX_E_CONFIG;
    budget=(uint64_t)c->pmtu-c->ip_header_len-8; tile=budget-a; total=(uint64_t)c->max_frame_len+9;
    if (total>budget && total>tile*c->max_fragments) return -SCHCVX_E_CONFIG;
    return 0;
}
int schcvx_tx_plan(const struct schcvx_tx_cfg *c, size_t len, struct schcvx_tx_plan *out)
{
    uint64_t total, budget, tile, k; struct schcvx_tx_plan p={0};
    if (!out || schcvx_tx_config_check(c)) return -SCHCVX_E_CONFIG;
    if (len<c->min_frame_len || len>c->max_frame_len) return -SCHCVX_E_TX_LENGTH;
    total=(uint64_t)len+9; budget=(uint64_t)c->pmtu-c->ip_header_len-8;
    if (total<=budget) { p.unfragmented=true; p.k=1; p.last_len=(uint32_t)total; }
    else { tile=budget-schcvx_all1_header_len(c->variant); k=total/tile+(total%tile!=0); if (k<2 || k>c->max_fragments) return -SCHCVX_E_TX_FRAGMENTS; p.k=(uint8_t)k; p.c=(uint32_t)tile; p.last_len=(uint32_t)(total-(k-1)*tile); }
    *out=p; return 0;
}
int schcvx_vxlan_header(uint32_t vni, uint8_t out[8])
{
    if (!out || vni>0xffffffu) return -SCHCVX_E_CONFIG;
    memset(out,0,8); out[0]=8; out[4]=(uint8_t)(vni>>16); out[5]=(uint8_t)(vni>>8); out[6]=(uint8_t)vni; return 0;
}
int schcvx_packet_prefix(uint8_t rule, uint32_t vni, uint8_t out[9])
{ if (!out || vni>0xffffffu) return -SCHCVX_E_CONFIG; out[0]=rule; return schcvx_vxlan_header(vni,out+1); }
int schcvx_tx_fragment(const struct schcvx_tx_plan *p, const struct schcvx_assoc_wire *a, uint8_t rule, uint64_t dtag, const uint8_t prefix[9], const uint8_t *frame, size_t len, unsigned pos, uint8_t *out, size_t cap, size_t *written)
{
    struct schcvx_msg m={0}; uint8_t header[13]; size_t h, off, n, take; uint32_t crc; int fcn;
    if (!p || !a || !prefix || (!frame && len) || !out || !written || p->unfragmented || p->k<2 || p->k>8 || pos>=p->k || p->c<9 || !p->last_len || p->last_len>p->c || len>SIZE_MAX-9 || (uint64_t)(p->k-1)*p->c+p->last_len!=(uint64_t)len+9 || prefix[0]!=a->cd_rule_id) return -SCHCVX_E_CONFIG;
    fcn=schcvx_fcn_value(p->k,pos); if (fcn<0) return fcn;
    m.kind=pos==(unsigned)p->k-1 ? SCHCVX_ALL1 : SCHCVX_REGULAR; m.rule_id=rule; m.dtag=dtag; m.fcn=(uint8_t)fcn;
    if (m.kind==SCHCVX_ALL1) { schcvx_crc32c_init(&crc); schcvx_crc32c_update(&crc,prefix,9); schcvx_crc32c_update(&crc,frame,len); m.rcs=schcvx_crc32c_final(crc); }
    h=schcvx_encode_header(a,&m,header); if (!h) return -SCHCVX_E_CONFIG;
    n=pos==(unsigned)p->k-1 ? p->last_len : p->c; if (cap<h || n>cap-h) return -SCHCVX_E_TX_BUFFER;
    memcpy(out,header,h); off=(size_t)pos*p->c;
    if (off<9) { take=9-off; if (take>n) take=n; memcpy(out+h,prefix+off,take); h+=take; n-=take; off+=take; }
    if (n) memcpy(out+h,frame+off-9,n);
    *written=h+n; return 0;
}
bool schcvx_tx_emission_stopped(uint64_t first, uint64_t now, uint64_t s)
{ return now>=first && now-first>=s; }
