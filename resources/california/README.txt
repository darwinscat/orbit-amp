The van's cabin art, as the factory folder carries it (CMakeLists.txt, CALIFORNIAAMP_RENDERS).

The renders are catbay's (~/CLAUDE_FILES/van-cabin/final, variant C): dashboard.png (3344 x 1882 RGBA, the cabin
over the 1672 x 941 stage at 2x, the windscreen transparent; 9.8 MB, kept there, not here: copy it into source/ to
remake the WebP) and pendant.png (187 x 528 RGBA, the peace pendant's sprite), which source/ holds. sockets.txt is the cabin's socket table and pendant.txt the sprite's box on the stage
and the pivot it swings about, both as catbay handed them. The two WebP files here are made from them with
cwebp 1.6.0, run in this folder:

  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard.png -o dashboard.webp
  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/pendant.png -o pendant.webp

Lossy colour, the alpha lossless and every alpha value as rendered, as resources/underwater/dashboard.webp is.

fonts/Shrikhand-Latin.ttf is a European Latin subset of Shrikhand (Jonny Pinhorn, SIL Open Font License 1.1,
fonts/Shrikhand-OFL.txt; no Reserved Font Name), cut from catbay's assets/fonts/Shrikhand-Regular.ttf with
fontTools 4.68:

  pyftsubset Shrikhand-Regular.ttf --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+0218-021B,U+2013-2014,U+2018-201E,U+2022,U+2026,U+2039-203A,U+20AC" --layout-features='*' --name-IDs='*' --notdef-outline --output-file=Shrikhand-Latin.ttf

It is embedded in the binary (CaliforniaData) for the letters on the keys, A to D, and the lever's P.

face/ holds what the face has of its own; the knobs and the faders' cap are AmberPawAmp's renders until it has
its own (CMakeLists.txt).
