# null_kernel Pico W port

CMake build of `null_kernel` for RP2040 / Pico W using the Raspberry Pi Pico
SDK and its TinyUSB CDC ACM stdio backend.

```sh
cmake -S nullk/pico -B build/NullKernel/pico -DPICO_BOARD=pico_w
cmake --build build/NullKernel/pico -j4
sudo picotool load -u -x build/NullKernel/pico/null_kernel_pico.uf2
```

`PICO_SDK_PATH` must point at a Pico SDK checkout with TinyUSB initialized.
The kernel sources are shared with the freestanding Cortex-M3 build; this port
adds `sdk_main.c`, `arch.c` (SDK arch hooks) and `context_sdk.S` (RP2040 M0+
context switch under the SDK vector names). `_os_compiler_rt.c` is excluded
because newlib provides those symbols.

See `../README.md` for the full build and flashing instructions.
