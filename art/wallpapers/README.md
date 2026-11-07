# Generated wallpapers

Every Setu wallpaper here is an algorithm, a palette and a seed
(`wallgen.py`). Nothing is traced or copied; the same inputs always give
the same pixels, and any size can be rendered sharp.

    python3 wallgen.py list
    python3 wallgen.py render flow -p ember -s 3 --size 3840x2160 -o flow-ember.png
    python3 wallgen.py sheet --styles flow,ridges -p setu-blue -p dawn -o sheet.png

| Style | Look |
|---|---|
| aurora | curtains of light over a dark horizon |
| flow | thousands of fine lines following an invisible current |
| lowpoly | faceted triangles catching light from one side |
| contours | topographic map lines over shaded terrain |
| ridges | layered hills fading into a hazy sky, with a low sun |
| glass | stained-glass cells with soft inner light |
| orbits | concentric arcs and a few bright planets |
| tiles | a grid of quarter circles and half moons, Bauhaus style |

Palettes: setu-blue, setu-dark, dawn, forest, slate, ember. A palette is two
background colours (dark, light) and five accents (main colours, then a
highlight, then a near-white). Theme packs (docs/THEMES.md) will pick a
style, palette and seed, so a pack can carry a wallpaper without shipping a
large image.

Needs python3-numpy, python3-pil and python3-cairo.
