#include "schcvx/counters.h"
#include <inttypes.h>
#include <stdio.h>
size_t schcvx_counters_format(const struct schcvx_counters *c, char *out, size_t capacity) {
    size_t used=0;
#define SCHCVX_FORMAT(n) do { int count=snprintf(used<capacity ? out+used : NULL, used<capacity ? capacity-used : 0, #n " %" PRIu64 "\n", c->n); if (count>0) used+=(size_t)count; } while (0);
    SCHCVX_COUNTER_FIELDS(SCHCVX_FORMAT)
#undef SCHCVX_FORMAT
    return used;
}
