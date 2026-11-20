/* Entry point for the Pico SDK RP2040/Pico W port. */
#include <stdio.h>

#include "pico/stdlib.h"

#include "../nk/kern.h"

int main(void) {
    /* Initializes clocks, TinyUSB CDC and stdio before the kernel writes its
     * first log line. No GPIO UART is used. */
    stdio_init_all();

    nkk_boot();
    return 0; /* nkk_boot() never returns */
}
