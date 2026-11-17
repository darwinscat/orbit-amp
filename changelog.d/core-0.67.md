Amps: OrbitAmp, CatpitAmp, RedPawAmp, FrostPawAmp, AmberPawAmp

### Dependencies
- felitronics-core v0.67.0 (from v0.62.0). The cabinet's convolver keeps only the memory its IR needs instead of
  its whole 5 s, and an IR longer than 1.41 s at 48 kHz (0.70 s at 96 kHz) is convolved in smaller steps, which
  plays within float noise of before. The echo plays exactly as before; with REPEATS at 0 the plug-in no longer
  tells the host about echo tail that is not there.
