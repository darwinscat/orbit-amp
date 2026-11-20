The Io world's finds (src/io/view/IoArt.h): the hardware left behind on Io, our own models, as the world's prototype
(~/CLAUDE_FILES/Worlds/io/finds) has them, the same bytes. Laid out by the installer in the factory folder's
Faces/IoTideBass/finds beside the cabin's two plates (resources/io/README.txt) and read from there at run time (IO_ART_DIR),
never built into a binary.

  mast3d/ dish3d/ module3d/ lander3d/ station3d/ rover3d/     the six single pieces
  base3d/ tubebase3d/ observatory3d/ plant3d/                  the four crewed stations

Each folder <variant>3d holds <name>.mesh (CBMESH1: 40-byte vertices, uint32 indices, z up, metres) and <name>.txt, its
table as it is: what it is, its credit line ("none: our own model", so none is shown), and for the lava-tube base the
skylight's pit radius ("pit radius 13.0"), which the world's pit follows. A mesh takes its variant's place everywhere;
a folder that is missing leaves the variant's stand-in drawn in code (IoWorld::structureMeshes). No textures.

Sizes: the ten meshes 4.5 MB together, as they are.
