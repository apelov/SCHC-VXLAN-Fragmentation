#include "schcvx/wire.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t rule = 1;
    struct schcvx_msg msg;
    for (int v = 0; v < 2; v++) {
        struct schcvx_assoc_wire wire = {(enum schcvx_variant)v, &rule, 1, 0};
        (void)schcvx_classify(&wire, data, size, &msg);
    }
    return 0;
}
