// io-gl.js's VS_TEX: the vertex shaders read the base, the road map and the feature map at level 0.
#define BTEX(uv) textureLod(uBase, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define FTEX(uv) textureLod(uFeat, uv, 0.0)
