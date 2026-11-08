# PlatformIO pre-script for [env:starter] (spec 048): lwIP with its features, which NAT
# needs (the setup network's browser reaches GitHub through the clock). The firmware's
# build_flags ask for the variant without them (PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY_LOW_FLASH,
# 10 KB less); the core picks its lwIP library from the flags before build_unflags
# applies, so the flag is taken out here, first. Without it the core's default is the
# variant with features at the same MSS (536): lwip2-536-feat.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

FLAG = "PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY_LOW_FLASH"
flags = []
for item in env.get("BUILD_FLAGS", []):
    flags.append(item.replace("-D " + FLAG, "").replace("-D" + FLAG, "") if FLAG in item else item)
env.Replace(BUILD_FLAGS=flags)
