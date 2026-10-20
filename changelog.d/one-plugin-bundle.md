Amps: all

### Changed
- **One plug-in for the whole family, in each format.** Every amp is now in ONE bundle per format,
  `Darwin's Cat Amps.vst3`, `Darwin's Cat Amps.component` (Mac) and `Darwin's Cat Amps.clap`, in place of a
  bundle per amp. Every amp keeps its plug-in ids, so a session finds the same amps it used. The standalone
  apps stay one per amp.
- **The installers tick a plug-in format once**: VST3, Audio Unit and CLAP, each with every amp in it, then
  every amp's standalone app; the amps' data stays ticked and greyed out.
- **Installing removes the per-amp plug-ins of earlier releases** — `OrbitAmp.vst3`, `CatpitAmp.vst3`,
  `RedPawAmp.vst3` and their `.component` and `.clap` — from the folders the earlier installers placed them
  in and from your own plug-in folders (`~/Library/Audio/Plug-Ins` on the Mac,
  `%LOCALAPPDATA%\Programs\Common` on Windows, `~/.vst3` and `~/.clap` on Linux), so a host does not list
  every amp twice. Nothing else in those folders is touched, and a format you untick keeps its old bundles.
- **The zips** carry the same three bundles and the apps, and their README says where each goes. If you
  copied the per-amp plug-ins of an earlier zip by hand, delete them.
