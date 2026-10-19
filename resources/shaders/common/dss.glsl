float dss (float e0, float e1, float x) { float t = clamp ((x - e0) / (e1 - e0), 0.0, 1.0); return 6.0 * t * (1.0 - t) / (e1 - e0); }
