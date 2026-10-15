"""Render real application framebuffers exported by app_integration_test."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
names = ("menu", "time", "alarm", "imu", "servo", "servo_uart")
sheet = Image.new("RGB", (1200, 500), "#17212c")
draw = ImageDraw.Draw(sheet)
for index, name in enumerate(names):
    raw = Image.open(root / f"build/tests/ui_{name}.pbm").convert("L")
    panel = Image.new("RGB", raw.size, "#020709")
    for y in range(64):
        for x in range(128):
            if raw.getpixel((x, y)) == 0:
                panel.putpixel((x, y), (130, 235, 255))
    panel = panel.resize((384, 192), Image.Resampling.NEAREST)
    x, y = 8 + (index % 3) * 400, 12 + (index // 3) * 250
    draw.text((x, y), name.upper(), fill="#eef6ff")
    sheet.paste(panel, (x, y + 25))
(root / "docs").mkdir(exist_ok=True)
sheet.save(root / "docs/ui-preview.png")
print("Saved docs/ui-preview.png (software framebuffer preview).")
