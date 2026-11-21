/* Internal SecureKernel engine declarations (not part of the C ABI). */
#ifndef NULLK_SECURE_ENGINE_H
#define NULLK_SECURE_ENGINE_H

#include <stdint.h>
#include "nullk/securekernel.h"

namespace nksecure {

int verify_image(const uint8_t *image, uint32_t size, nk_secure_report_t *report);
int sign_image(uint8_t *image, uint32_t size, uint32_t cap, uint32_t *out_size);

} /* namespace nksecure */

#endif /* NULLK_SECURE_ENGINE_H */
