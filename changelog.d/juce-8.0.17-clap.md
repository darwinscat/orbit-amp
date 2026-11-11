Amps: OrbitAmp, CatpitAmp, RedPawAmp, FrostPawAmp, AmberPawAmp, OrbitBass, RedGrooveBass

### Fixed
- **Safer with the files it opens.** Built on JUCE 8.0.17, its security release: a WAV with a broken header no longer
  crashes the amp, a zip can no longer write outside its folder, and a preset or a session nested too deep is refused
  rather than run.
- **CLAP hosts.** A latency that changes while the plug-in starts is reported to the host, a project's transport is
  read right across split blocks, and the plug-in's text reaches the host whole.
