from setuptools import setup, Extension
import sys
import os

src_dir = os.path.join("src")

ext = Extension(
    "arraypicture",
    sources=[
        os.path.join(src_dir, "_arraypicture.cpp"),
        os.path.join(src_dir, "arraypicture.cpp"),
    ],
    include_dirs=[src_dir],
    language="c++",
    libraries=["user32", "gdi32"],
    #extra_compile_args=["/std:c++17", "/permissive-", "/W4", "/DUNICODE", "/D_UNICODE"]
    extra_compile_args=["/std:c++17", "/Zi","/Od","/DDEBUG", "/W4", "/DUNICODE", "/D_UNICODE"]
    if sys.platform == "win32" else ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic"],
    extra_link_args=["/DEBUG"],
)

setup(
    ext_modules=[ext],
)
