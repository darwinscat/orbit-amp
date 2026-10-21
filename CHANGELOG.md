<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Darwin's Cat Amps** — the amps released together from this repository, under
one version — are documented here: each release with a part for every amp at once and one for each amp.
Up to 0.7.0 this book was OrbitAmp's alone. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.9.0] — 2026-10-21 — one plug-in per format, the packs at their measured levels, every amp on RED GIANT

### Every amp

#### Changed
- **The amps' data is installed once, in one folder they share.** The packs, the cabinets and the faces'
  pictures were built into every amp, in every format, for both Mac architectures — the same 26 MB many times
  over. Now every installer lays them out once, as a part of it that cannot be unticked: in
  `/Library/Application Support/Darwin's Cat/Factory` on a Mac, `C:\ProgramData\Darwin's Cat\Factory` on
  Windows, and `~/.local/share/darwinscat-amps/Factory` on Linux (`/usr/local/share/…` with `--system`).
  Every amp plays what is its own in it, and the download is a third of what it was. The Windows zips carry the
  same folder, with a README that says where to copy it.
- An amp that cannot find that folder says so, and where it belongs, instead of opening an empty face.
- **A capture's sound fades in once both captures the gain dial blends are ready** — at the amp's first sound and at
  every pack chosen. It used to start rising while one of the two was still on its way, and that one let the dry
  guitar through; now the four blocks it took (some 40 ms at 48 kHz in blocks of 512) are silent and the fade starts
  after them. From there on the sound is the same, sample for sample.
- **HUM is off for now.** The input's hum remover is not ready to be played yet, so every amp ships without
  it: no HUM in the input column's or the settings menu, no LEARN HUM key, no H key, and the hum stage never
  runs — a session saved with HUM on plays without it. It comes back in a later release, and a choice you
  made for it then is still there.
- **The input is unity again, in every amp.** OrbitAmp's IN fader and the INPUT lever of the cockpit and
  the rovers are 0 dB at their middle, and there is no machine setting under them any more: the lever's
  zero is gone, from every menu. How hard a capture is fed is its pack's own to say, measured where its
  captures were taken: -4 dB for the X88-IR's four channels and the IR-X's two.
- A cockpit's or a rover's session or preset from 0.8.0 keeps its INPUT lever where it stood over 0.8.0's
  zero, and its gate opens on the same guitar; fed at the pack's measured level, it plays 5 dB hotter than
  it did in 0.8.0 (one made under a zero of -12 dB, 8 dB hotter).
- A pack of your own that states no level is fed at unity.
- OrbitAmp feeds the packs of the set 4 dB softer than it did; its sessions keep their IN fader and their
  gate.
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
- **The Windows zips** carry the same two bundles and the apps, and their README says where each goes. If you
  copied the per-amp plug-ins of an earlier zip by hand, delete them.
- **Every amp opens on RED GIANT.** A fresh instance, every channel and INIT start on the Orange PPC212V's
  RED GIANT, where they started on DARK MATTER. A session or preset saved before this release on DARK MATTER
  opens on RED GIANT too, every channel of it; one on any other cabinet — another of the shelf's, an IR of
  your own, one the session carries — keeps it. DARK MATTER is still on the shelf, and picked from now on it
  stays picked.
- **Smaller downloads.** Every amp is built with link-time optimisation and without its internal symbol
  names, and without the Ogg Vorbis, FLAC and Windows Media readers no amp ever used; the Mac package and the
  Windows installer are packed tighter.
- **The Mac download is the `.pkg` alone**, universal — for Apple silicon and Intel alike; no Mac zip any more.
- **The Linux download is a `.tar.xz`**, `darwinscat-amps-<version>-linux-<arch>.tar.xz`: unpack it with
  `tar -xf`, as before.

#### Removed
- **OrbitAmp's OLD**, the Victory V4's six packs, is no longer shipped.

#### Fixed
- **A host is told the defaults an amp really opens on, and an amp it has just opened keeps its knobs still.**
  Each parameter's default, as a host reads it, is now where a new instance stands: CatpitAmp's and the
  rovers' room, room mix, delay and amp the ones their first channel opens on, and STEREO MODE the one the
  amp picks for where it runs — STEREO on a stereo track, MONO on a mono one, STEREO SPACE in the
  standalone. The mode is picked as soon as the host settles the track's channels, no longer when playback
  starts, so a host no longer sees the amp move a parameter by itself as it starts playing. How an amp
  opens and what it plays are as before, and so is everything the amp puts back itself — INIT, Reset to
  default, a double-click on a control; only a host's own "reset to default" goes where the amp opens.
- **A session opened in a CLAP host shows its knobs where the session left them, at once.** Loading a
  session or a preset into an amp's CLAP now tells the host every parameter's value has changed, as CLAP
  asks; before, a host could go on showing the positions of before until the amp next played, and might
  take the new ones for automation.
- **Linux: two of the family's plug-ins in one host no longer stop it.** A host that loaded both the VST3
  and the CLAP of the amps, or an amp's plug-in of an earlier release beside the family's, or two amps'
  plug-ins of 0.8.0, was stopped by the second one to load. Each plug-in now keeps everything inside it to
  itself, and any number of them play side by side in one host, as they always did on the Mac and Windows.
- **The badge and the version window open the amp's own page again.** They pointed at addresses the site
  no longer has (three of them showed "page not found"); each now opens its amp's page on darwinscat.com.
  The tip jar's link is unchanged.

#### Dependencies
- **felitronics-guitar-core v0.1.1**: Eigen, which the capture player computes with, comes from Darwin's Cat's own
  mirror on GitHub at the very commit it was before (Eigen 5.0.1), not from gitlab.com — a build no longer fails
  when GitLab turns requests away. The sound is the same, byte for byte.
- **felitronics-guitar-core v0.7.0, with namz v4.4.0**: a capture loads in a fifteenth of the time on a mono input
  and an eighth on a stereo one — it is built straight from its `.namz` instead of through a text copy of its weights,
  a mono input gets one network instead of two, and the silence it was warmed on before is gone, since the playing
  warms it before it is heard. A turn of the gain dial waits less for the capture it lands on, and a capture held for
  a mono input takes half the memory.

### OrbitAmp

#### Changed
- **The AMP menu names each device with its maker** — the Soldano X88-IR and the Friedman IR-X.
- **The OLD group is gone**, and the old V4 captures in it with it: the V4 comes back with its new captures
  and an amp of its own.
- **DRIVE keeps its size.** The switches a device brings stand in a column of their own right of the dial,
  not under it, so DRIVE no longer shrinks when a device has some. Its travel runs from 7 to 5 o'clock, so 2
  points straight left and 8 straight right.

### CatpitAmp

