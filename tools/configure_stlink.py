"""Adapt the user's existing Keil ST-Link options to this project."""
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
reference = Path(r"E:\STM32project\6-2 硬件收发IIC\project.uvoptx")
destination = root / "Project/eXin.uvoptx"
tree = ET.parse(reference)
options = tree.getroot()
for node in list(options):
    if node.tag not in ("SchemaVersion", "Header", "Extensions", "DaveTm", "Target"):
        options.remove(node)
target = options.find("Target")
target.find("TargetName").text = "eXin STM32F103C8"
settings = target.find("TargetOption")
settings.find("CLKADS").text = "72000000"
for tag in ("Breakpoint", "WatchWindow1", "WatchWindow2", "MemoryWindow1", "MemoryWindow2"):
    node = settings.find(tag)
    if node is not None:
        settings.remove(node)
settings.find("DebugOpt/pMon").text = r"STLink\ST-LINKIII-KEIL_SWO.dll"
settings.find("DebugOpt/uSim").text = "0"
settings.find("DebugOpt/uTrg").text = "1"
settings.find("DebugOpt/tGomain").text = "0"
entry = settings.find("TargetDriverDllRegistry/SetRegEntry/Name")
assert "37FF71064E5734361C8A1143" in entry.text
if destination.exists():
    destination.with_suffix(".uvoptx.before_stlink.bak").write_bytes(destination.read_bytes())
ET.indent(tree, space="  ")
tree.write(destination, encoding="utf-8", xml_declaration=True)
print("Configured Keil ST-Link SWD for detected probe; sector erase/program/verify/reset options preserved.")
