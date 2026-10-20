Amps: all

### Fixed
- **Linux: two of the family's plug-ins in one host no longer stop it.** A host that loaded both the VST3
  and the CLAP of the amps, or an amp's plug-in of an earlier release beside the family's, or two amps'
  plug-ins of 0.8.0, was stopped by the second one to load. Each plug-in now keeps everything inside it to
  itself, and any number of them play side by side in one host, as they always did on the Mac and Windows.
