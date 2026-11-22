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
            "include/nullk_linux/user_shim.h",
            "src/lkm/main.c",
            "src/user/init_pid1.c",
            "src/user/initramfs_init.c",
            "src/user/nk64_syscall_linux.c",
            "configs/initramfs/init",
            "scripts/mkinitramfs.py",
            "patches/optional/README.md",
            "tests/test_abi_layout.c",
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


if __name__ == "__main__":
    unittest.main()
