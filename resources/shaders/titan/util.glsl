vec2 rot(vec2 p, float c, float s){ return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }
vec2 rotT(vec2 p, float c, float s){ return vec2(c * p.x + s * p.y, -s * p.x + c * p.y); }
vec3 softclip(vec3 x){ return mix(x, 0.8 + 0.2 * (1.0 - exp(-(x - 0.8) / 0.2)), step(0.8, x)); }
vec3 toDisplay(vec3 c){ return pow(max(softclip(c), 0.0), vec3(1.0 / 2.2)); }
