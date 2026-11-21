/* C ABI wrappers with no platform dependencies (host tests link this too). */
#include "nullk/securekernel.h"
#include "engine.h"

extern "C" int nk_securekernel_verify(const uint8_t *image, uint32_t size,
                                      nk_secure_report_t *report)
{
    return nksecure::verify_image(image, size, report);
}

extern "C" int nk_securekernel_sign(uint8_t *image, uint32_t size, uint32_t cap,
                                    uint32_t *out_size)
{
    return nksecure::sign_image(image, size, cap, out_size);
}