#### Added
- **When the view out of the window cannot start on your graphics card, the amp now writes down why.** The
  windscreen falls back to the 1993 landscape and the cockpit's sky to its stars on their own, as before;
  now each time it happens a line goes into `gpu.log` in Darwin's Cat's folder — on the Mac
  `~/Library/Application Support/Darwin's Cat/gpu.log`, on Windows `%APPDATA%\Darwin's Cat\gpu.log`, on Linux
  `~/.config/Darwin's Cat/gpu.log` — with the date and time, the amp, the format and the host it was loaded
  in, the graphics card's own name for itself, and the reason in its driver's words. Send it to us with a
  report. The file stays small: past 256 KB it keeps only its newest half.

#### Changed
- **The cat lights up under your hand.** Point at it and it glows softly from behind, violet, like frosted
  glass with a lamp coming up under it. Click it, as before, to open the amp's page.
- **HPF and LPF are keys again, not long pills.** The two filter keys under the tone knobs are close to
  square now, as tall as before, and stand evenly spaced in the row instead of stretched across it — the
  same chrome key with the filter's sign cut into it.
- **The tone knobs are the capture's own** — as many as the playing channel's capture has, in its order
  and under its own names, each on a socket of its own; another channel with another count of them lays
  the row out again. Where there are more than the panel holds at full size, the knobs shrink to fit.
- **The keys right of HPF and LPF are the capture's switches** — one for each switch the channel lets the
  hand throw, as many as there are, in a frame of their own. The dimmed FAT, BRIGHT, DEPTH and PRESENCE,
  which waited for layers no capture carries, are gone.

### RedPawAmp

#### Added
- **When the view out of the window cannot start on your graphics card, the amp now writes down why.** The
  windscreen falls back to the 1993 landscape and the cockpit's sky to its stars on their own, as before;
  now each time it happens a line goes into `gpu.log` in Darwin's Cat's folder — on the Mac
  `~/Library/Application Support/Darwin's Cat/gpu.log`, on Windows `%APPDATA%\Darwin's Cat\gpu.log`, on Linux
  `~/.config/Darwin's Cat/gpu.log` — with the date and time, the amp, the format and the host it was loaded
  in, the graphics card's own name for itself, and the reason in its driver's words. Send it to us with a
  report. The file stays small: past 256 KB it keeps only its newest half.

#### Changed
- **The cat lights up under your hand.** Point at it and it glows softly from behind, Mars-orange, like
  frosted glass with a lamp coming up under it. Click it, as before, to open the amp's page.
- **The cat stands straight on the dashboard.** The painted hexagon plate under the windscreen is gone, and
  the cat is cut straight into the metal there, larger, in a ginger close to the plains outside.
- **The tone knobs are the capture's own** — as many as the playing channel's capture has, in its order
  and under its own names, each on a socket of its own; another channel with another count of them lays
  the row out again. Where there are more than the panel holds at full size, the knobs shrink to fit.
- **The keys right of HPF and LPF are the capture's switches** — one for each switch the channel lets the
  hand throw, as many as there are, in a frame of their own. The dimmed FAT, BRIGHT, DEPTH and PRESENCE,
  which waited for layers no capture carries, are gone.

#### Fixed
- **Opening the window no longer stops the host now and then.** Building the landscape's slopes as the window
  opened read a little past the end of its height texture, and where nothing lay after it the host stopped
  (Cubase, switching between plug-ins). The slopes at one column of the texture were wrong too; they are right
  now.

## [0.8.0] — 2026-10-19 — Darwin's Cat Amps: OrbitAmp, CatpitAmp and RedPawAmp in one installer

### Every amp

#### Changed
- **The build time in the About window reads on your own clock, and says which zone.** It used to show
  the UTC stamp as a bare time, so a build made just after midnight in Berlin read as the evening before.
- **The cabinet convolves through Darwin's Cat's own convolver**, the one OrbitCab plays, instead of
  JUCE's. At 48 kHz it sounds exactly as before. At other session rates it follows the IR more
  closely: across the factory shelf, JUCE's was 1.5 to 2.5 dB off between 8 and 16 kHz and over
  4 dB in the top octave; this one is a tenth of a decibel off below 16 kHz and under one above. The
  cabinet's level is the same at every rate.
- **On an Intel Mac or PC the reverb plays exactly what it plays on Apple Silicon.** The two used to
  part by up to a millionth of full scale, some 120 dB down.
- **One cabinet shelf for the whole family**, laid out in `Darwin's Cat/Factory/IRs`, one folder to a
  maker: **Orange PPC212V** for Darwin's Cat's own ten, **Other** for the 21 third-party ones OrbitAmp has
  always played. OrbitAmp carries all 31; CatpitAmp and RedPawAmp carry the
  Orange's ten, as before, from the same shelf.
- The cabinet lists follow those folders: OrbitAmp's IR menu has a submenu for each, with each cabinet's
  line inside, and the faces' cabinet window heads the Orange's list before YOURS.
- **One folder for your own IRs, for the whole family**: `Darwin's Cat/IRs`, beside `Factory`. OrbitAmp's SETUP
  tree adds, renames, deletes and drags there, and the faces take dropped files in there, so an IR you add in one
  product is there in all of them.
- On its first start, each product **copies** the IRs it kept in its own `Darwin's Cat/<Product>/IRs` into the
  family folder, once, folders and all. The old folder is left as it was. An IR already there under the same
  name with the same bytes is not copied again; one with the same name and other bytes is kept beside it as
  "name (Product).wav".
- The version window lists `felitronics-guitar-core`, where the capture player now comes from, beside the
  other libraries the build was made from.
- **Of two Darwin's Cat amps that carry the same pack, the newer build's copy is the one in the shared
  `Factory` folder**, and both play it. An older amp started afterwards leaves that copy as it is,
  instead of the two writing their own back in turn every time either starts. A copy that has gone
  missing is put back by whichever amp starts first.
- **The packs Darwin's Cat's amps come with lie in one folder they share**, `Darwin's Cat/Factory`, beside
  each amp's own folder: a pack two of them carry is one file there, and OrbitAmp's device list shows
  every pack laid out in it, whichever amp laid it there. When an amp stops carrying a pack, the pack
  stays as long as another amp still carries it. What you add yourself stays in OrbitAmp's own `Devices`
  folder, as before.

#### Fixed
- **About counts the build from the release it follows.** The number of commits past the last release counted
  from whichever tag in the tree was made last — CatpitAmp's own, after its release — so OrbitAmp's About
  counted from CatpitAmp's release. It counts from the releases' `vX.Y.Z` now, in every product.
- **The delay's repeats arrive exactly when TIME says.** After the time was changed, the echo settled a
  hair short of the new time and stayed there — at 48 kHz, 28 samples early at 1.5 s and 2.3 ms at a
  whole note at 40 BPM — so every repeat came that much early. It now lands on the time it is set to.
