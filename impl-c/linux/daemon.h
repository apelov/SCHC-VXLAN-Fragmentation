#ifndef SCHCVX_DAEMON_H
#define SCHCVX_DAEMON_H
#include "config.h"
#include "schcvx/counters.h"
struct schcvx_daemon;
struct schcvx_io {
 void *context;
 int (*send_datagram)(void *context, size_t association, const uint8_t *data, size_t len);
 int (*write_frame)(void *context, size_t association, const uint8_t *frame, size_t len);
 /* Must return monotonic nanoseconds; used between fragment emissions. */
 uint64_t (*now)(void *context);
};
struct schcvx_daemon *schcvx_daemon_create(const struct schcvx_config *config, const struct schcvx_io *io, uint64_t startup_ns);
void schcvx_daemon_destroy(struct schcvx_daemon *daemon);
int schcvx_process_frame(struct schcvx_daemon *daemon, size_t association, const uint8_t *frame, size_t len, uint64_t now);
/* IP strings are canonical dotted decimal; the caller has verified the UDP checksum. */
int schcvx_process_datagram(struct schcvx_daemon *daemon, const char *local_ip, uint16_t destination_port, const char *source_ip, const uint8_t *data, size_t len, uint64_t timestamp);
void schcvx_count_bad_checksum(struct schcvx_daemon *daemon, const char *local_ip, uint16_t destination_port, const char *source_ip);
void schcvx_sweep(struct schcvx_daemon *daemon, uint64_t now);
bool schcvx_next_deadline(const struct schcvx_daemon *daemon, uint64_t *deadline);
const struct schcvx_counters *schcvx_daemon_counters(const struct schcvx_daemon *daemon, size_t association);
#endif
