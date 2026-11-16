The underwater cabin's art, as the binary carries it (CMakeLists.txt, UnderwaterArtData).

source/ holds the renders as they came out of Blender (catbay, feat/sub-cabin): dashboard.png and sea.png
(3344 x 1882 RGBA, the cabin over the 1672 x 941 stage at 2x) and atlas.png (6144 x 1728 RGBA, 32 cells of
768 x 432, 8 x 4, 99.89 px per metre). The three WebP files here are made from them with cwebp 1.6.0, run in
this folder:

  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/dashboard.png -o dashboard.webp
  cwebp -q 80 -sharp_yuv -m 6 -noalpha -resize 1672 941 source/sea.png -o sea.webp
  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 source/atlas.png -o atlas.webp

dashboard.webp  3344 x 1882. Lossy colour, the alpha lossless: every alpha value as rendered (the window's
                zero, the edges, so the clicks' window mask is the same), and -exact keeps the colour under
                the zero alpha that the GPU's linear filter reaches at the window's edge.
sea.webp        1672 x 941, no alpha: half the dashboard's size, which the GPU scales up (linear) over the
                stage. The panel reads only its rgb, as (k - 1) * sea with |k - 1| <= 0.42, so its error
                reaches the screen at most at 0.42 of itself.
atlas.webp      6144 x 1728, the cells as rendered. Lossy colour, the alpha lossless. sprites.txt is the
                atlas's table as rendered (catbay's passby/sprites.txt), in the render's pixels: the code
                finds a cell by the grid alone and never reads the texture's size, so the table stays as it
                is even for a resized atlas.

Measured against the PNGs (decoded with dwebp, as libwebp decodes them in the plug-in):
  dashboard  opaque panel 45.3 dB PSNR, 99.9 % of pixels within 7/255 (the largest misses, up to 74, are
             the rims of the three small amber lamps: 4:2:0 chroma); alpha bit-exact.
  sea        40 dB where the panel shows it, scaled back up as the GPU does; on screen at most 6.6/255 in
             clear water, 15/255 at full silt, at single pixels along the panel's rims.
  atlas      44.5 dB on the premultiplied texels, 99.9 % within 9/255; alpha bit-exact.