- **A reverb that has just been loaded no longer lets a flash of the dry guitar into its first 10 ms.**
- **The delay no longer reads past the end of its own memory.** On an Apple Silicon Mac it did so at its
  default settings, 31 times a second, whether it was switched on or only in the rig; on any machine
  some offset and time settings did the same. Whatever it found there was added to the sound. Usually
  that was silence. When it was not, it was a click or a burst of noise, and the limiter could pull the
  level down for a moment after it.
- **A captured device no longer replays a moment of the last sound when the host restarts the audio.**
  Changing the buffer size or the sample rate — and, in some hosts, pressing play — prepares the plugin
  again, and a capture could answer the first silence after that with a scrap of what it was playing
  before. It now starts clean.
- **After a sample-rate change, moving a dial across a pack's captures crossfades to the next one when it
  is ready — not before, and not long after.** The wait was still counted at the old rate: half as long
  as the capture needs after going from 48 to 96 kHz, four times too long after going from 192 down to 48.
- **With PACK LEVEL COMP off, restarting the audio no longer shifts a device's level for a moment.** For
  about 40 ms it played at the pack's own input trim — the one the switch turns off — and then crept back.
- **macOS 11 Big Sur and later**, on Apple Silicon and on Intel: every format of every amp says so, and opens there.
  OrbitAmp 0.7.0 and CatpitAmp 0.2.0 said nothing, and were marked for the macOS of the machine that built them —
  macOS 26 — so an older Mac would not open them.

#### Dependencies
- `felitronics-core` **v0.53.0 → v0.59.0.** Nothing the plugin plays sounds different.
- `felitronics-appkit` **v0.21.0 → v0.21.1** — the build time above.
- `felitronics-appkit` **v0.21.1 → v0.22.0** — the update check can tell one product's releases from
  another's in a repository that ships several, and the family's mark is drawn at its size instead of
  faded. Nothing OrbitAmp plays sounds different.
- `felitronics-core` **v0.60.0 → v0.61.0** — the delay and the reverb now come from it, the same code
  the browser demo plays. With the knobs standing still the delay sounds as it did to within 2.4e-7
  and the reverb to within 6.5e-5 (MODULATED; the other five rooms within 3e-14).
- `felitronics-core` **v0.31.0 → v0.53.0.** The capture player — the NAM engine and the pack player — moved
  out of it into its own library.
- `felitronics-guitar-core` **v0.1.0**, new: that library.
- `felitronics-core` **v0.59.0 → v0.60.0**: the first release with `felitronics::dehum`, the hum canceller HUM
  is built on.

### OrbitAmp

#### Added
- **HUM — mains hum taken off the input** (OrbitAmp and CatpitAmp). Before the input trim, the 50 or 60 Hz hum
  and its harmonics are cancelled by a comb of notches that follows the mains. The notches are 0.5 Hz wide while
  you play, so a note beside a harmonic is barely touched, and 3 Hz wide while the strings are quiet, so the hum
  is learned fast. The ring a note leaves in a notch is put back when the note stops, so a palm-muted chug no
  longer drags an after-ring behind it.
- **Off / 50 / 60 / Auto**, a machine setting shared by every window. Auto listens and decides 50 or 60 Hz by
  itself in the first quiet second, and its line says "listening" until then. On a machine that never chose,
  HUM starts Off and its first On is Auto. Off is a bypass: the stage keeps learning, and the way back is instant.
  It listens only to the inputs the chain plays, so a hissing unused input does not hold Auto back.
- **Where**: CatpitAmp has it in the INPUT lever's menu and in the settings menu's INPUT section, OrbitAmp in its
  input column. The **H** key flips HUM between Off and the last mode, and on the cockpit a tag says which.
- **LEARN** heads every HUM menu: mute the strings, press it, and the stage learns the hum in a couple of
  seconds and shows its verdict. The gate's LEARN stays the gate's own.
- Machine settings for tuning by ear: the notch width while playing (0.25–4 Hz), the width while quiet
  (1–6 Hz) and how high it reaches (1.2–6.4 kHz, 2 kHz by default).
- Known limits: a note held longer than 3 s with a partial on a mains harmonic can leave a short ring after it
  stops. The plain case is the A string on 50 Hz mains, 5 × 110 = 11 × 50. A soft attack right after a note can
  leave one too.
- **Ten cabinet IRs of our own: the Orange PPC212V**, the cabinets CatpitAmp plays — DARK MATTER, ION
  CAVITY, EVENT HORIZON, CORONAL FLARE, BROWN DWARF, ACCRETION GAP, DUST CLOUD, BLUE SHIFT, RED GIANT and
  PULSAR BEAM. Darwin's Cat shot them on one 2x12, each IR a mix of three microphones at once: an SM57,
  an e906 and a U87 Ai.
- **Six Victory V4 packs come with OrbitAmp, in the AMP's OLD**: V4 Copper, V4 Copper TB, V4 Sheriff Ch1,
  V4 Sheriff Ch2, V4 Kraken Ch1 and V4 Kraken Ch2 — The Copper, The Sheriff and The Kraken, captured by
  Darwin's Cat. They ride inside the plug-in and the app like the X88-IR's, and are laid out in
  `Darwin's Cat/Factory/OLD` the first time OrbitAmp starts. They are not among the family's devices: the top
  of the AMP's menu lists the X88-IR and the IR-X, and the V4's six are in its OLD submenu, one to a line. If
  you keep an `OLD` folder of your own in `Devices`, the menu still has one OLD, with yours and these in it. A
  copy of one of them already in your own `Devices` folder stays there; the lists show the one that came with
  OrbitAmp.
- **The X88-IR comes with OrbitAmp**: its four channels as four preamp packs — X88-IR Normal, Altered,
  Crunch and Overdrive, the captures CatpitAmp plays. They ride inside the plug-in and the app
  themselves, in every format and on every system, and are laid out in `Darwin's Cat/Factory`, the folder
  Darwin's Cat's amps share, the first time OrbitAmp starts. A copy of one of them already in your own
  `Devices` folder stays there; the
  lists show the one that came with OrbitAmp.

#### Changed
- **OrbitAmp's PREAMP is called AMP**, as the faces call it: its arrow in the strip, its face on the
  panel, its list in SETUP, its rows in the load window (AMP and AMP EQ) and its controls as a host
  lists them. A session keeps its settings: only the names a player reads changed.
- **The Cab IR menu is sorted by cabinet**: Orange PPC212V, Behringer BG412S and Marshall 1960AX, each a
  submenu that opens on a line saying what its IRs are — the cabinet, its speakers, and where they come
  from. They are always there, at the top; your own IR library follows below them when it holds any.
  A preset or a session saved on one of the factory IRs opens on the same one.
- **A new OrbitAmp opens on DARK MATTER**, the first of the Orange's, instead of BIG BUBBA. A preset or a
  session keeps the cabinet it was saved with.
