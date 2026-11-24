/**
 * lv_conf.h - LVGL configuration for null_kernel / gothicOS.
 *
 * Included with LV_CONF_INCLUDE_SIMPLE (see lvgl_port.h). Only the settings
 * that differ from LVGL's defaults are listed here; everything else falls back
 * to lv_conf_internal.h.
 *
 * Constraints this file encodes:
 *   - freestanding: no malloc, strlen, sprintf or stdio from the C library,
 *     LVGL's built-in implementations are used instead
 *   - single-threaded: LVGL is driven from one task, so LV_USE_OS is none
 *   - 32 bpp XRGB8888, matching the Limine framebuffer handed to ABI v3 tasks
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/

/* 32: XRGB8888. The framebuffer port packs into 16/24/32 bpp targets. */
#define LV_COLOR_DEPTH 32

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/* Built-in allocator, string helpers and formatter: no libc in the kernel. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /* Static pool. Lives in the task's .bss; 192 KiB covers the dashboard,
     * a table and a handful of screens without falling back to mmap. */
    #define LV_MEM_SIZE (256 * 1024U)
#endif

/*=========================
   HAL SETTINGS
 *=========================*/

#define LV_DEF_REFR_PERIOD 33   /* 30 fps target */
#define LV_DPI_DEF 130

/* Single task drives LVGL; no threading layer and no interrupts. */
#define LV_USE_OS LV_OS_NONE

/* No logging: the kernel log is the console's job, and every message would
 * need a formatter. Raise LV_USE_LOG for debugging sessions. */
#define LV_USE_LOG 0

#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE  0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ    0

/* No float: soft-float formatters pull in far more code than they save. */
#define LV_USE_FLOAT 0
#define LV_USE_MATRIX 0

/*==================
   FONT USAGE
 *==================*/

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*==================
   WIDGETS
 *==================*/

/* Widgets keep LVGL's defaults (enabled). The desktop shell needs button,
 * label, slider, switch, checkbox, chart, bar, table, textarea, keyboard and
 * spinner; disabling the rest is a size decision for embedded profiles. */
#ifndef INTERFACEKIT_EMBEDDED
    #define LV_USE_KEYBOARD 1
    #define LV_USE_CALENDAR 0
    #define LV_USE_SCALE    0
#endif

/*========================
   THEME / LAYOUT DEFAULTS
 *========================*/

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif /* LV_CONF_H */
