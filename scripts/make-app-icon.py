"""Rebuild the Windows ICO from the approved transparent ShiftWheel logo."""
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root / 'resources/branding/shiftwheel-logo-v1.png'
destination = root / 'resources/branding/shiftwheel.ico'
sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
with Image.open(source) as logo:
    image = logo.convert('RGBA')
    if image.width != image.height:
        raise ValueError('The approved logo must be square.')
    if image.getchannel('A').getextrema()[0] != 0:
        raise ValueError('The approved logo must have a transparent background.')
    image.save(destination, format='ICO', sizes=[(size, size) for size in sizes])
print('Created', destination.relative_to(root), 'with sizes', ', '.join(map(str, sizes)))
