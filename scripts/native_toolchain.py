import os

Import("env")

toolchain_dir = env.PioPlatform().get_package_dir("toolchain-gccmingw32")
if not toolchain_dir:
    raise RuntimeError("PlatformIO package toolchain-gccmingw32 is not installed")

bin_dir = os.path.join(toolchain_dir, "bin")
gcc = os.path.join(bin_dir, "gcc.exe")
gxx = os.path.join(bin_dir, "g++.exe")
if not os.path.isfile(gcc) or not os.path.isfile(gxx):
    raise RuntimeError("MinGW compiler executables are missing from the PlatformIO package")

env.PrependENVPath("PATH", bin_dir)
env.Replace(CC=gcc, CXX=gxx)
env.Append(LINKFLAGS=["-static", "-static-libgcc", "-static-libstdc++"])
