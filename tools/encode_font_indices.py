"""Preserve UTF-8 glyph keys as ASCII C escapes for ARM Compiler 5."""
from pathlib import Path
import re

path = Path(__file__).resolve().parents[1] / "Drivers/OLED/OLED_Data_Hanzi.c"
source = path.read_text(encoding="utf-8")


def escaped(match):
    value = match.group(1)
    if value.isascii():
        return match.group(0)
    return '"' + ''.join(f'\\x{byte:02X}' for byte in value.encode("utf-8")) + '"'


result = re.sub(r'"([^"\\]*)"', escaped, source)
if result != source:
    path.write_text(result, encoding="utf-8", newline="\n")
print("OLED glyph indices use ASCII escapes; UTF-8 key bytes preserved.")
