#ifndef SCHCVX_CRC32C_H
#define SCHCVX_CRC32C_H

#include <stddef.h>
#include <stdint.h>

/* State holds the unfinalized CRC register. */
void schcvx_crc32c_init(uint32_t *state);
/* data may be NULL when len is zero; otherwise it addresses len bytes. */
void schcvx_crc32c_update(uint32_t *state, const void *data, size_t len);
uint32_t schcvx_crc32c_final(uint32_t state);
uint32_t schcvx_crc32c(const void *data, size_t len);
void schcvx_put_be32(uint8_t out[4], uint32_t v);

#endif
