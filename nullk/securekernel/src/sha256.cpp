#include "sha256.h"

namespace nksecure {

static uint32_t load32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void store64_be(uint8_t *p, uint64_t v)
{
    store32_be(p, (uint32_t)(v >> 32));
    store32_be(p + 4, (uint32_t)v);
}

static uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static const uint32_t k_round[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

void Sha256::init()
{
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
    block_len_ = 0u;
    bit_len_ = 0u;
    for (uint32_t i = 0u; i < 64u; i++) {
        block_[i] = 0u;
    }
}

void Sha256::transform(const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;

    for (uint32_t i = 0u; i < 16u; i++) {
        w[i] = load32_be(block + i * 4u);
    }
    for (uint32_t i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^
                      (w[i - 15u] >> 3u);
        uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^
                      (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = state_[0]; b = state_[1]; c = state_[2]; d = state_[3];
    e = state_[4]; f = state_[5]; g = state_[6]; h = state_[7];

    for (uint32_t i = 0u; i < 64u; i++) {
        uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);

        t1 = h + s1 + ch + k_round[i] + w[i];
        t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const uint8_t *data, uint32_t len)
{
    if (data == 0 || len == 0u) {
        return;
    }
    bit_len_ += (uint64_t)len * 8u;

    while (len != 0u) {
        uint32_t space = 64u - block_len_;
        uint32_t take = (len < space) ? len : space;

        for (uint32_t i = 0u; i < take; i++) {
            block_[block_len_ + i] = data[i];
        }
        block_len_ += take;
        data += take;
        len -= take;

        if (block_len_ == 64u) {
            transform(block_);
            block_len_ = 0u;
        }
    }
}

void Sha256::finish(uint8_t out[32])
{
    uint64_t bits = bit_len_;

    block_[block_len_++] = 0x80u;
    if (block_len_ > 56u) {
        for (uint32_t i = block_len_; i < 64u; i++) {
            block_[i] = 0u;
        }
        transform(block_);
        block_len_ = 0u;
    }
    for (uint32_t i = block_len_; i < 56u; i++) {
        block_[i] = 0u;
    }
    store64_be(block_ + 56, bits);
    transform(block_);

    for (uint32_t i = 0u; i < 8u; i++) {
        store32_be(out + i * 4u, state_[i]);
    }
}

void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *first, uint32_t first_len,
                 const uint8_t *second, uint32_t second_len,
                 uint8_t out[32])
{
    uint8_t effective[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t inner[32];
    Sha256 sha;

    for (uint32_t i = 0u; i < 64u; i++) {
        effective[i] = 0u;
    }
    if (key_len > 64u) {
        sha.init();
        sha.update(key, key_len);
        sha.finish(effective);
        key_len = 32u;
    } else if (key_len != 0u) {
        for (uint32_t i = 0u; i < key_len; i++) {
            effective[i] = key[i];
        }
    }
    for (uint32_t i = 0u; i < 64u; i++) {
        ipad[i] = (uint8_t)(effective[i] ^ 0x36u);
        opad[i] = (uint8_t)(effective[i] ^ 0x5cu);
    }

    sha.init();
    sha.update(ipad, 64u);
    sha.update(first, first_len);
    sha.update(second, second_len);
    sha.finish(inner);

    sha.init();
    sha.update(opad, 64u);
    sha.update(inner, 32u);
    sha.finish(out);
}

} /* namespace nksecure */
