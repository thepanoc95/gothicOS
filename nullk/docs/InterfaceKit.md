# InterfaceKit

InterfaceKit is the board-neutral graphical toolkit for null_kernel. On AMD64 it
backs a ring-3 desktop server; on embedded targets it is a reduced library of
common controls that can draw into any caller-owned RGBA surface. The visual
style is watercolor / **grained glass**: translucent pigment washes,
deterministic paper grain and soft bleed at widget edges.

The toolkit is deliberately split so no board has to take the AMD64 ABI:

| Header | Purpose |
| --- | --- |
| `include/nullk/interfacekit.h` | Portable RGBA pixmaps, generic framebuffer descriptor, font and blitting core |
| `include/nullk/interfacekit_widgets.h` | Common watercolor/grained-glass widgets and style presets |
| `include/nullk/interfacekit_server.h` | AMD64-only window server and routed input event API |
| `shared/interfacekit_gfx.c` | Portable drawing implementation |
| `shared/interfacekit_widgets.c` | Portable widget implementation |

## Profiles and common widgets

Every profile supports the same core controls:

- `interfacekit_widget_panel()`
- `interfacekit_widget_label()`
- `interfacekit_widget_button()`
- `interfacekit_widget_progress()`

The full desktop profile (AMD64 and ARM64) also adds:

- `interfacekit_widget_toggle()`
- `interfacekit_widget_slider()`

`interfacekit_widgets.h` selects the profile automatically: `__x86_64__` or
`_M_X64` selects AMD64, `__aarch64__` or `_M_ARM64` selects ARM64, and
`INTERFACEKIT_EMBEDDED` forces the reduced set. Cortex-M, RP2040 and Pico W
builds define `INTERFACEKIT_EMBEDDED`. The reduced profile keeps the watercolor
look but lowers grain and disables edge bleed so small framebuffers stay cheap.

There is no ARM64 board directory in this repository yet. The widget sources
are architecture-independent and already keyed to `__aarch64__`, so an ARM64
port only needs to provide a framebuffer/pixmap and compile the same shared
objects; it does not need the AMD64 syscall or window-server headers.

## Grained-glass rendering

Widgets take an `interfacekit_style_t` rather than reading global state. A style
carries the wash, edge pigment, text colour, accent colour, grain amount and
bleed radius. `interfacekit_glass_fill()` is the common primitive:

- translucent colours are alpha-blended into the destination pixmap,
- a small integer hash gives each pixel deterministic paper grain,
- neighbouring cells add a low-frequency watercolor bloom,
- edge pixels are mixed toward the style's pigment colour to simulate bleed.

All arithmetic is integer-only. There are no floats, heap allocations, locks,
timers or libc calls, so the same object links into ring 3, an embedded image or
a host test binary.

## AMD64 desktop architecture

```
Limine framebuffer request
        |
        v
nk64_boot_fb_init()             ring 0: metadata + PMM reservation
        |
        v
NK64_SC_FB_INFO / FB_MAP        ring 0: one bounded user mapping
        |
        v
InterfaceKit server             ring 3: windows, compositing, cursor, routing
        |
        v
InterfaceKit demo / clients     ring 3: common widgets into back buffers
```

`init64` starts the InterfaceKit server first, then the demo, SecureKernel,
`usb64` and `nksh`. The server calls `FB_MAP` exactly once; later claimants
receive `NK_STATUS_PERMISSION_DENIED`. The framebuffer is mapped at
`NK64_FB_VBASE` (`0x0000000040000000`) with user read/write and no-execute
permissions.

ABI v3 `TASK_CREATE` places these services in the same address space, so
clients call the server API directly. There is no IPC serialization in this
profile: the API is cooperative and its queues are bounded. Strong isolation
between graphical clients would need separate address spaces plus the still
unimplemented AMD64 IPC endpoints.

### Kernel primitives