- **A new OrbitAmp opens as CatpitAmp's Crunch channel**: the X88-IR Crunch through DARK MATTER, with no
  boost in the chain, the input and output columns in it at unity, and the delay switched on with its
  mix at zero. INIT goes back to the same. A preset or a session keeps what it was saved with.

#### Removed
- **OrbitAmp has no BOOST block any more.** The chain is the tuner, the gate, the preamp with its EQ,
  the delay, the reverb, the cabinet and the limiter: the boost, its EQ, its arrow in the strip, its
  face on the panel, its row in the load window, its list in SETUP and its parameters are gone. The
  preamp has the faceplate's first row to itself. A preset or a session saved with a boost in it
  opens without one.

### CatpitAmp

#### Added
- **CatpitAmp**: GATE's threshold and LIMITER's ceiling each on a short fader, in a slot cut into the
  slanted beam beside its corner lamp and lying at the lamp's slant — the gate from -70 dB at its foot to
  -40 at its head, the limiter from -3 dB to -0.1. Dim while its block is off; a double click puts it back
  where it opens, a right click is that block's menu, and while it is held a tag says where it stands.
  The lamps and the words over them light and open their menus as before.
- **CatpitAmp, RedPawAmp**: LEARN in GATE's menu — OrbitAmp's own measurement: three seconds
  of quiet, and the gate's threshold set the hysteresis above the loudest of them, the gate switched on.
  While it listens, the face's own screen shows it — CatpitAmp's tuner glass, the rovers' nav screen:
  LEARN — STAY QUIET, the level growing across, the threshold it would set dashed; then the threshold
  set, CANCELLED or NOTHING HEARD for two seconds, and the screen as it was. A click on the screen
  meanwhile cancels it, and so does anything that moves the gate — a preset, an undo, a register, its
  fader.
- **HUM — mains hum taken off the input** (OrbitAmp and CatpitAmp). Before the input trim, the 50 or 60 Hz hum
  and its harmonics are cancelled by a comb of notches that follows the mains. The notches are 0.5 Hz wide while
  you play, so a note beside a harmonic is barely touched, and 3 Hz wide while the strings are quiet, so the hum
  is learned fast. The ring a note leaves in a notch is put back when the note stops, so a palm-muted chug no
  longer drags an after-ring behind it.
- **Off / 50 / 60 / Auto**, a machine setting shared by every window. Auto listens and decides 50 or 60 Hz by
  itself in the first quiet second, and its line says "listening" until then. On a machine that never chose,
  HUM starts Off and its first On is Auto. Off is a bypass: the stage keeps learning, and the way back is instant.
  It listens only to the inputs the chain plays, so a hissing unused input does not hold Auto back.
- **Where**: CatpitAmp has it in the INPUT lever's menu and in the settings menu's INPUT section, OrbitAmp in its
  input column. The **H** key flips HUM between Off and the last mode, and on the cockpit a tag says which.
- **LEARN** heads every HUM menu: mute the strings, press it, and the stage learns the hum in a couple of
  seconds and shows its verdict. The gate's LEARN stays the gate's own.
- Machine settings for tuning by ear: the notch width while playing (0.25–4 Hz), the width while quiet
  (1–6 Hz) and how high it reaches (1.2–6.4 kHz, 2 kHz by default).
- Known limits: a note held longer than 3 s with a partial on a mains harmonic can leave a short ring after it
  stops. The plain case is the A string on 50 Hz mains, 5 × 110 = 11 × 50. A soft attack right after a note can
  leave one too.

#### Changed
- **CatpitAmp and RedPawAmp carry their pictures finished.** Each used to correct
  its panel picture every time it opened — the cockpit its windshield, its tone sockets and its TURBO frame,
  a rover its rims, its lever's notches and its corner plates, each the lamps it lights itself. That is now
  done once, in the tree, and the products carry the result as lossless WebP. What a player sees is the
  same, pixel for pixel, and each product is smaller — by 4 to 8 MB on a Mac.
- **Every amp but OrbitAmp is lighter** — by 5.7 MB in each format on the Mac, where the binary holds an Apple
  Silicon half and an Intel one, and by 2.8 MB elsewhere. CatpitAmp and the rovers carry the Orange PPC212V's
  ten cabinets, the ones they list and play, and no longer the 21 third-party IRs that only OrbitAmp lists. A
  host that moves their Cabinet IR parameter to one of those 21 gets DARK MATTER, the cabinet they open on.

### RedPawAmp

This tree now builds a third product beside OrbitAmp and CatpitAmp: RedPawAmp, the dashboard of a Mars rover
over the same engine. It comes in the family's release beside them.

#### Added
- **CatpitAmp, RedPawAmp**: LEARN in GATE's menu — OrbitAmp's own measurement: three seconds
  of quiet, and the gate's threshold set the hysteresis above the loudest of them, the gate switched on.
  While it listens, the face's own screen shows it — CatpitAmp's tuner glass, the rovers' nav screen:
  LEARN — STAY QUIET, the level growing across, the threshold it would set dashed; then the threshold
  set, CANCELLED or NOTHING HEARD for two seconds, and the screen as it was. A click on the screen
  meanwhile cancels it, and so does anything that moves the gate — a preset, an undo, a register, its
  fader.
- **RedPawAmp** — four channels, picked as two gears and TURBO: the gear lever's CRUNCH and HI-GAIN, and
  TURBO over either. Each gear's own switches are on the keys right of HPF and LPF, under their own names,
  the position the switch stands at lit: CRUNCH's BRIGHT 1, CENTER and BRIGHT 2, HI-GAIN's TIGHT and FAT.
  Each gear keeps its own DRIVE, tone, switches, keys, reverb, delay and cabinet, and opens on a room of its
  own; TURBO is an option of the gear's, as its tone is — thrown, nothing else on the dashboard moves, and a
  gear pulled comes back with TURBO as it was left. Keys 1 2 pick the gear and
  T throws TURBO; MIDI Program Change 1–4 picks the four channels, controller 20 is TURBO.
- **PARK**, the lever's top end: the whole amp out of the sound, the guitar coming through dry and in time
  — at the INPUT and OUTPUT columns' levels, as the amp was, never louder past a master turned down —
  the rooms and the delay ringing out — and the drive outside comes to a stand. A gear pulled plays again
  on the channel that was waiting. 0 or P on the keyboard, Program Change 5, or the PARK parameter in a host.
- Through the windscreen, an endless drive across Mars — on the GPU, as a 1993-style landscape on the CPU,
  or a still picture — chosen from a right click on the glass; a drag across the glass steers.
- The cyan ring round DRIVE and each tone knob shows where the knob stands — lit from the start of its
  travel to its pointer — and brightens under the hand, easing back a moment after it has gone.
- GATE's threshold and LIMITER's ceiling each on a short fader down its corner lamp's plate: the gate
  from -70 dB at its foot to -40 at its head, the limiter from -3 dB to -0.1. Dim while its block is off,
  and a right click on it is that block's menu.
