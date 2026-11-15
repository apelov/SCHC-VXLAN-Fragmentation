#include "daemon.h"
#include "schcvx/dtag.h"
#include "schcvx/rx_tolerant.h"
#include <stdlib.h>
#include <string.h>
struct association { struct schcvx_dtag dtag; struct schcvx_counters counters; struct schcvx_rx_ordered ordered; struct schcvx_rx_tolerant tolerant; void *storage; };
struct schcvx_daemon { struct schcvx_config config; struct schcvx_io io; struct schcvx_rx_tolerant_pool pool; struct association associations[SCHCVX_MAX_ASSOC]; };
void schcvx_daemon_destroy(struct schcvx_daemon *d) { size_t i; if(!d)return; for(i=0;i<d->config.count;i++) free(d->associations[i].storage); free(d); }
struct schcvx_daemon *schcvx_daemon_create(const struct schcvx_config *c,const struct schcvx_io *io,uint64_t startup) {
 struct schcvx_daemon *d; size_t i,n; if(!c || !io || !io->send_datagram || !io->write_frame || !io->now || !c->count || c->count>SCHCVX_MAX_ASSOC) return NULL;
 d=calloc(1,sizeof *d); if(!d)return NULL; d->config=*c; d->io=*io; d->pool.max_entries=c->max_entries; d->pool.max_bytes=c->max_bytes; d->pool.max_orphans=c->orphan_budget;
 for(i=0;i<c->count;i++) { struct schcvx_config_assoc *a=&d->config.assoc[i]; struct association *s=&d->associations[i]; struct schcvx_assoc_wire w={a->tx.variant,&a->fr_rule_id,1,a->cd_rule_id};
 if(schcvx_dtag_init(&s->dtag,a->tx.variant==SCHCVX_COMPACT?32:60,a->dtag_start,startup,a->Q,a->dtag_rate,a->dtag_burst)) goto error;
 if(a->tolerant) { struct schcvx_rx_tolerant_cfg r={0}; r.wire=w; r.vnis=a->vnis; r.vni_count=a->vni_count; r.max_entries=a->max_entries; r.retirement_capacity=a->max_retired; r.max_bytes=a->max_bytes; r.max_frame_len=a->tx.max_frame_len; r.max_tile_len=a->tx.pmtu; r.orphan_budget=a->orphan_budget; r.deadline_ns=a->Delta; r.inactivity_ns=a->I; r.retention_ns=a->R; r.pool=&d->pool; n=schcvx_rx_tolerant_bytes(&r); if(!n || !(s->storage=malloc(n)) || schcvx_rx_tolerant_init(&s->tolerant,&r,s->storage,n,&s->counters)) goto error; }
 else { struct schcvx_rx_ordered_cfg r={0}; r.wire=w; r.vnis=a->vnis; r.vni_count=a->vni_count; r.max_entries=a->max_entries; r.retirement_capacity=a->max_retired; r.max_bytes=a->max_bytes; r.max_frame_len=a->tx.max_frame_len; r.inactivity_ns=a->I; r.retention_ns=a->R; n=schcvx_rx_ordered_bytes(&r); if(!n || !(s->storage=malloc(n)) || schcvx_rx_ordered_init(&s->ordered,&r,s->storage,n,&s->counters)) goto error; }
 } return d;
 error: schcvx_daemon_destroy(d); return NULL;
}
int schcvx_process_frame(struct schcvx_daemon *d,size_t i,const uint8_t *frame,size_t len,uint64_t now) {
 struct schcvx_config_assoc *a; struct association *s; struct schcvx_tx_plan p; struct schcvx_assoc_wire w; uint8_t prefix[9],*buf; size_t n; uint64_t tag; unsigned j; int rc;
 if(!d || i>=d->config.count || (!frame && len))return -1;
 a=&d->config.assoc[i]; s=&d->associations[i]; rc=schcvx_tx_plan(&a->tx,len,&p);
 if(rc) { if(rc==-SCHCVX_E_TX_LENGTH) s->counters.sender_drop_length++; else s->counters.sender_drop_k_range++; return rc; }
 if(schcvx_packet_prefix(a->cd_rule_id,a->vnis[0],prefix)) return -1;
 if(p.unfragmented) { buf=malloc(len+9); if(!buf)return -1; memcpy(buf,prefix,9); memcpy(buf+9,frame,len); rc=d->io.send_datagram(d->io.context,i,buf,len+9); free(buf); if(!rc)s->counters.packets_sent_unfragmented++; return rc; }
 rc=schcvx_dtag_allocate(&s->dtag,now,&tag); if(rc) { if(rc==-SCHCVX_E_DTAG_QUARANTINE) s->counters.sender_drop_quarantine++; else s->counters.sender_drop_dtag_admission_stopped++; return rc; }
 buf=malloc(a->tx.pmtu); if(!buf)return -1; w=(struct schcvx_assoc_wire){a->tx.variant,&a->fr_rule_id,1,a->cd_rule_id};
 for(j=0;j<p.k;j++) { if(j && schcvx_tx_emission_stopped(now,d->io.now(d->io.context),a->S)) { s->counters.sender_drop_s_exceeded++; free(buf); return -1; } rc=schcvx_tx_fragment(&p,&w,a->fr_rule_id,tag,prefix,frame,len,j,buf,a->tx.pmtu,&n); if(rc || (rc=d->io.send_datagram(d->io.context,i,buf,n))!=0) { free(buf); return rc; } }
 free(buf); s->counters.packets_sent_fragmented++; return 0;
}
int schcvx_process_datagram(struct schcvx_daemon *d,const char *local,uint16_t port,const char *source,const uint8_t *data,size_t len,uint64_t now) { size_t i; struct schcvx_delivery out; int r; if(!d || !local || !source || (!data && len))return -1; for(i=0;i<d->config.count;i++) { struct schcvx_config_assoc *a=&d->config.assoc[i]; struct association *s=&d->associations[i]; if(strcmp(local,a->local_ip) || port!=a->udp_port || strcmp(source,a->peer_ip))continue; r=a->tolerant?schcvx_rx_tolerant_receive(&s->tolerant,data,len,now,&out):schcvx_rx_ordered_receive(&s->ordered,data,len,now,&out); if(r==1) return d->io.write_frame(d->io.context,i,out.frame,out.frame_len); return r; } return -1; }
void schcvx_sweep(struct schcvx_daemon *d,uint64_t now) { size_t i; if(!d)return; for(i=0;i<d->config.count;i++) if(d->config.assoc[i].tolerant) schcvx_rx_tolerant_sweep(&d->associations[i].tolerant,now); else schcvx_rx_ordered_sweep(&d->associations[i].ordered,now); }
bool schcvx_next_deadline(const struct schcvx_daemon *d,uint64_t *deadline) { size_t i; uint64_t t; bool found=false; if(!d || !deadline)return false; for(i=0;i<d->config.count;i++) { bool yes=d->config.assoc[i].tolerant?schcvx_rx_tolerant_next_deadline(&d->associations[i].tolerant,&t):schcvx_rx_ordered_next_deadline(&d->associations[i].ordered,&t); if(yes && (!found || t<*deadline)) { *deadline=t; found=true; } } return found; }
const struct schcvx_counters *schcvx_daemon_counters(const struct schcvx_daemon *d,size_t i) { return d && i<d->config.count?&d->associations[i].counters:NULL; }

void schcvx_count_bad_checksum(struct schcvx_daemon *d,const char *local,uint16_t port,const char *source) { size_t i; if(!d || !local || !source)return; for(i=0;i<d->config.count;i++) { const struct schcvx_config_assoc *a=&d->config.assoc[i]; if(!strcmp(local,a->local_ip) && port==a->udp_port && !strcmp(source,a->peer_ip)) { d->associations[i].counters.malformed++; return; } } }
