#include "schcvx/crc32c.h"
#include "schcvx/wire.h"
#include "wire_vectors.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    const uint8_t rule=1;
    const uint8_t envelope[]={0,8,0,0,0,0,0x30,0x39,0};
    uint8_t packet[1523], datagram[1536];
    memcpy(packet,envelope,sizeof(envelope));
    for (size_t i=0; i<1514; ++i)
        packet[9+i]=(uint8_t)i;
    for (size_t v=0; v<sizeof(wire_vectors)/sizeof(wire_vectors[0]); ++v) {
        const struct wire_vector *vector=&wire_vectors[v];
        struct schcvx_assoc_wire a={vector->bits==32 ? SCHCVX_COMPACT : SCHCVX_EXTENDED,&rule,1,0};
        struct schcvx_msg got;
        uint32_t rcs=schcvx_crc32c(packet,sizeof(packet));
        size_t offset=0;
        CHECK(vector->packet_len==sizeof(packet) && rcs==vector->rcs);
        CHECK(vector->count==2);
        for (size_t i=0; i<vector->count; ++i) {
            const struct vector_datagram *d=&vector->datagrams[i];
            struct schcvx_msg m={.kind=i==0 ? SCHCVX_REGULAR : SCHCVX_ALL1,
                .rule_id=1,.dtag=1,.fcn=i==0 ? 8 : 15,.rcs=rcs};
            size_t n=schcvx_encode_header(&a,&m,datagram);
            CHECK(n!=0 && n+d->tile_len<=sizeof(datagram));
            CHECK(d->tile_len<=sizeof(packet)-offset);
            memcpy(datagram+n,packet+offset,d->tile_len);
            CHECK(n+d->tile_len==d->len && memcmp(datagram,d->data,d->len)==0);
            CHECK(20+8+d->len==d->ip_len);
            CHECK(schcvx_classify(&a,d->data,d->len,&got)==0);
            CHECK(got.kind==m.kind && got.rule_id==1 && got.dtag==1 && got.fcn==m.fcn);
            CHECK(got.first==(i==0) && got.payload_len==d->tile_len);
            CHECK(memcmp(got.payload,packet+offset,d->tile_len)==0);
            if (i!=0)
                CHECK(got.rcs==rcs);
            offset+=d->tile_len;
        }
        CHECK(offset==sizeof(packet));
        CHECK(sizeof(unfragmented_vector)==9+64);
        CHECK(memcmp(packet,unfragmented_vector,sizeof(unfragmented_vector))==0);
        CHECK(schcvx_classify(&a,unfragmented_vector,sizeof(unfragmented_vector),&got)==0);
        CHECK(got.kind==SCHCVX_UNFRAGMENTED && got.rule_id==0);
        CHECK(got.payload_len==sizeof(unfragmented_vector));
        CHECK(memcmp(got.payload,packet,got.payload_len)==0);
    }
    return 0;
}
