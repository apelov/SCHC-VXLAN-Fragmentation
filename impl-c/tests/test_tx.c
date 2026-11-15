#include "schcvx/tx.h"
#include "wire_vectors.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
 uint8_t frame[11000], prefix[9], out[1600], packet[11009]; const uint8_t rule=1;
 struct schcvx_tx_cfg c={1500,20,SCHCVX_COMPACT,14,10000,8}; struct schcvx_tx_plan p;
 struct schcvx_assoc_wire a={SCHCVX_COMPACT,&rule,1,0};
 for (size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)i;
 CHECK(!schcvx_packet_prefix(0,12345,prefix)); memcpy(packet,prefix,9); memcpy(packet+9,frame,sizeof(frame));
 for (unsigned variant=0;variant<2;variant++) for (unsigned j=0;j<3;j++) {
  c.variant=(enum schcvx_variant)variant; a.variant=c.variant; c.pmtu=j==2?1280:1500; c.ip_header_len=j==1?40:20; c.max_frame_len=9014;
  size_t boundary=c.pmtu-c.ip_header_len-17;
  CHECK(!schcvx_tx_config_check(&c)); CHECK(!schcvx_tx_plan(&c,boundary,&p) && p.unfragmented && p.last_len==c.pmtu-c.ip_header_len-8);
  CHECK(!schcvx_tx_plan(&c,boundary+1,&p) && p.k==2);
  for (unsigned i=0;i<p.k;i++) { size_t n; CHECK(!schcvx_tx_fragment(&p,&a,1,1,prefix,frame,boundary+1,i,out,sizeof(out),&n)); CHECK(c.ip_header_len+8+n<=c.pmtu); }
  CHECK(!schcvx_tx_plan(&c,9014,&p) && p.k==(j==2?8:7));
 }
 c.variant=SCHCVX_COMPACT; a.variant=c.variant; c.pmtu=1280; c.ip_header_len=40; CHECK(!schcvx_tx_plan(&c,9014,&p) && p.k==8);
 c.pmtu=1500; c.ip_header_len=20; CHECK(!schcvx_tx_plan(&c,1514,&p) && p.c==1463 && p.last_len==60);
 c.pmtu=1280; CHECK(!schcvx_tx_plan(&c,1514,&p) && p.c==1243 && p.last_len==280);
 for (size_t v=0;v<sizeof(wire_vectors)/sizeof(wire_vectors[0]);v++) {
  const struct wire_vector *w=&wire_vectors[v]; c.variant=w->bits==32?SCHCVX_COMPACT:SCHCVX_EXTENDED; a.variant=c.variant;
  c.pmtu=(uint32_t)(20+8+schcvx_all1_header_len(c.variant)+w->datagrams[0].tile_len); c.ip_header_len=20;
  CHECK(!schcvx_tx_plan(&c,1514,&p));
  for (unsigned i=0;i<p.k;i++) { size_t n; CHECK(!schcvx_tx_fragment(&p,&a,1,1,prefix,frame,1514,i,out,sizeof(out),&n)); CHECK(n==w->datagrams[i].len && !memcmp(out,w->datagrams[i].data,n)); }
 }
 c.pmtu=1500; c.ip_header_len=20; c.variant=SCHCVX_COMPACT; a.variant=c.variant; c.max_frame_len=11000;
 for (unsigned k=2;k<=8;k++) { size_t len=k==2 ? 1464 : (k-1)*1463+1-9, offset=0; CHECK(!schcvx_tx_plan(&c,len,&p) && p.k==k);
  for (unsigned i=0;i<k;i++) { size_t n; struct schcvx_msg m; CHECK(!schcvx_tx_fragment(&p,&a,1,1,prefix,frame,len,i,out,sizeof(out),&n)); CHECK(c.ip_header_len+8+n<=c.pmtu); CHECK(!schcvx_classify(&a,out,n,&m)); CHECK(m.fcn==schcvx_fcn_value(k,i)); CHECK(!memcmp(m.payload,packet+offset,m.payload_len)); offset+=m.payload_len; } CHECK(offset==len+9);
 }
 CHECK(schcvx_tx_plan(&c,13,&p)==-SCHCVX_E_TX_LENGTH); CHECK(schcvx_tx_plan(&c,11001,&p)==-SCHCVX_E_TX_LENGTH);
 c.max_fragments=2; CHECK(schcvx_tx_config_check(&c)==-SCHCVX_E_CONFIG); CHECK(!schcvx_tx_emission_stopped(10,19,10)); CHECK(schcvx_tx_emission_stopped(10,20,10)); return 0;
}
