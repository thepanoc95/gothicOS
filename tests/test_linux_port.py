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
            "include/nullk_linux/parasite.h",
            "include/nullk_linux/tty_console.h",
            "include/nullk_linux/user_shim.h",
            "src/lkm/main.c",
            "src/user/init_pid1.c",
            "src/user/initramfs_init.c",
            "src/user/nk64_syscall_linux.c",
            "src/user/tty_console.c",
            "configs/initramfs/init",
            "scripts/mkinitramfs.py",
            "patches/optional/README.md",
            "tests/test_abi_layout.c",
        ]
        for relative in required:
            self.assertTrue((PORT_ROOT / relative).is_file(), relative)

    def test_framebuffer_console_sources_are_removed(self):
        removed = [
            "include/nullk_linux/evdev_keycode.h",
            "include/nullk_linux/fb_console.h",
            "include/nullk_linux/fb_font.h",
            "include/nullk_linux/fb_gfx.h",
            "include/nullk_linux/fb_gui.h",
            "src/user/fb_console.c",
            "src/user/fb_font.c",
            "src/user/fb_gfx.c",
            "src/user/fb_gui.c",
            "tests/test_evdev_keycode.c",
        ]
        for relative in removed:
            self.assertFalse((PORT_ROOT / relative).exists(), relative)

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
        makefile = (PORT_ROOT / "Makefile").read_text()
        self.assertIn("src/user/tty_console.c", makefile)
        for source in ("fb_console.c", "fb_font.c", "fb_gfx.c", "fb_gui.c",
                       "test_evdev_keycode.c"):
            self.assertNotIn(source, makefile)

    def test_shell_console_and_framebuffer_backend(self):
        shim = (PORT_ROOT / "src/user/nk64_syscall_linux.c").read_text()
        self.assertIn("nullk.log", shim)
        self.assertIn("nk64_owns_console", shim)
        self.assertIn("FBIOGET_VSCREENINFO", shim)
        self.assertIn("DRM_IOCTL_MODE_SETCRTC", shim)
        self.assertIn("DRM_IOCTL_MODE_DIRTYFB", shim)

    def test_tty_console_replaces_the_framebuffer_tui(self):
        shim = (PORT_ROOT / "src/user/nk64_syscall_linux.c").read_text()
        tty = (PORT_ROOT / "src/user/tty_console.c").read_text()
        init = (PORT_ROOT / "src/user/init_pid1.c").read_text()

        self.assertIn("nk64_tty_console_init", init)
        for token in ("/dev/console", "/dev/tty0", "TIOCSCTTY", "ICANON",
                      "ECHO", "VMIN", "tcsetattr", "dup2"):
            self.assertIn(token, tty)
        self.assertNotIn("nk64_fb_console", shim)
        self.assertNotIn("struct pollfd", shim)
        self.assertNotIn("nk64_fb_console", shim + tty + init)

    def test_shell_engine_has_cwd_history_and_lvgl_hook(self):
        engine = (REPO_ROOT / "nullk" / "shared" / "shell64.c").read_text()
        header = (REPO_ROOT / "nullk" / "include" / "nullk" / "shell64.h").read_text()
        for command in ("sysinfo", "stat", "head", "wc", "grep", "history",
                        "cd", "ui lvgl"):
            self.assertIn(command, engine)
        self.assertNotIn('"gui"', engine)
        self.assertIn("nk_shell64_resolve", engine)
        self.assertIn("nk_shell64_history_push", engine)
        self.assertIn("char cwd[NK_VFS_PATH_MAX]", header)
        self.assertIn("nk_status_t (*ui_lvgl)(void)", header)
        self.assertNotIn("nk_status_t (*gui)(uint32_t command)", header)
        editor = (REPO_ROOT / "nullk" / "apps" / "sh64" / "main.c").read_text()
        self.assertNotIn("nk64_fb_console", editor)
        self.assertIn("\\033[H\\033[2J", editor)
        self.assertIn("sh_repaint_line", editor)
        self.assertIn("sh_complete_command", editor)
        self.assertIn("0x0cu", editor)

    def test_console_paths_are_documented(self):
        readme = (PORT_ROOT / "README.md").read_text()
        overview = (REPO_ROOT / "docs" / "LinuxPort.md").read_text()
        self.assertIn("console=ttyS0", readme)
        self.assertIn("tty0", readme)
        self.assertIn("framebuffer TUI", readme)
        self.assertIn("/dev/input/event*", readme)
        self.assertNotIn("framebuffer console", readme)
        self.assertNotIn("framebuffer console", overview)
        self.assertIn("/dev/input/event*", overview)


if __name__ == "__main__":
    unittest.main()
