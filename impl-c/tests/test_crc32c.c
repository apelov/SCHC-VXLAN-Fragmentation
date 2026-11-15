#include "schcvx/crc32c.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: failing check: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void)
{
    uint8_t packet[1523] = {0x00, 0x08, 0x00, 0x00, 0x00,
                            0x00, 0x30, 0x39, 0x00};
    const size_t chunks[] = {1, 7, 0, 257, 3, 1024, 0, 231};
    uint32_t state;
    size_t offset = 0;
    uint8_t serialized[4];
    const uint8_t expected[4] = {0x94, 0xdc, 0x7a, 0x0e};

    CHECK(schcvx_crc32c("123456789", 9) == UINT32_C(0xe3069283));
    for (size_t i = 0; i < 1514; ++i) {
        packet[9 + i] = (uint8_t)(i % 256);
    }
    CHECK(schcvx_crc32c(packet, sizeof(packet)) == UINT32_C(0x94dc7a0e));

    schcvx_crc32c_init(&state);
    CHECK(state == UINT32_C(0xffffffff));
    schcvx_crc32c_update(&state, NULL, 0);
    CHECK(schcvx_crc32c_final(state) == 0);
    CHECK(schcvx_crc32c(NULL, 0) == 0);
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        uint32_t before = state;
        CHECK(chunks[i] <= sizeof(packet) - offset);
        schcvx_crc32c_update(&state, packet + offset, chunks[i]);
        if (chunks[i] == 0) {
            CHECK(state == before);
        }
        offset += chunks[i];
    }
    CHECK(offset == sizeof(packet));
    CHECK(schcvx_crc32c_final(state) == schcvx_crc32c(packet, sizeof(packet)));
    {
        uint32_t before = state;
        schcvx_crc32c_update(&state, NULL, 0);
        CHECK(state == before);
    }

    schcvx_put_be32(serialized, UINT32_C(0x94dc7a0e));
    CHECK(memcmp(serialized, expected, sizeof(expected)) == 0);
    return 0;
}
