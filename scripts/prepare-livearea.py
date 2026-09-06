"""Validate the supplied LiveArea artwork and encode installable Vita PNGs."""
from pathlib import Path
from io import BytesIO
import shutil
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'livearea/sce_sys'
DEST = ROOT / 'vita/direct/extras/livearea'
ASSETS = {
    'icon0.png': ((128, 128), 'icon0.png'),
    'pic0.png': ((960, 544), 'pic0.png'),
    'livearea/contents/bg0.png': ((840, 500), 'bg0.png'),
    'livearea/contents/startup.png': ((280, 158), 'startup.png'),
}

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    for relative, (size, name) in ASSETS.items():
        with Image.open(SOURCE / relative) as im:
            im.load()
            if im.size != size:
                raise ValueError(f'{relative}: expected {size}, found {im.size}')
            # Re-encode without editor metadata. Preserve existing palettes;
            # the supplied RGB bubble needs an 8-bit indexed palette on Vita.
            if im.mode == 'P':
                encoded = im.copy()
                transparency = im.info.get('transparency')
            else:
                encoded = im.convert('RGBA').quantize(colors=256, method=Image.Quantize.FASTOCTREE)
                transparency = encoded.info.get('transparency')
            encoded.info.clear()
            output = BytesIO()
            kwargs = {'transparency': transparency} if transparency is not None else {}
            encoded.save(output, format='PNG', optimize=True, bits=8, **kwargs)
            data = output.getvalue()
            assert data[24:29] == bytes([8, 3, 0, 0, 0]), 'Expected indexed, noninterlaced PNG'
            target = DEST / name
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
            print(f'{name}: {size[0]}x{size[1]}, indexed PNG, {len(data)} bytes')
    template = SOURCE / 'livearea/contents/template.xml'
    tree = ET.parse(template)
    assert tree.getroot().tag == 'livearea'
    assert tree.findtext('livearea-background/image') == 'bg0.png'
    assert tree.findtext('gate/startup-image') == 'startup.png'
    if not (DEST/'template.xml').exists() or (DEST/'template.xml').read_bytes() != template.read_bytes():
        shutil.copyfile(template, DEST/'template.xml')

if __name__ == '__main__':
    main()
