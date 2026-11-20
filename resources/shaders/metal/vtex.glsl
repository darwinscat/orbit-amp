// metal-gl.js's VS_TEX: the vertex shaders read the base and the road map at level 0.
#define BTEX(uv) textureLod(uBase, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
