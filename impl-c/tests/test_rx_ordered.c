#include "schcvx/rx_ordered.h"
#include "schcvx/tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
int main(void) {
 uint8_t rule=1, prefix[9], frame[600], dg[8][700]; size_t sizes[8]; uint32_t vni=100;
 struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&rule,1,0};
 struct schcvx_rx_ordered_cfg cfg={wire,&vni,1,4,8,4096,600,10,100};
 size_t bytes=schcvx_rx_ordered_bytes(&cfg); void *mem=malloc(bytes); CHECK(mem);
 struct schcvx_rx_ordered rx; struct schcvx_counters c={0}; struct schcvx_delivery out;
 CHECK(!schcvx_rx_ordered_init(&rx,&cfg,mem,bytes,&c));
 for (size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)i;
 CHECK(!schcvx_packet_prefix(0,100,prefix));
 for (unsigned k=2;k<=8;k++) {
  struct schcvx_tx_plan plan={false,(uint8_t)k,80,10}; size_t length=80*(k-1)+10;
  for (unsigned j=0;j<k;j++) CHECK(!schcvx_tx_fragment(&plan,&wire,1,k,prefix,frame,length-9,j,dg[j],sizeof(dg[j]),&sizes[j]));
  for (unsigned j=0;j<k;j++) CHECK(schcvx_rx_ordered_receive(&rx,dg[j],sizes[j],k*20+j,&out)==(j==k-1));
  CHECK(out.vni==100 && out.frame_len==length-9 && !memcmp(out.frame,frame,length-9));
  CHECK(!schcvx_rx_ordered_receive(&rx,dg[0],sizes[0],k*20+k,&out)); CHECK(c.late==k-1);
 }
 uint8_t packet[23]; memcpy(packet,prefix,9); memcpy(packet+9,frame,14);
 CHECK(schcvx_rx_ordered_receive(&rx,packet,sizeof(packet),300,&out)); packet[1]=0; CHECK(!schcvx_rx_ordered_receive(&rx,packet,sizeof(packet),301,&out));
 free(mem); return 0;
}
