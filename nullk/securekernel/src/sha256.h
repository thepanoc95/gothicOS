/* Minimal freestanding SHA-256/HMAC-SHA256 used by SecureKernel. */
#ifndef NULLK_SECURE_SHA256_H
#define NULLK_SECURE_SHA256_H

#include <stdint.h>

namespace nksecure {

class Sha256 {
public:
    void init();
    void update(const uint8_t *data, uint32_t len);
    void finish(uint8_t out[32]);

private:
    uint32_t state_[8];
    uint8_t block_[64];
    uint32_t block_len_;
    uint64_t bit_len_;

    void transform(const uint8_t block[64]);
};

/*
 * HMAC over two contiguous ranges. SecureKernel hashes an ELF with its
 * signature section excluded, which naturally maps to (before, after).
 */
void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *first, uint32_t first_len,
                 const uint8_t *second, uint32_t second_len,
                 uint8_t out[32]);

} /* namespace nksecure */

#endif /* NULLK_SECURE_SHA256_H */
