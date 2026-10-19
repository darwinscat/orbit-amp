
uint ih(uint x){ x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float h01(uint a, uint s){ return float(ih(a * 0x9E3779B1u ^ ih(s * 0x85EBCA77u + 0x632BE5ABu))) * (1.0 / 4294967296.0); }
float hcell(ivec2 c, uint s){ return float(ih(uint(c.x) * 0x9E3779B1u ^ ih(uint(c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float sq(float x){ return x * x; }
