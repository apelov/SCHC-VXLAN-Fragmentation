#include "schcvx/wire.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int run(enum schcvx_variant v)
{
    uint8_t rules[] = {1, 2};
    struct schcvx_assoc_wire a = {v, rules, 2, 0};
    struct schcvx_msg m = {.kind=SCHCVX_REGULAR, .rule_id=1, .dtag=1, .fcn=8}, got;
    uint8_t b[64] = {0};
    const uint8_t compact[] = {0x10,0,0,0,0x18};
    const uint8_t extended[] = {1,0,0,0,0,0,0,0,0x18};
    size_t h = schcvx_header_len(v);
    CHECK(h == (v == SCHCVX_COMPACT ? 5u : 9u));
    CHECK(schcvx_all1_header_len(v) == h + 4);
    CHECK(schcvx_wire_config_check(&a) == 0);
    CHECK(schcvx_encode_header(&a, &m, b) == h);
    CHECK(memcmp(b, v == SCHCVX_COMPACT ? compact : extended, h) == 0);
    for (size_t n=0; n<h; ++n)
        CHECK(schcvx_classify(&a,b,n,&got) == -SCHCVX_E_MALFORMED);
    for (unsigned f=0; f<16; ++f) {
        memset(b,0,sizeof(b));
        m.fcn=(uint8_t)f; m.kind=f==15 ? SCHCVX_ALL1 : SCHCVX_REGULAR;
        m.rcs=UINT32_C(0x89abcdef);
        m.dtag=v==SCHCVX_COMPACT ? UINT32_MAX : UINT64_C(0x0fedcba987654321);
        size_t n=schcvx_encode_header(&a,&m,b);
        if (f==6 || f==7) {
            CHECK(n==0); m.fcn=0;
            CHECK(schcvx_encode_header(&a,&m,b)==h);
            b[h-1]=(uint8_t)((b[h-1]&0xf0)|f);
            CHECK(schcvx_classify(&a,b,h+9,&got)==-SCHCVX_E_INVALID_FCN);
            continue;
        }
        CHECK(n==h+(f==15 ? 4u : 0u));
        CHECK(schcvx_classify(&a,b,n+9,&got)==0);
        CHECK(got.kind==m.kind && got.rule_id==1 && got.dtag==m.dtag && got.fcn==f);
        CHECK(got.payload==b+n && got.payload_len==9);
        CHECK(got.first==(f>=8 && f<=14));
        if (f==15) {
            CHECK(got.rcs==m.rcs);
            for (size_t extra=1; extra<=4; ++extra)
                CHECK(schcvx_classify(&a,b,h+extra,&got)==-SCHCVX_E_SHORT_ALL1);
            CHECK(schcvx_classify(&a,b,h,&got)==0);
            CHECK(got.kind==SCHCVX_ABORT && got.payload_len==0);
            m.kind=SCHCVX_ABORT;
            CHECK(schcvx_encode_header(&a,&m,b)==h);
        } else {
            CHECK(got.countdown==(f&7));
            CHECK(schcvx_classify(&a,b,h,&got)==-SCHCVX_E_MALFORMED);
            if (f>=8) {
                for (size_t extra=1; extra<9; ++extra)
                    CHECK(schcvx_classify(&a,b,h+extra,&got)==-SCHCVX_E_FIRST);
                b[h]=3;
                CHECK(schcvx_classify(&a,b,h+9,&got)==-SCHCVX_E_FIRST);
            }
        }
    }
    memset(b,0,sizeof(b));
    for (size_t n=0; n<23; ++n)
        CHECK(schcvx_classify(&a,b,n,&got)==-SCHCVX_E_MALFORMED);
    CHECK(schcvx_classify(&a,b,23,&got)==0);
    CHECK(got.kind==SCHCVX_UNFRAGMENTED && got.payload==b && got.payload_len==23);
    b[0]=v==SCHCVX_COMPACT ? 0x30 : 3;
    {
        /* Sentinel: output struct is unchanged on every classify error. */
        struct schcvx_msg sentinel, before;
        memset(&sentinel,0xa5,sizeof(sentinel)); before=sentinel;
        CHECK(schcvx_classify(&a,b,23,&sentinel)==-SCHCVX_E_UNKNOWN_RULE);
        CHECK(memcmp(&sentinel,&before,sizeof(before))==0);
        CHECK(schcvx_classify(&a,b,0,&sentinel)==-SCHCVX_E_MALFORMED);
        CHECK(schcvx_classify(&a,NULL,23,&sentinel)==-SCHCVX_E_MALFORMED);
        CHECK(schcvx_classify(&a,b,h-1,&sentinel)==-SCHCVX_E_MALFORMED);
        CHECK(memcmp(&sentinel,&before,sizeof(before))==0);
    }
    m.kind=SCHCVX_REGULAR; m.fcn=0; m.dtag=0; m.rule_id=2;
    CHECK(schcvx_encode_header(&a,&m,b)==h);
    CHECK(schcvx_classify(&a,b,h+1,&got)==0 && got.rule_id==2);
    {
        /* Sentinel: the encoder writes nothing when it returns an error. */
        uint8_t pat[sizeof(b)], s[sizeof(b)];
        memset(pat,0xa5,sizeof(pat));
        memcpy(s,pat,sizeof(s));
        m.dtag=v==SCHCVX_COMPACT ? UINT64_C(0x100000000) : UINT64_C(0x1000000000000000);
        CHECK(schcvx_encode_header(&a,&m,s)==0);
        CHECK(memcmp(s,pat,sizeof(s))==0);
        m.dtag=0; m.fcn=16;
        CHECK(schcvx_encode_header(&a,&m,s)==0);
        CHECK(memcmp(s,pat,sizeof(s))==0);
        m.fcn=0; m.rule_id=16;
        CHECK(schcvx_encode_header(&a,&m,s)==0);
        CHECK(memcmp(s,pat,sizeof(s))==0);
    }
    a.cd_rule_id=v==SCHCVX_COMPACT ? 0x10 : 1;
    CHECK(schcvx_wire_config_check(&a)==-SCHCVX_E_CONFIG);
    return 0;
}
int main(void)
{
    CHECK(run(SCHCVX_COMPACT)==0 && run(SCHCVX_EXTENDED)==0);
    for (unsigned k=2; k<=8; ++k) {
        for (unsigned p=0; p<k; ++p) {
            int f=schcvx_fcn_value(k,p);
            CHECK(f==(p==k-1 ? 15 : p==0 ? (int)k+6 : (int)(k-p-2)));
            CHECK(schcvx_fcn_is_first((uint8_t)f)==(p==0));
            CHECK(schcvx_fcn_countdown((uint8_t)f)==(p==k-1 ? -SCHCVX_E_INVALID_FCN : (int)(k-p-2)));
        }
        CHECK(schcvx_fcn_value(k,k)<0);
    }
    CHECK(schcvx_fcn_value(1,0)<0 && schcvx_fcn_value(9,0)<0);
    CHECK(schcvx_header_len((enum schcvx_variant)99)==0);
    CHECK(schcvx_all1_header_len((enum schcvx_variant)99)==0);
    return 0;
}