- Everything else the cockpit has, on the dashboard: DRIVE and the three tone knobs, the six keys, undo,
  redo and the four registers on the roof's toggles, REVERB, DELAY and CAB, the INPUT and OUTPUT columns
  with their lamps, GATE and LIMITER, the tuner on the nav screen, presets, the cabinets' window, MIDI.

#### Changed
- **CatpitAmp and RedPawAmp carry their pictures finished.** Each used to correct
  its panel picture every time it opened — the cockpit its windshield, its tone sockets and its TURBO frame,
  a rover its rims, its lever's notches and its corner plates, each the lamps it lights itself. That is now
  done once, in the tree, and the products carry the result as lossless WebP. What a player sees is the
  same, pixel for pixel, and each product is smaller — by 4 to 8 MB on a Mac.
- **Every amp but OrbitAmp is lighter** — by 5.7 MB in each format on the Mac, where the binary holds an Apple
  Silicon half and an Intel one, and by 2.8 MB elsewhere. CatpitAmp and the rovers carry the Orange PPC212V's
  ten cabinets, the ones they list and play, and no longer the 21 third-party IRs that only OrbitAmp lists. A
  host that moves their Cabinet IR parameter to one of those 21 gets DARK MATTER, the cabinet they open on.

## [0.7.0] — 2026-09-13 — your own cabinets, and a plugin that does not go quiet

Two things a player will notice first. The cabinet plays IRs of your own, and a preset carries
them. And one bad sample can no longer silence the plugin for the rest of a session.

### Added
- **Your own cabinet IRs in the Cab IR menu.** With nothing in the IR library the list is the
  factory shelf as before. Otherwise a **Factory** submenu sits at the top and your library
  follows as its folder tree, with a tick on what plays. WAV and AIFF are read; anything longer
  than 5 seconds is not a cabinet and is refused.
- **A preset or a session carries your IR whole — the file's bytes, not its path.** It sounds the
  same after the file is renamed, moved, deleted, or on another machine. An automation lane on
  the cabinet still moves it: a host moving `Cabinet IR` lets your own IR go.
- **SETUP → IR is a shelf you can shape.** Every folder row has a **+** to add IRs into it.
  Right-click offers Rename, Add IRs here, New folder inside, and **Move to** the top level or
  any folder. Rows select like a file list (click, ⌘, ⇧), and a selection drags onto a folder.
- **The tuner has a MUTE pill** under its cents: silence at the output while you tune, and the
  tuner keeps listening.
- **SETUP → TUNER: level floor** −80 / −70 / −60 / −50 dBFS, −60 by default. The quietest the
  tuner still reads; raise it if hum with nobody playing shows as a note.
- **The delay follows the host's tempo, or keeps its own.** In a DAW the time menu has TEMPO:
  HOST (the default) or OWN. While the host conducts, the field shows the host's tempo.

### Fixed
- **The plugin no longer goes silent some while into a session.** A single NaN or infinity that
  reached the reverb, the delay or the limiter stayed in it for ever, until the link was taken
  out of the rig. The reverb and the delay now heal from it, and the limiter's detector ignores
  it. Nothing that is not a number leaves the plugin: a bad sample becomes silence instead of
  taking the host's whole mix with it. A parameter set to NaN by a broken automation lane reads
  as its default.
- **Space starts and stops your DAW again** while the plugin window has focus. It toggles the
  demo loop only while the demo player is on show. Before, it also started a loop nobody could
  see, in place of the guitar.
- **The tuner reads what you play:**
  - a string tuned up from flat no longer reads flat for its first moments, because the needle
    starts from the new note;
  - the tail of a low string under hiss no longer wanders several cents off;
  - clean high notes are no longer thrown away;
  - 96 kHz reads as well as 44.1 and 48.
  
  The needle is also a little steadier.
- **Importing a pack you already have updates it instead of adding a copy.** Packs are known by
  their capture id. The old copy goes to the Trash; a pack that ships with OrbitAmp is not
  replaced, and the library says so. Copies already on disk show once in the device lists.
- **Hosts that save from a background thread** get a finished copy of the session, never one read
  while it was changing.
- **A host block longer than the host promised** plays correctly. The reverb added its tail to
  only the first part of such a block.
- **The limiter lets go within half a second** after a huge spike, not four.
- **SETUP's lists read at 13 px.**

### Dependencies
- `felitronics-core` **v0.30.0 → v0.31.0.**

## [0.6.0] — 2026-09-11 — every capture that plays off its own rate sounds like itself again

The headline is not a feature. It is that a captured device playing at a session rate other than
the one it was captured at has been quietly wrong since the first release, in two ways at once, and
both are gone.

### Changed
- **The top octave is back.** A capture is resampled on the way into its model and back out again,
  and the interpolator doing that was a cubic — cheap, and audibly not transparent. One round trip
  at 44.1 kHz against a 48 kHz capture cost **4.17 dB at 17.6 kHz**. That is not a subtlety on a
  high-gain amp: it is the difference between a capture that keeps its air and one that arrives
  dulled, every time the session is not at the pack's own rate. The new kernel measures
  **+0.0002 dB** in the same place.
- **And the bass is clean.** Worse than the missing top was what the missing top turned into. The
  resampler's error was not a fixed filter — it changed with the phase of each sample, at the rate
  of the conversion, so a driven nonlinear stage folded it down into a **100 Hz line at −17.65
  dBFS: fourteen and a half decibels LOUDER than the tone that produced it.** A rumble that no
  amplifier in the chain put there. It now measures **−96.54 dBFS**, sixty decibels under its own
  carrier.
- ⚠️ **The latency the plugin reports to your host has changed, and by a lot: 8 samples to 122 at
  44.1 kHz** with two captured blocks in the rig. That is the honest cost of the new kernel's
  geometry, and the host will compensate for it — but a session saved with 0.5.x will have its
  track re-aligned by that difference when it reopens. Nothing is lost; it moves once.

### Fixed
- **A bypassed block above 48 kHz was not the wire it claimed to be.** Switching a captured block
  out is supposed to leave a delay exactly as long as the block it replaces, so the chain does not
  jump forward and so the fifteen-millisecond crossfade is not a signal blended against an early
  copy of itself. The delay it left was capped at 64 samples by a constant whose comment justified
  it with arithmetic two kernel generations old. At 88.2 kHz the block asks for 91, at 96 kHz for
  96, at 192 kHz for 160 — and the cap cut silently. What that sounds like: a comb through the
  switch, first null at **1500 Hz** at 96 kHz, and a bypassed block sitting permanently early
  against anything you have the track bussed to in parallel. The length is computed from the
  geometry now, asked of the resampler rather than restated, so it cannot fall behind the kernel
  again.
- **A block that has just been switched out no longer opens on silence, and one switched in no
  longer leaves a ghost.** The delay line is fed on every block rather than cleared when idle, so a
  capture landing mid-session — picking a device with the block still switched off — does not cost
  a millisecond of hole in the through-path.
