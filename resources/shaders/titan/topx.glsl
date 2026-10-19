vec2 toPx(vec3 v){ vec2 ndc = vec2(v.x / (v.z * uTan.x), v.y / (v.z * uTan.y) + uShear); return (ndc * 0.5 + 0.5) * uRes; }
