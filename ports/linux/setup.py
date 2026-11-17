"""Build the Cython Linux port as a CPython extension module."""

from pathlib import Path

from Cython.Build import cythonize
from setuptools import Extension, setup

HERE = Path(__file__).resolve().parent

extension = Extension(
    "gothicos_linux_port",
    sources=[str(HERE / "linux_port.pyx")],
    include_dirs=[str(HERE / "include")],
)

setup(
    name="gothicos-linux-port",
    version="1.1.1",
    description="Cython userspace NULLKern runtime with a thin Linux LKM transport",
    ext_modules=cythonize([extension], language_level=3),
)
