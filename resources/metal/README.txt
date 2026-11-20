The metal cabin's art (FerroClawAmp, the ENGL E530's face), as the factory folder carries it (CMakeLists.txt,
FERROCLAWAMP_RENDERS): laid out in Faces/FerroClawAmp and read at run time (src/metal/MetalFaceArt.h).

The plates are catbay's renders (the metal world's cabin, variant c6, the owner's pick of 20.11; catbay's
feat/rover-cabin 9235e72, blender/cabins/metal_cabin.py --final), kept there, not here (22 MB):
dashboard_night.png and dashboard_flash.png, 3344 x 1882 RGBA, the cabin over the 1672 x 941 stage at 2x, its
window the alpha (glass reflections at a few per cent). DARWIN'S CAT and POWERED BY are baked into both. The two
WebP files here are made from them (the night plate under the name every face's plate has) with cwebp 1.6.0, run in this folder on copies of them in source/, as resources/underwater/dashboard.webp is:

  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard_night.png -o dashboard.webp
  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard_flash.png -o dashboard_flash.webp

dashboard.webp        the plate the face paints whenever the world does not draw the cabin itself (until the metal
                      world's port comes: always). Lossy colour, the alpha lossless and exact.
dashboard_flash.webp  the same cabin lit by the lightning, for the world's renderer, which crossfades the two by the
                      flash (catbay's metal-gl.js, cabinFlash). The face never reads it.

sockets.txt and labels.txt are catbay's tables as handed over (final/sockets_dashboard.txt, labels_dashboard.txt):
every control's centre and size, and the bands the controls' names are written in, in stage px. The face's
layout, src/metal/MetalLayout.h, is read from them.

face/ holds what is the face's own beside the plates: its MIDI list. The knobs, the faders' cap and the mark are
the helicopter's (resources/amberpaw) until the cabin has its own; one dropped into face/ under the same name is
taken instead on the next configure.
