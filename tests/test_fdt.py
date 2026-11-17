import unittest

from gothicos.OSBuild.fdt import cpu_count_from_fdt as build_cpu_count
from internal.nullkern.fdt import cpu_count_from_blob as runtime_cpu_count
from internal.nullkern.kernel import Kernel

try:
    from .fdt_fixture import build_fdt
except ImportError:
    from fdt_fixture import build_fdt


class FDTTests(unittest.TestCase):
    def test_build_and_runtime_parsers_count_enabled_cpus(self):
        blob = build_fdt(("okay", "disabled", "okay", "fail"))

        self.assertEqual(build_cpu_count(blob), 2)
        self.assertEqual(runtime_cpu_count(blob), 2)

    def test_kernel_topology_comes_from_fdt(self):
        kernel = Kernel.from_fdt(build_fdt(("okay", "okay")))

        self.assertEqual(kernel.core_count, 2)
        self.assertTrue(kernel.multicore_enabled)

    def test_invalid_fdt_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "invalid FDT magic"):
            runtime_cpu_count(b"x" * 40)


if __name__ == "__main__":
    unittest.main()