| Syscall | Number | Contract |
| --- | ---: | --- |
| `NK64_SC_FB_INFO` | 26 | Copy `nk64_fbinfo_t` to the caller; `NOT_FOUND` if Limine supplied no display. |
| `NK64_SC_FB_MAP` | 27 | Claim the framebuffer; returns `NK64_FB_VBASE` or a status error. |
| `NK64_SC_INPUT_READ` | 28 | Drain up to 16 `nk64_input_event_t` records. |

`libnk64` wraps these as `nk64_fb_info()`, `nk64_fb_map()` and
`nk64_input_read()`. The kernel's Limine metadata is converted once into a
generic `interfacekit_framebuffer_t`; after that, all drawing code is portable.

The PS/2 driver supports legacy set-1 scancodes, shift/ctrl/alt modifiers,
caps lock, extended navigation keys and three-byte mouse packets. It appends
events to a 64-entry kernel ring; overflow drops newest events rather than
overwriting queued input.

### Server model

- A 1 MiB static RGBA pool backs at most `INTERFACEKIT_MAX_WINDOWS` (8) windows.
- Each window has a front and back pixmap. `interfacekit_window_backbuffer()`
  returns the back pixmap; `interfacekit_window_present()` swaps buffers, marks
  the window dirty and raises it.
- Input is routed to the topmost window under the pointer. Keyboard events go
  to the focused window. Client event queues hold 16 records each.
- The desktop composites a vertical gradient, taskbar, window titles and client
  content in z-order.

Clients never call framebuffer syscalls and never touch PS/2 ports.

## Demo client

`apps/interfacekit_demo/main.c` creates one window and renders the common
widgets through `interfacekit_widgets.h`:

- a typed text label (printable keys append, Backspace edits, Enter clears),
- a pressed-state Send button,
- an animated progress bar,
- a toggle and slider on full desktop profiles,
- mouse position and click feedback.

The AMD64 demo uses the server API from `interfacekit_server.h`. Embedded code
uses only `interfacekit.h` and `interfacekit_widgets.h`.

## Embedded integration

The generic Makefile compiles `shared/interfacekit_gfx.c` and
`shared/interfacekit_widgets.c` into the Cortex-M and RP2040 images with
`-DINTERFACEKIT_EMBEDDED`. The Pico SDK project adds the same two sources and
the same definition for Pico W. Embedded services can allocate a small static
pixmap and draw the reduced widget set without pulling in `abi64.h`, Limine,
PS/2 or the window server.

## Build and test

```sh
make -C nullk test
make -C nullk BOARD=amd64 test
make -C nullk BOARD=amd64 all
make -C nullk BOARD=amd64 qemu-ui LIMINE_DIR=/path/to/limine
```

`qemu-ui` boots the same AMD64 ISO without `-nographic` so Limine can provide a
framebuffer. The original `qemu` target remains headless with serial output; if
no framebuffer is reported, InterfaceKit logs that the desktop is disabled and
other services continue normally. Serial log lines from the desktop path are
prefixed `[InterfaceKit]` and `[InterfaceKitDemo]`.

`make -C nullk BOARD=amd64 test` runs two InterfaceKit host suites:

- `tests/test_interfacekit.c` covers decoders, RGB888/RGB565 packing, RGBA
  blending, clipping, glyphs, framebuffer blits and every full-profile widget.
- `tests/test_interfacekit_embedded.c` is compiled with
  `-DINTERFACEKIT_EMBEDDED` and verifies that the common reduced widget set
  renders without any AMD64 headers.

Both suites link the exact `shared/interfacekit_*.c` objects used by the images.

## Limitations

- No GPU acceleration, hardware cursor, UTF-8 text or window resizing.
- PS/2 only on AMD64; USB HID is not part of this profile.
- The AMD64 framebuffer claim is permanent for the boot and is not released if
  the UI task exits.
- The shared-address-space desktop model is cooperative, not a security
  boundary between graphical clients.
- No ARM64 board tree exists yet; the portable widget layer is ARM64-ready but
  has no bootable ARM64 image in this repository.
