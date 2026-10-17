# Rasterize the logo SVG with headless Edge, then build PNG + multi-size ICO with Pillow.
# Usage (from the repo root):
#   python docs/logo/make_icon.py docs/logo/surume.svg docs/logo/surume.png src/surume.ico <temp_dir>
import os
import subprocess
import sys

from PIL import Image

RENDER_SIZE = 512  # render large, downsample for quality
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
EDGE = os.path.join(os.environ["ProgramFiles(x86)"], "Microsoft", "Edge", "Application", "msedge.exe")


def render(svg_path, work_dir):
    html_path = os.path.join(work_dir, "render.html")
    shot_path = os.path.join(work_dir, "render.png")
    svg_url = "file:///" + os.path.abspath(svg_path).replace("\\", "/")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write('<!doctype html><html><body style="margin:0;background:transparent">'
                '<img src="%s" width="%d" height="%d" style="display:block"></body></html>'
                % (svg_url, RENDER_SIZE, RENDER_SIZE))
    if os.path.exists(shot_path):
        os.remove(shot_path)
    subprocess.run([EDGE, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--allow-file-access-from-files", "--default-background-color=00000000",
                    "--window-size=%d,%d" % (RENDER_SIZE, RENDER_SIZE),
                    "--screenshot=" + shot_path, "file:///" + html_path.replace("\\", "/")],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return Image.open(shot_path).convert("RGBA")


def main():
    svg_path, png_path, ico_path, work_dir = sys.argv[1:5]
    big = render(svg_path, work_dir)
    assert big.size == (RENDER_SIZE, RENDER_SIZE), big.size
    assert big.getpixel((0, 0))[3] == 0, "background is not transparent"

    big.resize((256, 256), Image.LANCZOS).save(png_path)
    frames = [big.resize((s, s), Image.LANCZOS) for s in ICO_SIZES]
    frames[-1].save(ico_path, format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    print("wrote", png_path, ico_path)


main()
