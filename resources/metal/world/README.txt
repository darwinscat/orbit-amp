The metal world's finds (the ENGL E530's view, src/metal/view), as the factory folder carries them beside the cabin's
plates (Faces/FerroClawAmp/world) and the world reads them at run time (src/metal/view/MetalArt.h, METAL_ART_DIR); never
built into the binary. Taken from the view's prototype (catbay's Worlds/metal/finds and Worlds/rogue/finds/dragon3d),
only what the world reads; the previews and the other 27 guitars of guitars3d/ are left out.

<name>3d/<name>.mesh   the owner's meshes, CBMESH1 as they are: the skull gate, the fossil, the warrior, the cat, the
                       horns, the mammoth, the Brachyceratops, the lava crawler; each with its .txt beside it, its
                       credit line (CC BY: the credits travel with the meshes).
guitars3d/             guitars.txt (the index of all 35, as it is: the groves deal from it by name) and the eight the
                       groves use, each with its .txt: the four stars (strat_candy_apple_red, pbass_lake_placid_blue,
                       rg_flame, btb5) and our four designs (ears_blackchrome, claw_gunmetal, fang_bloodred,
                       bolt_brushed).
dragon3d/              the rogue planet's dragon: dragon.mesh, its wing-beat dragon.anim (CBANIM1), dragon.txt.
letters/               letters.txt as it is (it names each text's PNG; the world reads the WebP of the same name) and the
                       eight texts, lossless:  cwebp -lossless -z 9 -exact <name>.png -o <name>.webp
banners/               banners.txt as it is (cells in px of the atlas) and atlas.webp, the colour lossy and the alpha
                       lossless and exact, as the plates:  cwebp -q 90 -alpha_q 100 -exact -sharp_yuv -m 6 atlas.png -o atlas.webp

The plates beside world/ (dashboard.webp, dashboard_flash.webp) are the face's, made from catbay's final plates by the
cwebp commands the face's README gives (cwebp 1.6.0, byte for byte the same files).
