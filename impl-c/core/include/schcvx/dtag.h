#ifndef SCHCVX_DTAG_H
#define SCHCVX_DTAG_H
#include <stdint.h>
struct schcvx_dtag { uint64_t next, startup_ns, quarantine_ns, last_ns, fraction; uint32_t rate, burst, tokens; unsigned bits; };
enum schcvx_dtag_error { SCHCVX_E_DTAG_CONFIG = 200, SCHCVX_E_DTAG_QUARANTINE, SCHCVX_E_DTAG_RATE, SCHCVX_E_DTAG_TIME };
int schcvx_dtag_init(struct schcvx_dtag *state, unsigned bits, uint64_t start, uint64_t startup_ns, uint64_t quarantine_ns, uint32_t rate, uint32_t burst);
int schcvx_dtag_allocate(struct schcvx_dtag *state, uint64_t now_ns, uint64_t *value);
#endif
