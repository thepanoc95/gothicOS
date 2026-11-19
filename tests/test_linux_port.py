import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PORT_ROOT = REPO_ROOT / "ports" / "linux"


def load_build_module():
    spec = importlib.util.spec_from_file_location("linux_port_build", PORT_ROOT / "build.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LinuxPortBuildTests(unittest.TestCase):
    def test_linux_port_sources_are_declared(self):
        module = load_build_module()
        self.assertEqual(module.SOURCE_FILES, ("linux_port.pyx",))
        for relative in module.SOURCE_FILES + module.HEADER_FILES:
            self.assertTrue((PORT_ROOT / relative).is_file(), relative)
        self.assertTrue((PORT_ROOT / "nullkern_lkm.c").is_file())
        self.assertTrue((PORT_ROOT / "Makefile").is_file())

    def test_cython_translates_linux_port(self):
        module = load_build_module()
        with tempfile.TemporaryDirectory() as output_dir:
            generated = module.build_port(output_dir, python=sys.executable)
            self.assertEqual(len(generated), 1)
            self.assertTrue(generated[0].is_file())
            source = generated[0].read_text(encoding="utf-8")
            self.assertIn("gothicos_linux_port.h", source)
            self.assertIn("handoff_status", source)
            self.assertIn("handoff_start", source)
            self.assertGreater(len(source), 1000)

    def test_shared_ioctl_header_is_in_sync(self):
        header = (PORT_ROOT / "include" / "gothicos_linux_port.h").read_text(encoding="utf-8")
        self.assertIn("GOTHICOS_LKM_IOC_GET_INFO", header)
        self.assertIn("GOTHICOS_LKM_IOC_RAISE_IRQ", header)
        self.assertIn("GOTHICOS_LKM_IOC_PORT_SEND", header)
        self.assertIn("GOTHICOS_LKM_IOC_SCHED_HINT", header)
        self.assertIn("GOTHICOS_LKM_IOC_HANDOFF_STATUS", header)
        self.assertIn("GOTHICOS_LKM_IOC_HANDOFF_START", header)
        self.assertIn("GOTHICOS_LKM_IOC_HANDOFF_STOP", header)
        self.assertIn("gothicos_lkm_handoff_info_t", header)


if __name__ == "__main__":
    unittest.main()
