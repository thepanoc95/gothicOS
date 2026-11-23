"""Contract checks for the native null_kernel Linux ABI v3 port."""

from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
PORT_ROOT = REPO_ROOT / "nullk" / "ports" / "linux"


class NativeLinuxPortTests(unittest.TestCase):
    def test_native_port_sources_are_declared(self):
        required = [
            "README.md",
            "Makefile",
            "Kbuild",
            "include/nullk_linux/abi3_ioctl.h",
            "include/nullk_linux/abi64_kernel.h",
            "include/nullk_linux/evdev_keycode.h",
            "include/nullk_linux/fb_console.h",
            "include/nullk_linux/fb_font.h",
            "include/nullk_linux/fb_gfx.h",
            "include/nullk_linux/fb_gui.h",
            "include/nullk_linux/parasite.h",
            "include/nullk_linux/user_shim.h",
            "src/lkm/main.c",
            "src/user/fb_console.c",
            "src/user/fb_font.c",
            "src/user/fb_gfx.c",
            "src/user/fb_gui.c",
            "src/user/init_pid1.c",
            "src/user/initramfs_init.c",
            "src/user/nk64_syscall_linux.c",
            "configs/initramfs/init",
            "scripts/mkinitramfs.py",
            "patches/optional/README.md",
            "tests/test_abi_layout.c",
            "tests/test_evdev_keycode.c",
        ]
        for relative in required:
            self.assertTrue((PORT_ROOT / relative).is_file(), relative)

    def test_old_cython_port_is_removed(self):
        old_root = REPO_ROOT / "ports" / "linux"
        self.assertFalse(old_root.exists())
        for relative in ("build.py", "setup.py", "linux_port.pyx",
                         "nullkern_lkm.c"):
            self.assertFalse((old_root / relative).exists(), relative)

    def test_build_recipe_uses_native_make_targets(self):
        recipe = (PORT_ROOT / "build.json").read_text()
        self.assertIn("nullk/ports/linux", recipe)
        self.assertIn("test", recipe)

    def test_shell_console_and_framebuffer_backend(self):
        shim = (PORT_ROOT / "src/user/nk64_syscall_linux.c").read_text()
        self.assertIn("nullk.log", shim)
        self.assertIn("nk64_owns_console", shim)
        self.assertIn("FBIOGET_VSCREENINFO", shim)
        self.assertIn("DRM_IOCTL_MODE_SETCRTC", shim)
        self.assertIn("DRM_IOCTL_MODE_DIRTYFB", shim)

    def test_standalone_framebuffer_console(self):
        shim = (PORT_ROOT / "src/user/nk64_syscall_linux.c").read_text()
        console = (PORT_ROOT / "src/user/fb_console.c").read_text()
        makefile = (PORT_ROOT / "Makefile").read_text()

        self.assertIn("nk64_fb_console_init", shim)
        self.assertIn("nk64_fb_console_input_get", shim)
        self.assertIn("nk64_fb_console_output_push", shim)
        self.assertIn("poll(&pfd, 1u, 10)", shim)
        self.assertIn("/dev/input/event%u", console)
        self.assertIn("EVIOCGBIT(EV_KEY", console)
        self.assertIn("g_kbd_seen", console)  # never open one evdev node twice
        self.assertIn("nk64_evdev_key_to_sequence", console)
        self.assertIn("ev->value == 2", console)  # autorepeat is not a press
        self.assertIn("nkfb_glyph", console)
        self.assertNotIn("interfacekit_glyph", console)
        self.assertNotIn("interfacekit_server", console)
        self.assertIn("nkfb_gui_dashboard", console)
        for source in ("src/user/fb_console.c", "src/user/fb_font.c",
                       "src/user/fb_gfx.c", "src/user/fb_gui.c",
                       "tests/test_evdev_keycode.c"):
            self.assertIn(source, makefile)

    def test_rounded_console_font_replaces_the_3x5_face(self):
        font = (PORT_ROOT / "src/user/fb_font.c").read_text()
        header = (PORT_ROOT / "include/nullk_linux/fb_font.h").read_text()
        self.assertIn("NKFB_FONT_W        6u", header)
        self.assertIn("NKFB_FONT_H        10u", header)
        self.assertIn("NKFB_FONT_CHARS    95u", header)
        self.assertIn("0x20", font)
        self.assertIn("0x7e", font)
        # Lowercase must have its own art: 'l' and 'I' differ, as do 's' and 'B'.
        rows = {}
        for line in font.splitlines():
            if line.strip().startswith("/* 0x") and "'" in line:
                tag = line.split("'")[1]
                if len(tag) == 1:
                    rows[tag] = line.split("{")[1].rstrip("}, ") + "}"
        self.assertNotEqual(rows.get("l"), rows.get("I"))
        self.assertNotEqual(rows.get("s"), rows.get("B"))
        self.assertNotEqual(rows.get("a"), rows.get("A"))

    def test_gui_engine_paints_console_elements(self):
        gui = (PORT_ROOT / "src/user/fb_gui.c").read_text()
        gfx = (PORT_ROOT / "src/user/fb_gfx.c").read_text()
        for widget in ("nkfb_ui_window", "nkfb_ui_card", "nkfb_ui_row",
                       "nkfb_ui_progress", "nkfb_ui_button", "nkfb_ui_table",
                       "nkfb_ui_list", "nkfb_ui_badge", "nkfb_ui_statusbar",
                       "nkfb_gui_dashboard", "nkfb_gui_showcase"):
            self.assertIn(widget, gui)
        self.assertIn("nkfb_fill_round", gfx)
        self.assertIn("nkfb_fill_gradient", gfx)
        self.assertNotIn("interfacekit", gui + gfx)

    def test_evdev_keycodes_are_not_treated_as_alphabetical(self):
        keymap = (PORT_ROOT / "include/nullk_linux/evdev_keycode.h").read_text()
        self.assertIn("[KEY_L]         = 'l'", keymap)
        self.assertIn("[KEY_S]         = 's'", keymap)
        self.assertNotIn("return (uint8_t)('a' + (code - KEY_A))", keymap)
        test = (PORT_ROOT / "tests/test_evdev_keycode.c").read_text()
        self.assertIn("KEY_L, 0, 0) == (uint8_t)'l'", test)
        self.assertIn("KEY_S, 0, 0) == (uint8_t)'s'", test)

    def test_keymap_covers_editing_keys_and_numpad(self):
        keymap = (PORT_ROOT / "include/nullk_linux/evdev_keycode.h").read_text()
        for code in ("KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT",
                     "KEY_HOME", "KEY_END", "KEY_DELETE", "KEY_KP1",
                     "KEY_KPENTER", "KEY_KPEQUAL"):
            self.assertIn(code, keymap)
        self.assertIn("nk64_evdev_key_to_sequence", keymap)
        self.assertIn("EV_KEY value 2", keymap)

    def test_shell_engine_has_cwd_history_and_gui_hooks(self):
        engine = (REPO_ROOT / "nullk" / "shared" / "shell64.c").read_text()
        header = (REPO_ROOT / "nullk" / "include" / "nullk" / "shell64.h").read_text()
        for command in ("sysinfo", "stat", "head", "wc", "grep", "history",
                        "cd", "gui"):
            self.assertIn(command, engine)
        self.assertIn("nk_shell64_resolve", engine)
        self.assertIn("nk_shell64_history_push", engine)
        self.assertIn("char cwd[NK_VFS_PATH_MAX]", header)
        self.assertIn("nk_status_t (*gui)(uint32_t command)", header)
        editor = (REPO_ROOT / "nullk" / "apps" / "sh64" / "main.c").read_text()
        self.assertIn("sh_repaint_line", editor)
        self.assertIn("sh_complete_command", editor)
        self.assertIn("0x0cu", editor)

    def test_console_paths_are_documented(self):
        readme = (PORT_ROOT / "README.md").read_text()
        overview = (REPO_ROOT / "docs" / "LinuxPort.md").read_text()
        self.assertIn("console=ttyS0", readme)
        self.assertIn("tty0", readme)
        self.assertIn("framebuffer console", readme)
        self.assertIn("/dev/input/event*", readme)
        self.assertIn("framebuffer console", overview)
        self.assertIn("/dev/input/event*", overview)


if __name__ == "__main__":
    unittest.main()
