"""Package portable sources plus the current verified Keil artifacts."""
from pathlib import Path
import hashlib
import json
from shutil import copy2
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parents[1]
project = root / "Project/eXin.uvprojx"
axf = root / "build/keil/eXin.axf"
hex_path = root / "build/keil/eXin.hex"
# Keil emits its current map in ListingPath, independently of OutputDirectory.
map_path = root / "build/keil/eXin.map"
listing_map = root / "Project/Listings/eXin.map"
if listing_map.is_file() and (not map_path.is_file() or listing_map.stat().st_mtime > map_path.stat().st_mtime):
    copy2(listing_map, map_path)
tree = ET.parse(project)
registered = set()
for node in tree.findall(".//Groups/Group/Files/File"):
    filename = node.findtext("FilePath")
    source = (project.parent / filename.replace("\\", "/")).resolve()
    if not source.is_file():
        raise RuntimeError(f"Missing registered source: {filename}")
    if source.suffix.lower() in (".c", ".h", ".s"):
        registered.add(source)
        if source.stat().st_mtime > axf.stat().st_mtime:
            raise RuntimeError(f"Firmware older than source: {filename}")
expected = set()
for folder in ("Drivers", "Application", "Protocol"):
    expected.update((root / folder).rglob("*.c"))
if expected - registered:
    raise RuntimeError(f"Sources missing from Keil: {expected - registered}")
paths = [root / "README.md"]
allowed = {".c", ".h", ".s", ".md", ".ld", ".sct", ".uvprojx", ".ps1", ".py", ".txt", ".png"}
for folder in ("Application", "Drivers", "Protocol", "Project", "tools", "tests", "docs", "Vendor/LocalSPL"):
    for path in (root / folder).rglob("*"):
        if path.is_file() and path.suffix.lower() in allowed and "__pycache__" not in path.parts:
            paths.append(path)
mirror = root / "Vendor/STM32F10x_StdPeriph_Lib"
paths.extend((
    mirror / "README.md",
    mirror / "Libraries/CMSIS/License.doc",
    mirror / "Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x/startup/gcc_ride7/startup_stm32f10x_md.s",
    hex_path, axf, root / "build/keil/eXin.map", root / "Project/build_autodebug.log",
))
release = root / "release"
release.mkdir(exist_ok=True)
archive = release / "eXin_STM32F103C8_SPL.zip"
manifest = {}
with ZipFile(archive, "w", ZIP_DEFLATED) as out:
    for path in sorted(set(paths)):
        relative = path.relative_to(root).as_posix()
        data = path.read_bytes()
        manifest[relative] = hashlib.sha256(data).hexdigest()
        out.writestr("eXinFirmware/" + relative, data)
    out.writestr("eXinFirmware/SHA256SUMS.json", json.dumps(manifest, indent=2, ensure_ascii=False))
with ZipFile(archive) as check:
    if check.testzip() is not None:
        raise RuntimeError("Archive CRC validation failed")
    for relative, digest in manifest.items():
        assert hashlib.sha256(check.read("eXinFirmware/" + relative)).hexdigest() == digest
assert axf.is_file() and hex_path.is_file()
print(f"Packaged {len(manifest)} files, CRC/SHA256 verified: {archive}")
print(f"ZIP size: {archive.stat().st_size} bytes")
print(f"HEX SHA256: {manifest['build/keil/eXin.hex']}")
