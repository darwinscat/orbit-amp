The California world's art and files, as the installer lays them out (Faces/PeacePhaseAmp in the factory folder) and
src/california/view/CaliforniaArt.cpp reads them at run time (CALIFORNIA_ART_DIR, src/face/Renders.h). Never built
into a binary. The world's prototype (california-gl.js) names its pictures .png; each is read from the .webp of the
same name here.

From the van cabin's renders (catbay, van-cabin/final, cabin C — the prototype's cabin-van/dashboard_c.png and
pendant_c.png, the same bytes):

  dashboard.webp           3344 x 1882, the cabin over the 1672 x 941 stage at 2x, the windshield alpha 0. As the
                           underwater dashboard: lossy colour, the alpha lossless and exact (the window's zero and its
                           edges as rendered: the windshield's box, the glass's lower edge and the clicks' window mask
                           come from it), -exact keeping the colour under the zero alpha.
                             cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 dashboard.png -o dashboard.webp
  dashboard_pendant.webp   187 x 528, the peace pendant; dashboard_pendant.txt its box on the stage and its pivot.
                             cwebp -lossless -exact -z 9 dashboard_pendant.png -o dashboard_pendant.webp
  label.webp               512 x 112, the CENSORED word exactly as the prototype draws it on its canvas (bold 84 px
                           Helvetica, a 9 px dark outline, in headless Chrome on this Mac), straight alpha, lossless.

finds/, from the prototype's finds/ (the tables, the meshes as they are):

  dish/, tower/            atlas.webp (3072 x 768, the sprites' headings) + sprites.txt
  signs/                   atlas.webp (2048 x 1024) + signs.txt
  flags/                   atlas.webp (576 x 256) + flags.txt
  chalk/                   atlas.webp (2048 x 1024) + chalk.txt
                           The atlases carry alpha edges (the sprites' and the signs' shapes, the cloth, the chalk's
                           strokes): lossless.  cwebp -lossless -exact -z 9 atlas.png -o atlas.webp
  van3d/ saucer3d/ yellowsub3d/ barn3d/ barn_smiley3d/ barn_salute3d/ barn_victory3d/ drivein3d/ combos3d/
  walker3d/ guitars3d/ hippieguitars3d/ (heron, kestrel, lander, lantern)
                           <name>.mesh (CBMESH1) and <name>.txt as they are; the textures the .txt names as WebP,
                           each the smaller of lossless (cwebp -lossless -exact -z 9) and lossy with the alpha lossless
                           (cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6): lossy for the three barns' murals,
                           lossless for the rest.

Sizes, PNG -> WebP: dashboard 9.77 MB -> 1.19 MB; pendant 64 KB -> 38 KB; dish 1.30 MB -> 751 KB; tower 1.28 MB ->
879 KB; signs 1.56 MB -> 957 KB; flags 190 KB -> 144 KB; chalk 453 KB -> 241 KB; the meshes' eleven textures
1.04 MB -> 266 KB. The meshes 7.0 MB as they are. Everything here: 11 MB.

THE FACE'S OWN (PeacePhaseAmp, src/california): sockets.txt is the cabin's socket table as catbay handed it; face/
holds what the face has of its own (its MIDI list); the knobs and the faders' cap are AmberPawAmp's renders until it
has its own (CMakeLists.txt). The face reads the world's dashboard.webp and dashboard_pendant.webp (one pendant, its box
in dashboard_pendant.txt) and paints them, the pendant hanging still, whenever the world does not draw the cabin itself.
The build lays out all of it but sockets.txt, fonts/ and this file in the factory folder's Faces/PeacePhaseAmp, finds/
as a folder with every file at the same path (add_amp's ART_FOLDERS).

fonts/Shrikhand-Latin.ttf is a European Latin subset of Shrikhand (Jonny Pinhorn, SIL Open Font License 1.1,
fonts/Shrikhand-OFL.txt; no Reserved Font Name), cut from catbay's assets/fonts/Shrikhand-Regular.ttf with
fontTools 4.68:

  pyftsubset Shrikhand-Regular.ttf --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+0218-021B,U+2013-2014,U+2018-201E,U+2022,U+2026,U+2039-203A,U+20AC" --layout-features='*' --name-IDs='*' --notdef-outline --output-file=Shrikhand-Latin.ttf

It is embedded in the binary (CaliforniaData) for the letters on the keys, A to D, and the lever's P.