- **In MONO, the right channel no longer keeps what it heard in STEREO.** Its history had simply
  stopped being written, so the next stereo block replayed a stretch from before the switch.

### Added
- The rig lives in the strip, and the cabinet stops guessing (#28).
- A restored trim is a window, never a hand (#29).
- The display face arrives once, from the library that owns it (#30).
- The toolbar sets a player's words in a face that can spell them (#31).

### Dependencies
- `felitronics-core` **v0.29.0 → v0.30.0** — the resampler kernel above, its round-trip delay
  published as one function so no consumer restates it, and the same cure applied to the player's
  own dry alignment.

## [0.5.1] — 2026-09-07 — the plugin stops guessing how long it rings

Two things 0.5.0 was not telling the truth about, both found by reading it back the morning after.

### Fixed
- **The declared tail was a flat eight seconds, and eight is wrong in both directions.** A host asks
  how long this keeps sounding after the input goes quiet — for an offline bounce, for a freeze,
  for deciding when it may stop calling us — and it asks about every silence, not only about a
  block standing by. Eight was too SHORT: a HALL at double decay rings for nearly ten, and a
  two-second echo at 95% repeats takes over four minutes to fall to a thousandth of itself, so a
  bounce could end mid-tail. It was also too LONG: with the room and the echo out of the rig there
  is nothing here that rings at all, and every bounce still had eight seconds of silence rendered
  onto the end of it. The three links that have tails — the echo, the room and the speaker's own
  impulse — are asked what theirs currently is, and only while they are in the rig. They ADD UP
  rather than compete, because they are in series: an echo leaving two seconds after the note is
  what the room then rings about. Capped at thirty seconds, because a delay at a hundred per cent
  repeats never decays and no honest number exists for it — and floored at the old eight while
  either the echo or the room is in the rig, because there is no message in any plugin format that
  says "my tail changed": a host that asks once, when it loads the session, keeps that answer. So
  the number is honest when a host re-asks and never worse than 0.5.0's when it does not.
- **The plug-ins in the macOS .zip carried no stapled notarization ticket.** They were notarized —
  they went up with the installer — but only the standalone was submitted on its own and so only
  the standalone came back with a ticket attached, and stapling the .pkg staples the package, not
  what is inside it. Gatekeeper would fetch the verdict over the network on first load; a stapled
  ticket is that same verdict already in the bundle, which is what a machine with no network needs.
  One submission now carries all four, and each is stapled before anything is packed.

## [0.5.0] — 2026-09-07 — a rig you assemble, and a bypass that behaves like an insert

The case this one exists for: a player could switch a block off, but not decide which blocks their
instrument HAS — and every switch in the window meant something slightly different depending on
what it sat next to. There are two switches now, the same two on every link, and the strip is a
picture of the rig rather than a list of things that happen to be here.

### Added
- **Three states on every link — ON, STANDBY, OFF.** Two ordinary automatable parameters,
  `*_present` and `*_on`, so a rig travels in the preset, the session, the A/B/C/D registers and
  undo with no code about storage. The arrow switches standby and on; Setup's EDITOR page decides
  what the rig has at all. Two switches rather than one three-position control because the two
  actions are not the same weight: bypassing is something players automate, removing is not, and on
  one lane aiming at the middle of three and landing on the end takes a block out of the rig when
  you meant to step it aside.
- **Standby is an insert's bypass, as a DAW means it.** A link stops taking new signal but keeps
  running, so a delay's repeats and a reverb's tail ring OUT instead of being chopped. Links that
  replace the signal — boost, preamp, cabinet — crossfade to what they were handed over 15 ms.
  Nothing is cut any more: switching, letting the safety go, and leaving the rig all fade.
- **Setup is a window with three pages** — LIBRARY, EDITOR, VIEW — opened by the gear, which used
  to drop a menu carrying a door to Setup, two doors to pages nobody sets anything on, and four
  switches. Each page says whether it travels with the preset or stays on this machine.
- **The chain strip is one grammar.** The end caps became rows like any other, so IN and OUT answer
  to the same two switches as everything else, and the gate and the limiter have consoles on their
  own arrows instead of riding somebody else's rail.
- **DEVICES & TRADEMARKS moved to the footer**, beside the build stamp: the two pages that are only
  ever read, together, reached from what they are about.
- **`KEEP WINDOW HEIGHT`, `STANDBY KEEPS ITS PLACE`, `EQ HANDS OVER THE CURVE`** — an emptied row
  can hand its height to whoever is left rather than shrink the window; a link standing by can keep
  its place and go dark; the EQ console's row of hands can sit over its curve. All three are about
  the eye, so all three stay on the machine and never enter a preset.

### Changed
- **The power amp is gone from the code.** It had no button — no arrow, no tile, no tab — because
  no pack ever shipped for it, and a block a player cannot see is a block that is not in the
  instrument. The history has it.
- **IN and OUT start out of the rig.** The volumes that matter are the captured blocks' own IN
  trims; a global fader is what a player reaches for when a rig needs fixing.
- **The gate's mute position is named for the chain** — START and END. "Pre-Reverb" was a strange
  thing to read on a rig that may have no reverb in it. The position itself has not moved: END
  falls after the preamp and its console, so the hiss those two add dies with it and whatever the
  delay and the room are holding rings out underneath.
- **The footer stands at the house floor of 13 px** and grew four pixels to hold it. The four facts
  on that strip — the build, what is installed, the host's rate, the cost — are read as sentences,
  and they were set at 12, with the cost breakdown behind them at 11 and its heading at 10.
- **A device's tone points are markers, not handles.** They say where each measured knob acts
  hardest; the knob below is the hand. Their drag converted decibels into knob travel through a
  single measured swing, which is honest as an indicator and was not as a control.
- **A bypassed link keeps its latency**, deliberately, because that is what a bypassed insert does:
  a rate-matching model reports a delay the host compensates for, so the bypass path carries the
  same delay rather than letting the chain arrive early.
- **What standing by COSTS depends on the kind of link, and the breakdown says which.** A link that
  adds — the delay, the room — keeps running, because that is what lets its tail finish, so it goes
  on costing what it costs. A link that replaces stops once its fifteen milliseconds are over: the
  model is not asked, and a preamp standing by is very nearly free. Both keep their row and both
  read their real number, faintly.
- **The cost list is the rig.** A link out of it has no row, because it is not in the chain.

### Fixed
- **The plugin declared no tail** while a room and an echo now ring on purpose — a host told there
  is no tail may cut an offline render exactly where we started holding one.
- **The cabinet kept its convolution across standby**, so the first IR-length after it came back
  convolved what was played before it stood down.
- **A room out of the rig cleared itself every block** — about 150 KB, some 120 MB/s at 64 samples,
  untimed and invisible because its row was not in the list.
- **The OUT cost row carried the limiter's time as well as its own**, so the breakdown did not add
  up — which is the one thing a breakdown is for.
- **Fifteen milliseconds means fifteen milliseconds.** The fade interpolated across the whole block,
  so at 4096 samples it took eighty-five.
- **A pack naming a slot this instrument does not have is offered nowhere.** Saying nothing and
  saying something we do not have had been given one answer, so a pack calling itself a power amp
  turned up in the boost list.
- **A device and its switches were remembered by NUMBER.** A device's parameter is an index into
  the scanned packs, and the scan sorts bundled-first and then along the gain ramp — so dropping one
  new pack into the folder renumbered every device after it, and every session and preset that named
  one by number named a different one. Its switches had the same disease a floor down: a fraction is
  an index into the pack's own position list, so a device repacked one position shorter reopened
  every old session on a different position. Both now carry the NAME beside the number, and the name
  decides on the way back — the device first, because the positions belong to its pack.
- **The names were written at save time, into the live tree.** That edited the state behind the
  player: the settle timer saw a change nobody had made and committed an undo step for it, so the
  next Cmd-Z moved a switch on the device. And saving a preset never goes through the host at all,
  so a preset carried either no names or the ones left from the last project save. They are written
  when a switch MOVES now, which leaves nothing to fix at save time.
- **A settings page could not see a switch move.** It read every row's state when it painted and
  nothing asked it to paint, so automation, an undo, a preset or a second editor left the toggle
  showing yesterday's answer.
- **Setup's LIBRARY page said nothing about its own lifetime** while both of its neighbours did.
  It is the one page with two answers — the folders are this machine's, the switch on it rides in
  the preset — and saying nothing read as a page nobody had thought about.
- **Two editors shared one LEARN.** The compare history had a single after-apply hook, and a host
  may open two windows on the same plugin — so whichever closed first silently disarmed the other's
  measurement. The gate console watches its own three parameters instead, which catches more than
  the hook did: a register recall, a preset, an undo, a hand on the switch, host automation.
- **Hiding a column no longer destroys its trim.** It used to write the value to zero so an unseen
  hand could not keep pressing; the chain simply does not apply a volume that is standing by, so the
  number stays where the player left it.
- Two menus outlived their windows; a column going dark under a held mouse left an automation
  gesture open; the drag rulers had lost their parent; Setup's sub-tabs could not be clicked and its
  small print was under the type floor.

## [0.4.0] — 2026-09-06 — a pack states how hard it is fed and how loud it leaves

The case this release exists for: a boost was too hot for the preamp it fed, and the only thing to
fix it with was the block's IN hand — which is a DRIVE control, so correcting a volume changed a
tone. A pack now carries its own two levels and the player applies both, so balancing one device
against the next costs nothing in character.

### Added
- **The pack's own levels** — `chain[].input_db` and `chain[].output_db` (namz schema 4), applied by
  felitronics::rigplayer with no switch anywhere. Nothing in this plugin implements them: the pin
  brought them, and what this release adds is the honesty around them.
- **The IN wall shows both sides of that level.** The bar fills to what the network is actually
  eating; a thin line marks what entered the block. Between the two stand the pack's `input_db`, the
  chain trim past the ends of the captured range, the active slot's alias trim, and an uncaptured
  device's drive — on a linked notch usually six decibels of daylight between the number drawn and
  the number meant. At 0 the two marks coincide, which is the honest picture of a pack that states
  no level. Between two DIFFERENT captures there is no single "network input" at all, and the panel
  says so rather than averaging two into a number true of neither.
- **The model's loudness tag, under every captured block** — the number when it carries one, and a
  warning in the hand's own colour when it does not, because such a model plays raw, some 8–10 dB
  from its neighbours, and nothing else on the face would report it.

### Fixed
- **The loudness tag named the wrong model.** It read slot 0 always, and slots go by knot parity: on
  an odd capture the whole sound comes out of slot 1, so the face named a silent neighbour's tag —
  or called a tagged model untagged. It now names what is sounding, and says "of two" mid-crossfade.
- **A silent block could draw a level.** The meter tap is clamped at −90 in the audio thread and the
  offset was added after that clamp, so a positive one lifted silence onto the scale and drew a
  sliver of fill and a peak-hold line on a block that was not playing.
- **Text that had become false.** `captureHotDb` said there was nothing in the pack to read about
  level; `blockIn` implied the only loudness fix between blocks is a drive change; `Pack Level Comp`
  claimed to be about the pack's level story when it bypasses the ALIAS trims and nothing else.

### Changed
- felitronics-core **v0.29.0**, namz **v4.1.0**. Normalizing by a model's loudness tag is on by
  default in the player and there is no switch here — it is a contract, not a listener's option.

## [0.3.0] — 2026-09-03 — the papers, and the devices they name

The plugin learns to say what it is: which build is running, what it was built against, whose
equipment it names and on what terms. It also learns the rig format's new shape — a knob that
clicks can carry bands — and ships with an empty factory, so what a player hears is what a player
put there.

### Added
- **The rig format's clicking knob** (namz v4.0.0, felitronics-core v0.25.0). A switch position now
  states the filter it IS, with no law of travel, because a switch has no rotation for a gain to
  ride. Schema 4 is a breaking read, so the pins move before the capture app writes it: a session
  saves a switch by NAME rather than by its place in the list, a switch is judged by the absence of
  rotation rather than by letters in its legends, and a switch that carries bands counts as a tone
  stack the face may offer.
- **The engine the build actually compiled.** The local NeuralAmpModelerCore copy is a shortcut, and
  a shortcut that changes what you build is not one: it is taken only when it sits on the commit
  felitronics-core pins, and a build directory holding a stale cached pin heals itself instead of
  quietly compiling the engine we replaced. The version window names what was compiled, from the
  same directory the compiler read.
- **An empty factory.** This release ships no packs at all: the device list says so, and every
  device a player drops into the Devices folder is theirs and named as such.
- **The build stamp, at the end of the footer.** `V0.2.0 · STANDALONE` in the strip's own voice,
  beside the sample rate and the DSP figure — a fact about the run, like everything else on that
  line. Click it and appkit's version popover steps out: the whole stamp (commit, build number,
  the machine it was built on), what it was built against — felitronics-core, felitronics-appkit
  and JUCE, each naming its tag, whether it came from a sibling checkout or the pin, and its
  commit — the licence, and the links home.
- **namz and NeuralAmpModelerCore in the dependency rows** — the codec the packs are written in and
  the engine that runs them, beside felitronics-core, felitronics-appkit and JUCE. namz's tag is
  matched as `v[0-9]*` on purpose: it carries per-language release tags for its ports, and "the
  nearest tag" would name one of those as the codec version. The engine is a pinned commit of
  somebody else's repository, so its row says where that pin sits between releases — `v0.5.3+11 ·
  pin · gb5a68c3`.
- **The title row leads in two directions.** The product's mark and name open OrbitAmp's page; the
  cat and "by Darwin's Cat" open darwinscat.com. Both carry the plugin's campaign tag, and the tip
  jar carries it too — beside the `from` / `platform` / `format` signature appkit adds for the
  access log (felitronics-appkit v0.13.0).
- **DEVICES & TRADEMARKS**, from the gear: the full third-party hardware notice above, and below it
  every device the installed packs name — maker, model, slot, points on the gain dial, year, serial
  and where it was built — with what shipped in the installer kept apart from what a player added.
  Each half scrolls on its own, and the list is re-read on every open.
- **One window for the product's papers.** The gear's ABOUT and a click on the footer's stamp open
  the same panel: the build stamp, the dependency rows, the licence, the opt-in update check, the
  tip jar — and now the trademark notice, carried at its foot from `resources/notice.txt`. The
  plugin's own About overlay is gone; there is one place a player looks and one text to keep right.
- **The cat in the popover.** The maker's mark stands beside the product's on the title row, the way
  the window header carries both — the same embedded SVG, not a second drawing of it.
- **An update check that asks first.** The popover's button queries this repository's latest
  release; a switch beside it lets the check run once a day on its own, off until it is ticked.
  Nothing reaches us either way: the request goes to GitHub, and the versions are compared on the
  machine. When a release seen by a check is newer than the running build, an orange dot lights
  beside the stamp and stays lit across sessions until the build catches up.
- **Feed the Cat**, at the foot of the same popover — the suite's one tip jar, reached through
  darwinscat.com's steerable hop, signed `?from=orbitamp&platform=<os>` so the access log can tell
  which app and which machine fed the cat. Neither parameter reaches the payment page.
- **`OrbitAmpVersion.h`**, baked on every build (`cmake/GenerateOrbitAmpVersion.cmake`): git
  describe, short hash, dirty flag, commits past the tag, a 14-digit UTC build number, the build
  OS/arch/builder, and the resolved dependency rows. End users have no repository; the binary
  carries its own stamp.

## [0.2.0] — 2026-09-02 — the box on the page

The device pages learn what a pack actually ships, the two tone stacks stop playing at once, and the
repository gets its first green build.

### Added
- **The BOX.** A pack ships a photograph of the thing it was captured off — namz's `picture`, a WebP
  cut-out in the pack root — and nothing read it. PHOTO is a page of its own beside the five scopes
  and the paper: the box alone, filling the tile.
- **The CARD.** Where the room seats them — a lone block's wide tile, a thrown-open face, the whole
  monitor — the box and its paper become one page, side by side, with the circuit standing between
  them as the divider. The tile keeps two pages, because 106 points cannot hold both.
- **What the paper says.** The MAKER instead of the voice's alias, and on the card the box's own
  history: the year it was built, the serial number stamped on it, where it was designed and made,
  and who made the capture. All from `gear` (namz v3.3.0); a pack that does not say shows nothing.
- **ABOUT**, from the gear menu — the product, its version and licence, the cat at a size worth
  looking at, the link home, and the trademark notice in one canonical wording the README carries
  verbatim. Names of other people's equipment identify the equipment; this page says so.
- **The corner glyphs come with the hand** and fade when it leaves, so a photograph is a photograph.
  The whole-screen view keeps a door in words at the top of the picture menu.
- **The spectrum reads in BANDS** on a picture that owns the face: `MultiResSpectrumPane`
  (felitronics-core v0.22) feeds three window lengths from one frame and reports a twenty-fourth of
  an octave — fifty-six readings between 20 and 100 Hz where a 2048-point window gave three and a
  half. Only there: it costs about fifteen times the classic pane, and the face runs six to eight.

### Changed
- **The two tone stacks are alternatives, and now they behave like it.** Choosing the device's own
  parked ours in name only — our bands went on filtering with no controls on the face to reach them.
  Only the tone sections step aside now; the cut filters and the level belong to the block whichever
  tone is playing. Where a device measured nothing there is no choice, so there is no chooser.
- **Every picture draws what the block does.** The tile was handed the device's measured curve alone
  and drew a flat line under a console showing a mountain. Both call one function now.
- The circuit symbols are drawn PER PART — four diodes are four diodes, which is what the line under
  them says.

### Fixed
- **The Windows gate had never run.** `AmpProcessor` is 1.5 MB and stood on the stack; Windows gives
  a main thread one megabyte where macOS gives eight, so the gate died in `main`'s prologue on every
  CI run this repository has ever had — reported as `127`, the low byte of `STATUS_STACK_OVERFLOW`,
  which reads as "command not found" and points the other way. The processors move to the heap, and
  the gates now say what they found and what they printed.
- The DEVICE page overflowed its own tile, printing its last two lines on top of each other.
- The whole-screen view opened blank on the two pages that carry no audio.

### Build
- libwebp builds for a universal macOS binary (felitronics-appkit v0.11.2): its SIMD probe compiles,
  and a two-architecture compile answered with one voice.
- Pins: felitronics-core v0.23.0, felitronics-appkit v0.11.2, namz v3.3.0.

## [0.1.0] — 2026-08-31 — the player leaves the bench

First release. A curated guitar tone player — VST3 / AU / CLAP / Standalone; macOS, Windows, Linux.

### Added
- **The chain**: boost → EQ → preamp → EQ → delay → reverb → power amp → cabinet, on one compact,
  resizable faceplate. The boost, preamp and power amp are captured neural voices; everything
  linear or time-based around them is honest DSP.
- **Captured voices** ride in `.orbitrig` device packs (namz schema 3), played through
  `felitronics::rigplayer`: a continuous gain dial over the captured positions — SMOOTH
  crossfades between them, STEP lands on the knots — and per-block tone controls that follow
  the device's own knobs (Native) or park them and hand the shaping to the player's EQ (OURS).
- **The library**: packs dropped into the user's `Devices` folder appear in the block's list;
  a factory layer inside the bundle sits underneath it. Files in a folder — no ceremony.
- **The cabinet**: a single impulse response with HPF / LPF / trim / phase baked into it,
  drawn as the IR's waveform. 21 cabinet IRs ship in the box.
- **The echo before the space**: a delay ahead of the reverb — repeats of what the preamp
  made, free or host-clocked, with a darkness on the repeats and a stereo offset that widens
  the back half. The reverb speaks six characters — Ambience · Room · Hall · Plate · Spring ·
  Modulated — with DECAY, PREDELAY and an always-in tail HPF as its late refinements.
- **Stereo** three ways: MONO, STEREO (everything twice), STEREO SPACE — mono where the sound
  is made, stereo from the reverb on, where the space is. A fresh instance follows its bus.
- **Around the sound**: input gate, tuner, output limiter (opens on SAFETY), A/B/C/D compare
  slots, scope with IN/OUT metering.
