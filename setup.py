from setuptools import setup, Extension
import sys, os

src_dir = "src"

ext = Extension(
    "arraypicture",
    sources=[
        os.path.join(src_dir, "_arraypicture_abi3.cpp"),
        os.path.join(src_dir, "arraypicture.cpp"),
    ],
    include_dirs=[src_dir],
    language="c++",
    libraries=["user32", "gdi32"],
    define_macros=[
        ("Py_LIMITED_API", "0x03080000"),  # abi3 from Python 3.8
        ("UNICODE", None),
        ("_UNICODE", None),
        ("NOMINMAX", None),
    ],
    extra_compile_args=(
        ["/std:c++17", "/permissive-", "/W4"]
        if sys.platform == "win32" else
        ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic"]
    ),
    extra_link_args=[],
    py_limited_api=True,  # key to abi3-weel
)

setup(
    ext_modules=[ext],
)
