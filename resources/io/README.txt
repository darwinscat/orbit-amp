The Io rover's cabin art (IoBass, the SansAmp RBI's face), as the factory folder carries it (CMakeLists.txt,
IOBASS_RENDERS): laid out in Faces/IoBass and read at run time (src/io/IoFaceArt.h).

The plates are catbay's renders (~/CLAUDE_FILES/Worlds/io/cabin/final, variant b), kept there, not here (18 MB):
dashboard_day.png and dashboard_dark.png, 3344 x 1882 RGBA, the cabin over the 1672 x 941 stage at 2x, its windscreen
the alpha. The two WebP files here are made from them (the day's plate under the name every face's plate has) with
cwebp 1.6.0, run in this folder on copies of them in source/, as resources/underwater/dashboard.webp is:

  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard_day.png -o dashboard.webp
  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard_dark.png -o dashboard_dark.webp

dashboard.webp       the day's plate, which the face paints whenever the world does not draw the cabin itself (the 1993
                     landscape, the still, a picture taken). Lossy colour, the alpha lossless and exact.
dashboard_dark.webp  the same cabin in the dark, for the world's renderer, which crossfades the two by the Sun
                     (catbay's cabinDay). The face never reads it; it only follows how dark the world says the cabin is
                     (IoDrive::cabinDark, taken every frame: IoEditor::followWorld) with its labels' glow.

sockets.txt is catbay's table as handed over (final/sockets_dashboard.txt): every control's centre and size, in stage
px. The face's layout, src/io/IoLayout.h, is read from it; sock_dosimeter is the world's (it draws the needle). The
pictograms and the words stand where the approved mockup has them (cabin/drafts/face/io.html, glyphs_io.html), but
the tone knobs' three, raised 14 px with the knobs on the re-rendered plates. The table's comment line tone_strip, a
text strip under the tone knobs with no socket, is where MEM and CPU are written.

fonts/B612-Regular-Latin.ttf and fonts/B612-Bold-Latin.ttf are a European Latin subset of B612 (The B612 Project
Authors, github.com/polarsys/b612 at 48ac6ba, SIL Open Font License 1.1, fonts/B612-OFL.txt; no Reserved Font Name,
so the subset keeps its family name), cut from fonts/ttf/B612-Regular.ttf and B612-Bold.ttf with fontTools 4.68:

  pyftsubset B612-Regular.ttf --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+2010-2027,U+2039-203A,U+20AC" --layout-features='*' --name-IDs='*' --notdef-outline --no-hinting --drop-tables+=DSIG --output-file=B612-Regular-Latin.ttf

(and the same for the Bold). They are embedded in the binary (IoData) for the few words on the panel: GATE, LIMITER,
the switches' sides, the lever's P and the keys' A to D.

face/ holds what the face has of its own: its MIDI list. The knobs and the faders' cap are the helicopter's
(resources/amberpaw) until the cabin has its own; one dropped into face/ under the same name is taken instead on the next
configure. It has no mark of its own: the maker's plate wears the family's cat.
