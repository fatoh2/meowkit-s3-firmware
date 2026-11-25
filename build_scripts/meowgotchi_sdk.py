"""Route the pinned SDK raw-TX call through the app's scoped validator.

GNU --wrap cannot redirect a reference defined in the same object. Keep the
original validator intact, and change only its call-site relocation in a
build-local copy of libnet80211.a. The shared PlatformIO package is untouched.
"""
import hashlib
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess


SDK_SHA256 = "039bbc3105c8bdbcf0816c63e9c5f0067bde9750ef36b3a351f5b670f0fcf477"
ORIGINAL = b"ieee80211_raw_frame_sanity_check"
WRAPPER = b"__wrap_ieee80211_raw_frame_sanity_check"


def redirect(blob):
    """ELF32 little-endian Xtensa relocatable; reject unfamiliar layouts."""
    data = bytearray(blob)
    if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 94:
        raise ValueError("Expected an ELF32 Xtensa object")
    section_offset = struct.unpack_from("<I", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, section_offset+i*stride) for i in range(count)]
    sym_index = next(i for i, section in enumerate(sections) if section[1] == 2)
    sym = sections[sym_index]
    strings = sections[sym[6]]
    names = data[strings[4]:strings[4]+strings[5]]
    symbols = {}
    for index in range(sym[5]//sym[9]):
        offset = sym[4]+index*sym[9]
        name = struct.unpack_from("<I", data, offset)[0]
        symbols[bytes(names[name:names.index(0, name)])] = index
    original, wrapper = symbols[ORIGINAL], symbols[WRAPPER]
    # objcopy added a global absolute symbol. Make it an undefined reference
    # resolved to the application's wrapper while preserving the SDK definition.
    struct.pack_into("<H", data, sym[4]+wrapper*sym[9]+14, 0)
    changed = 0
    for section in sections:
        if section[1] not in (4, 9) or section[6] != sym_index:
            continue
        for offset in range(section[4], section[4]+section[5], section[9]):
            info = struct.unpack_from("<I", data, offset+4)[0]
            if info >> 8 == original:
                struct.pack_into("<I", data, offset+4, (wrapper << 8) | (info & 255))
                changed += 1
    # One call: literal-pool address plus Xtensa call-relaxation metadata.
    if changed != 2:
        raise ValueError(f"Expected two relocations for one SDK validator call, found {changed}")
    return data


def prepare(env):
    platform = env.PioPlatform()
    framework = Path(platform.get_package_dir("framework-arduinoespressif32"))
    source = framework / "tools/sdk/esp32s3/lib/libnet80211.a"
    if hashlib.sha256(source.read_bytes()).hexdigest() != SDK_SHA256:
        raise RuntimeError("MeowGotchi: review raw-TX hook for the changed SDK archive")
    toolchain = Path(platform.get_package_dir("toolchain-xtensa-esp32s3")) / "bin"
    suffix = ".exe" if os.name == "nt" else ""
    ar = str(toolchain / ("xtensa-esp32s3-elf-ar"+suffix))
    objcopy = str(toolchain / ("xtensa-esp32s3-elf-objcopy"+suffix))
    output = Path(env.subst("$BUILD_DIR")) / "meowgotchi-sdk"
    output.mkdir(parents=True, exist_ok=True)
    archive = output / source.name
    stamp = output / "patch.sha256"
    script = Path(env.subst("$PROJECT_DIR")) / "build_scripts/meowgotchi_sdk.py"
    digest = hashlib.sha256(script.read_bytes()).hexdigest()
    if not archive.exists() or not stamp.exists() or stamp.read_text() != digest:
        obj = output / "ieee80211_output.o"
        # `ar p` on this Windows toolchain translates binary stdout newlines.
        subprocess.run([ar, "x", str(source), obj.name], cwd=output, check=True)
        subprocess.run([objcopy, "--add-symbol", WRAPPER.decode()+"=0,global", str(obj)], check=True)
        obj.write_bytes(redirect(obj.read_bytes()))
        shutil.copyfile(source, archive)
        subprocess.run([ar, "r", str(archive), str(obj)], check=True)
        stamp.write_text(digest)
    env.Prepend(LIBPATH=[str(output)])
    objdump = str(toolchain / ("xtensa-esp32s3-elf-objdump"+suffix))
    nm = str(toolchain / ("xtensa-esp32s3-elf-nm"+suffix))

    def verify_link(source, target, env):
        elf = Path(str(target[0]))
        blob = elf.read_bytes()
        offset = struct.unpack_from("<I", blob, 32)[0]
        stride, count = struct.unpack_from("<HH", blob, 46)
        sections = [struct.unpack_from("<10I", blob, offset+i*stride) for i in range(count)]
        symbols = {}
        for line in subprocess.check_output([nm, "-P", str(elf)], text=True).splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[1].upper() != "U":
                symbols[fields[0]] = int(fields[2], 16)

        def check(caller, callee):
            disassembly = subprocess.check_output([
                objdump, "-d", "--disassemble="+caller, str(elf)
            ], text=True)
            if "<"+callee+">" in disassembly:
                return
            # Xtensa relaxes a long-distance call to l32r/callx8. Resolve the
            # literal's actual ELF bytes instead of trusting its nearest label.
            indirect = re.search(r"l32r\s+a8,\s*([0-9a-f]+)[^\n]*\n[^\n]*callx8\s+a8", disassembly)
            if indirect:
                address = int(indirect[1], 16)
                for section in sections:
                    if section[3] <= address and address+4 <= section[3]+section[5]:
                        value = struct.unpack_from("<I", blob, section[4]+address-section[3])[0]
                        if value == symbols[callee]:
                            return
            raise RuntimeError(f"MeowGotchi: linked call {caller} -> {callee} was not verified")

        check("esp_wifi_80211_tx", WRAPPER.decode())
        check(WRAPPER.decode(), ORIGINAL.decode())
        print("[MeowGotchi] Verified scoped validator in linked SDK call site")

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.VerboseAction(verify_link,"Checking MeowGotchi SDK call targets"))
    print("[MeowGotchi] Scoped raw-TX validator: pinned SDK, build-local archive")


if "Import" in globals():
    Import("env")  # noqa: F821 -- PlatformIO/SCons
    prepare(env)  # noqa: F821
