# PlatformIO post-script (the firmware's extra_scripts, not the starter's): link-time
# optimisation of our own sources and the libraries (spec 047,
# docs/decisions/2026-11-08-link-time-optimisation.md): the image 13.4 KB smaller and its
# .bin.gz 8.4 KB smaller (src/ alone: 10.2 and 6.7 KB). IRAM unchanged, crash addresses
# still decode to functions.
#
# Flash data in headers must not be a C++17 inline variable: under LTO it loses its
# PROGMEM section and lands in RAM (use static).
#
# src/ and the libraries are compiled with -flto, the core and the SDK not: LTO over them
# breaks their link order (undefined ets_intr_lock, vPortFree ...). Nor ESP8266WiFi: its
# BearSSLHelpers defines SigningVerifier_verify, which only the core's Updater uses, and
# LTO drops it (keeping it with --undefined pulls BearSSL's verifier in: 27 KB). Library
# archives need the LTO-aware gcc-ar, or the linker sees no symbols in them. The core's
# linker script puts code in flash by object name (*.c.o, *.cpp.o); LTO's objects are
# *.ltrans.o, and without a pattern for them their code lands in IRAM, which overflows.
import os

Import("env", "projenv")  # noqa: F821 (provided by PlatformIO/SCons)

projenv.Append(CCFLAGS=["-flto"])
for lib in env.GetLibBuilders():
    if lib.name != "ESP8266WiFi":
        lib.env.Append(CCFLAGS=["-flto"])
        lib.env.Replace(AR="xtensa-lx106-elf-gcc-ar", RANLIB="xtensa-lx106-elf-gcc-ranlib")
env.Append(LINKFLAGS=["-flto", "-Os", "-ffunction-sections", "-fdata-sections",
                      "-mtext-section-literals", "-mlongcalls"])


def flash_for_lto_objects(target, source, env):
    path = os.path.join(env.subst("$BUILD_DIR"), "ld", "local.eagle.app.v6.common.ld")
    script = open(path).read()
    if "ltrans" not in script:
        line = "    *.c.o(.literal* .text*)\n"
        assert line in script, "the core's linker script changed: check where code goes"
        script = script.replace(line, line + "    *.ltrans.o(.literal* .text*)\n", 1)
        with open(path, "w") as f:
            f.write(script)


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", flash_for_lto_objects)
