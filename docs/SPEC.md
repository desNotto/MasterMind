# MetalBus — Modern Metal Master Bus Suite (VST3) — Technical Specification

Nov 26, 2026 · @des

## 0. Instructions for the coding agent

Build MetalBus as a Windows VST3 in small milestones, talk to the user in Russian, write everything else in English, and never start the next milestone before the user has tested a downloadable CI build in REAPER.

**Language**

- Chat with the user: **Russian** only. This covers explanations, questions, progress reports, error explanations and test instructions.
- Everything else is **English**: source code, identifiers, comments, commit messages, files in `docs/`, UI strings (see section 8). The one exception is `docs/README_RU.md`, a short Russian how-to for the user.
- The user is a guitarist, not a programmer. He cannot build locally and does not read code. Explain in plain Russian, define each DSP term in one sentence at first use (for example: true peak = the real waveform peak between samples), and prefer short lists and tables over code in chat. Paste code into chat only when asked.
- Keep every message short: what was done, what to test, what comes next.

**Workflow**

1. One milestone at a time (section 11). A milestone ends with: a green GitHub Actions run, a downloadable zip with the `.vst3`, and a Russian test checklist of at most 10 items for REAPER. Then wait for feedback.
2. This document is the source of truth. Copy it to `docs/SPEC.md` in M0. Record every deviation and its reason in `docs/DECISIONS.md`. If something is ambiguous, apply the default from section 12, tell the user in one Russian sentence, and continue. Ask first only when a wrong guess is costly: architecture, parameter IDs, file formats, licences.
3. Never claim that something works unless a test or CI run proved it. If a result can only be judged by ear or in REAPER, say so and put it in the checklist.
4. Do not add features that are not in this spec. Propose them at the end of a milestone.
5. Parameter IDs and the saved-state format are frozen after M1. A breaking change needs a migration and the user's approval.

**Visual changes: mockup first**

Any change that affects how the plugin looks follows this protocol. That means a new screen or panel, layout, colours, fonts, the look of controls and graphs, icons, window sizes, resize behaviour and animations.

1. Draw a **mockup first**: an image or an HTML preview, no plugin code. Show it at 100 percent (1280 x 800), at 120 percent (1536 x 960) and at one compact size whenever the layout changes with size. Include the states that matter (for example monitoring on, a traffic light in each colour).
2. Send the mockup to the user in Russian with a short description of what changes, and ask for approval.
3. **Wait for explicit approval.** If the user asks for changes, draw a new version of the mockup. Write no UI code in the meantime.
4. Only after approval write the UI code, matching the approved mockup. Save every approved mockup in `docs/mockups/` with a version and a date. Any later deviation needs a new mockup and a new approval.

A pure bug fix that restores the approved appearance needs no mockup. Work that does not change the look (DSP, analysis, files, tests) is not affected.

**Build and delivery**

- The user cannot build the plugin. All builds run in GitHub Actions on `windows-latest` (MSVC, Release, x64) and produce an artifact `MetalBus-<version>-win64-vst3.zip` containing the single .vst3 file and `README_RU.md` (details in section 3).
- Tell the user in Russian how to download the artifact from the Actions run, where to copy the plugin (`C:\Program Files\Common Files\VST3\`) and how to rescan plugins in REAPER (Preferences, Plug-ins, VST, Re-scan).
- Version scheme `0.<milestone>.<build>`, shown in the plugin footer together with the git commit hash.

**Real-time rules (non-negotiable)**

No heap allocation, locks, file or network I/O, logging or exceptions on the audio thread. The UI and background analysis talk to the audio thread only through lock-free FIFOs and atomics. Use `juce::ScopedNoDenormals` in `processBlock`.

**Agent skills, if installed**

If Claude Code skills for JUCE and DSP are available (for example `juce-plugin`, `dsp`, `juce-best-practices`, `juce-review`, `audio-artifacts-debug`), use them, and run the review skills after every DSP milestone. Do not depend on them: this spec must be enough on its own.

## 1. Product overview, goals and design principles

MetalBus (working title) is a master-bus VST3 for modern metal that combines mastering processing with objective metering, so one person with imperfect monitoring can shape, check and match a whole album by eye.

**Target material.** High-gain guitars on 7, 8 and 9 strings, extreme low tunings, loud and dense drums, wall-of-sound mixes. Reference points: an 8-string guitar in standard tuning has its lowest fundamental at about 46 Hz (F#1), a 9-string at about 35 Hz (C#1), and kick fundamentals sit around 45 to 65 Hz. Low-end control between 30 and 150 Hz is therefore the central problem, and the 3 to 8 kHz fizz of high-gain guitars and cymbals is the second.

**Goals**

- **G1. One album sound.** Bring tracks of different mixes to a consistent tonal balance, loudness, dynamics, stereo image and low end, and show the deviation of each track from the album target numerically.
- **G2. Translation to any system.** Headphones, speakers, car, phone, laptop, earbuds. The user owns only Beyerdynamic DT 990 headphones and Edifier R1280T speakers in an untreated room and cannot judge sub-bass or air by ear, so the plugin must provide trustworthy visual and numeric tools instead of relying on his ears.

**Core idea.** Translation cannot be guaranteed by any plugin. It is approached by measuring the mix against commercial reference albums that are known to translate: if every metric stays inside the reference corridor, the master should translate about as well as the references do.

**Design principles**

- **P1. Three separate layers.** (1) *Processing* changes the audio and goes into the render. (2) *Analysis* (the "Eyes") only measures and never changes audio. (3) *Monitoring* (correction, system emulation, solos, reference playback, loudness-matched A/B) is listen-only and must never reach a render or bounce.
- **P2. Monitoring safety.** Compensation for the user's own headphones or room baked into a master would be an invisible disaster. Section 6 defines the safety rules; they have priority over any convenience.
- **P3. Transparent by default.** A freshly inserted plugin does not change the sound (bit-exact passthrough). Every processing module starts bypassed or at neutral settings.
- **P4. Numbers next to every picture.** Every graph has a readout in dB, Hz, LU or percent, and every deviation has a plain-language label such as "Bass +2.3 dB vs target".
- **P5. Honest approximations.** Anything that is an estimate (system emulations, generic headphone curves, the reference corridor from few tracks) is labelled as an approximation in the UI.

## 2. User context and hard constraints

The plugin is built for one Windows 10 user working in REAPER with a Russian-speaking, non-programmer workflow, and every decision below follows from that.

| Item | Fact | Consequence for the build |
| --- | --- | --- |
| OS and host | Windows 10 x64, REAPER | Windows x64 and VST3 only in v1; REAPER is the primary test host |
| Building | The user cannot build locally | All builds via GitHub Actions; every milestone ships a downloadable artifact |
| Delivery | One plugin file | The product is a single `.vst3` file with all assets embedded and nothing else to install (section 3) |
| Visual changes | The user approves the look before any UI code | Mockup first, then code (section 0) |
| Headphones | Beyerdynamic DT 990 | Listen-only correction slot; the user supplies the correction profile (section 6) |
| Speakers and room | Edifier R1280T in an untreated room | Bass and sub-bass cannot be trusted by ear; speaker correction needs a measurement imported from outside (section 6) |
| Hearing check | Cannot judge bass and highs by ear | Visual and numeric analysis is the main product, so analysis modules come first in the milestones |
| Material | Modern metal, 7 to 9 string guitars, loud drums | Low-end tools, fizz control and clipper/limiter quality get the most attention |
| Use | Personal use is assumed | JUCE licence terms still apply if the plugin is ever distributed; note this in `docs/DECISIONS.md` |

Assets the user will provide (never bundle copyrighted audio in the repository):

- 5 to 10 commercial reference tracks (WAV or FLAC) from albums in the target genre that the user considers well-translating. They are loaded locally at runtime and are never committed to Git.
- A headphone correction profile for the DT 990 in ParametricEQ text format (for example an AutoEQ result).
- Later, optionally, a speaker or room measurement exported from REW as parametric filters or an impulse response.

## 3. Tech stack and architecture

Use JUCE with C++20 and CMake, build a stereo VST3 for Windows x64 in GitHub Actions, and keep the DSP and analysis code in a GUI-free library so it can be unit-tested.

**Stack**

- JUCE 8.x, pinned to one release tag via CMake `FetchContent`. C++20, CMake 3.22 or newer, MSVC (Visual Studio 2022 on `windows-latest`), Release x64.
- Targets: `juce_add_plugin` with `FORMATS VST3` (add `Standalone` only if it helps testing). Plugin category: effect. Stereo in, stereo out only; reject every other bus layout.
- Modules: `juce_audio_processors`, `juce_audio_formats` (WAV, FLAC, MP3, OGG for reference files), `juce_dsp`, `juce_gui_extra` if needed. No other third-party dependencies unless approved; no GPL code.
- Code layout: `Source/dsp/`, `Source/analysis/`, `Source/monitor/`, `Source/album/`, `Source/ui/`, `Source/state/`, `tests/`. The DSP and analysis code is compiled as a static library `metalbus_core` with no GUI dependency, linked into the plugin and into the tests (and later into the offline analyzer, section 7).

**Deliverable: one file**

- The product is a **single file** `MetalBus.vst3`. The user copies it to `C:\Program Files\Common Files\VST3\` and nothing else: no installer, no extra DLLs, no resource folders next to it.
- Everything the plugin needs is embedded in the binary: fonts, images, factory presets and string tables (`juce_add_binary_data`). The C++ runtime is linked statically (`CMAKE_MSVC_RUNTIME_LIBRARY` set to `MultiThreaded`), so no Visual C++ Redistributable is needed.
- JUCE normally writes a Windows VST3 as a bundle folder (`MetalBus.vst3\Contents\x86_64-win\MetalBus.vst3`). A post-build step in CI copies the inner binary to a flat single file `MetalBus.vst3`. CI must prove that the flat file passes pluginval, and the M0 checklist must prove that REAPER loads it. If the flat file is rejected by either, ship the standard bundle instead, tell the user in Russian and record the reason in `docs/DECISIONS.md`.
- Files created at runtime (settings, presets, caches) live only in `%APPDATA%\MetalBus\` and are created on first use; the plugin works without them.
- The optional offline analyzer (7.5) is a separate program and is not part of the plugin file.

**GitHub Actions** (`.github/workflows/build-windows.yml`)

1. Trigger on push and manually.
2. Checkout, configure with CMake, build Release.
3. Run unit tests with CTest (Catch2).
4. Download `pluginval` and validate the built VST3 at strictness level 5 or higher; the job fails if validation fails.
5. Upload the artifact `MetalBus-<version>-win64-vst3.zip` with the single .vst3 file and `README_RU.md`.

**Threading**

- Audio thread: `processBlock` only. Tap points (input and output) write into lock-free ring buffers (`juce::AbstractFifo`) and atomics.
- Analysis thread: one background `juce::Thread` reads the ring buffers and computes FFTs, loudness, correlation, spectrogram and album statistics. It publishes finished frames to the UI through a lock-free triple buffer or FIFO.
- UI thread: reads published frames at 30 to 60 fps via a timer or `VBlankAttachment`. Reference file analysis and album analysis run on their own worker threads, never on the audio or UI thread.

**Audio engine rules**

- Sample rates 44.1 to 192 kHz; block sizes 1 to 8192, including blocks that vary from call to call. All filters are recomputed on `prepareToPlay` for any rate.
- Internal precision: filters below 200 Hz run in double precision (sub-bass biquads in 32-bit float are noisy). Everything else may use float.
- All gain and frequency parameters are smoothed (`juce::SmoothedValue`, 20 to 50 ms). Bypass uses a 10 ms crossfade. No zipper noise or clicks under automation.
- Oversampling with `juce::dsp::Oversampling` for the clipper, saturation and limiter. Global Quality parameter: Normal (2x), High (4x), Auto (2x in real time, 4x when `isNonRealtime()`).
- **Latency.** Report the real total through `setLatencySamples` and call `updateHostDisplay` when it changes (linear-phase mode, limiter look-ahead, oversampling, convolution). Every bypassed or parallel path is delay-compensated so that bypass and A/B never shift timing.
- Guard against NaN and Inf at the input: output silence for that block and reset filter states.

**State and files**

- Parameters in `AudioProcessorValueTreeState` with stable IDs (section 9). Non-parameter state (reference file paths, album project path, correction profile paths) in the same `ValueTree`, saved in the host project.
- User data in `%APPDATA%\MetalBus\`: `settings.json`, `presets\`, `refcache\` (analysis cache of reference files), `corrections\`, `translation\` (system emulation presets), `albums\` (default album folder).
- File dialogs are asynchronous (`juce::FileChooser::launchAsync`). Bad or unreadable files must produce a clear message, never a crash.

## 4. Signal flow and processing modules

The processing chain has a fixed order, ten modules, each with a latency-compensated bypass and its own meter; analysis taps read before and after it, and monitoring comes after the last tap.

```text
PROCESSING (changes the audio, goes into the render)
  Input + HPF > Dynamic EQ > Multiband > Low-End Mgr > Bus Comp
  > Saturation > Stereo > Clipper > Limiter > Output

EYES (measure only, never change the audio)
  Analysis engine reads the IN tap (before Input) and the OUT tap (after Output)

MONITORING (listen-only, bypassed when rendering), fed from the OUT tap
  Reference A/B > Monitor correction > Translation check > Listening tools
```

The IN tap (before Input) and the OUT tap (after Output, before monitoring) are read-only copies of the signal: both feed the Eyes lane, and the OUT tap also feeds the monitoring lane.

Defaults are conservative: only Input, Output and metering are active in the default state. Ranges below are the allowed parameter ranges; values in brackets are defaults.

**4.1 Input and subsonic filter**

- Input trim -24 to +24 dB [0], polarity invert (L, R), DC blocker (first order, 5 Hz, always on).
- Subsonic high-pass: 15 to 60 Hz [25], slope 12, 24, 48 dB/oct [24]. Removes energy nobody hears but which eats limiter headroom (8 and 9 string tunings plus kick produce a lot of it). Its curve is drawn on the spectrum.

**4.2 Dynamic EQ**

- 10 bands. Band 1 and band 10: low and high shelf or cut (6 to 48 dB/oct). Bands 2 to 9: bell, shelf, notch.
- Per band: frequency 20 Hz to 20 kHz, gain -18 to +18 dB, Q 0.1 to 18, placement Stereo, Mid, Side, Left or Right.
- Per band dynamics (on/off): threshold -60 to 0 dB, range 0 to 18 dB, mode compress or expand, attack 0.1 to 100 ms, release 5 to 1000 ms.
- Phase mode (global for EQ and multiband): minimum phase [default] or linear phase (reports its latency). Draggable band nodes on the spectrum view. EQ curve accuracy against an RBJ biquad reference within 0.05 dB.
- Intended jobs: mud at 150 to 400 Hz, box at 400 to 600 Hz, guitar fizz at 4 to 8 kHz, low-end boom on one specific note (see the note-name readout in section 5).
- *Resonance suppressor* (optional, M7): STFT-based dynamic cut with range 150 Hz to 12 kHz, depth 0 to 20 dB, sharpness, speed, mix and delta listen. Its latency is reported.

**4.3 Multiband dynamics**

- 5 bands, 4 crossovers [80, 250, 1500, 5500 Hz], each 30 Hz to 16 kHz. Crossover type: Linkwitz-Riley 4th order [default; the bands must sum flat within 0.01 dB] or linear-phase FIR (latency reported).
- Per band: threshold -60 to 0 dB, ratio 1:1 to 10:1 [1.5:1], attack 0.5 to 200 ms, release 10 to 1000 ms or Auto, knee 0 to 12 dB, makeup -12 to +12 dB, mix 0 to 100 percent, stereo link 0 to 100 percent, processing mode Stereo or Mid/Side, solo and bypass, band gain trim.
- The lowest band (up to 80 Hz) is where the 8/9 string and kick energy meet. Its gain-reduction meter is always visible.

**4.4 Low-End Manager**

- *Mono maker*: frequency 40 to 300 Hz [120], slope 12, 24, 48 dB/oct, amount 0 to 100 percent. Removes the side signal below the frequency, complementary to the crossover so nothing else changes.
- *Phase rotator*: all-pass cascade, frequency 30 to 200 Hz, rotation 0 to 360 degrees. Reduces the asymmetry of low-end waveform peaks to win headroom. A before/after waveform of the 20 to 150 Hz band is shown (section 5).
- *Low-End Translator* (sub-harmonic enhancer): band-pass 30 to 90 Hz, oversampled waveshaper that adds 2nd and 3rd harmonics, amount 0 to 100 percent, 2nd/3rd mix, level-dependent so it does not bloat loud passages. Purpose: make the low notes audible on laptops, phones and small speakers by adding harmonics that those devices can reproduce.
- Low-band solo listen (monitoring, section 6).

**4.5 Bus compressor**

- Models: VCA (clean glue) and Punch (faster, more bite). Threshold, ratio 1.5, 2, 3, 4, 6, 10, attack 0.1 to 30 ms, release 50 ms to 1.2 s or Auto, soft or hard knee, sidechain high-pass 20 to 300 Hz [100] so kick and low guitars do not pump the mix, mix 0 to 100 percent (parallel), peak/RMS detection blend, stereo link, auto makeup. Typical use is 1 to 2 dB of gain reduction. Gain-reduction meter with history.

**4.6 Saturation**

- 3 bands [crossovers 150 Hz and 3 kHz]. Per band: drive 0 to 24 dB, type Tube, Tape, Transformer or Diode, asymmetry (bias), mix, output. DC blocking after asymmetric stages. Oversampled. Low-band saturation adds harmonics that help small-speaker translation; high-band saturation must stay gentle because it adds to fizz.

**4.7 Stereo**

- Width per band: low [locked mono below the mono-maker frequency], mid 0 to 200 percent [100], high 0 to 200 percent [100]. Mid/side balance -6 to +6 dB.
- *Correlation guard*: when the correlation of a band falls under a threshold, width in that band is reduced automatically. The guard is optional and its action is visible on the meter.

**4.8 Clipper**

- Oversampled at least 4x. Types: Hard, Soft (tanh), Soft knee (polynomial), Diode (asymmetric). Threshold -12 to 0 dBFS, shape, mix 0 to 100 percent (parallel), *Low-band protection* (the band below about 150 Hz is clipped less to avoid distorting the sub).
- Shaves the snare and kick peaks before the limiter, so the limiter does not have to flatten the drums. Meter: amount of peak reduction in dB. Default Off; typical use is 0 to 4 dB of peak reduction.

**4.9 Limiter**

- Look-ahead 0.5 to 5 ms [2], release 10 to 500 ms or Auto, two stages (a fast transient stage and a slower program-dependent stage), modes Transparent, Balanced, Aggressive, input drive 0 to 24 dB, ceiling -3 to 0 dBTP [-1.0], stereo link 0 to 100 percent [100], transient preservation 0 to 100 percent.
- True-peak safe: detection at 4x oversampling plus a final safety stage. **Acceptance test:** with +24 dB drive on pink noise, a sine sweep and a synthetic kick, the true peak measured by an independent 8x oversampled meter stays at or below ceiling + 0.05 dB.
- Meters: gain reduction, output true peak, and a gain-reduction histogram (section 5).

**4.10 Output**

- Output gain -12 to +12 dB. Dither Off, TPDF or noise-shaped, target 16 or 24 bit [Off]; dither is the last step of the processing chain.
- Loudness-matched A/B lives in monitoring (section 6), not here, because it must never be baked into a render.

## 5. Analysis modules (the "Eyes")

The analysis modules measure the signal at an input tap and an output tap without ever changing audio; they are the main replacement for the user's ears and come first in the milestones.

**Common rules**

- Every view can show the input tap (before the chain), the output tap (after the chain, before monitoring) or both overlaid.
- Analysis runs on the background thread (section 3). Values are time-smoothed; accumulated (integrated) values reset on a Reset button and, optionally, when the host transport starts from the beginning (use `AudioPlayHead`).
- Every value has a unit and a numeric readout. Traffic-light colours (green, yellow, red) use thresholds from `settings.json`; defaults are green within 1.0 dB of target, yellow within 2.5 dB, red beyond.

**5.1 Spectrum analyzer**

- FFT 4096, 8192 or 16384 points [8192], Blackman-Harris window, 75 percent overlap. Smoothing: none, 1/12, 1/6, 1/3, 1 octave [1/3]. Tilt 0, 3 or 4.5 dB/oct [4.5, so pink noise looks flat]. Channels: L, R, Mid, Side, Stereo.
- Modes: real time with peak decay, average since reset (energy average), peak hold.
- Overlays: reference curve, reference corridor, EQ curve, delta view (our curve minus the reference target).
- Cursor readout: frequency, level and the **nearest musical note** (A4 = 440 Hz) for 20 to 500 Hz. This lets the user see which guitar or bass note is booming.

**5.2 Tonal balance (6 bands)**

| Band | Range |
| --- | --- |
| Sub | 20 to 60 Hz |
| Bass | 60 to 150 Hz |
| Low-mid | 150 to 500 Hz |
| Mid | 500 Hz to 2 kHz |
| Presence | 2 to 6 kHz |
| Air | 6 to 16 kHz |

- Band energy is normalised by the total energy from 20 Hz to 16 kHz, so a track is compared with a reference regardless of loudness. Display: one bar per band with the target marker, the corridor, the value in dB relative to the target and a traffic light.
- Time basis: integrated since reset [default] or the last 10 seconds.
- This is the main indicator for the bass and the highs. The sentence under it reads like "Sub +2.4 dB vs target".

**5.3 Loudness and dynamics**

- Momentary (400 ms), short-term (3 s), integrated (gated), loudness range (LRA) per ITU-R BS.1770 and EBU R128 / Tech 3342. K-weighting filter coefficients are computed for any sample rate. Gating: absolute -70 LUFS, relative -10 LU.
- True peak with 4x oversampling per BS.1770 Annex 2, sample peak, crest factor, PLR (maximum true peak minus integrated loudness), PSR (maximum true peak minus short-term loudness), counters for sample overs and true-peak overs above the ceiling.
- Loudness history graph of short-term loudness with target lines. Target presets: Streaming reference (-14 LUFS integrated, -1 dBTP) and Custom. These lines are informational only. Modern metal masters are often louder than streaming reference levels and the plugin must not impose a target.

**5.4 Low-end analyzer**

- Spectrum 20 to 200 Hz at 1/12 octave resolution or finer (analysis of a decimated low-band stream), channels L, R, Mid, Side, with the note-name readout.
- Scrolling waveform of the 20 to 150 Hz band (2 s window) with a peak-asymmetry readout.
- Low-band phase correlation (-1 to +1) and mono loss in dB for the band below the mono-maker frequency.
- Sub-to-bass ratio (energy 20 to 60 Hz versus 60 to 120 Hz) compared with the reference.

**5.5 Stereo**

- Vectorscope (goniometer, mid/side rotated) with decay. Broadband correlation meter and correlation per band (below 150 Hz, 150 to 500 Hz, 500 Hz to 4 kHz, above 4 kHz). Width per band (side/mid energy ratio in percent). Left/right balance.
- **Mono compatibility:** the loss in dB when the signal is summed to mono, per band. A large loss in a band means phase problems that will disappear on mono phones and club systems.

**5.6 Spectrogram**

- Time-frequency picture, 20 Hz to 20 kHz log axis, FFT 4096, window 1 to 60 s, perceptually uniform colour palette, freeze, click to read frequency, time and level. Used to find resonances that persist over time.

**5.7 Harshness meter**

- Energy 3 to 8 kHz relative to 500 Hz to 2 kHz in 1/3 octave detail, shown against the reference and as a 30 second history. Targets high-gain fizz and cymbal harshness, which the user cannot hear reliably on the DT 990 headphones.

**5.8 Dynamics per band**

- Crest factor (peak minus RMS) in the 6 tonal bands compared with the reference, to show over-compression of the low end or the highs.

**5.9 Overs and limiter damage**

- Counters for sample overs and inter-sample overs before the limiter. A gain-reduction histogram for clipper and limiter over the last N seconds or the whole song, with cumulative percentages above 3 and 6 dB of reduction. It shows how often the limiter is flattening the drums.

**5.10 Checks summary (one-glance panel)**

One panel combines the verdicts into a traffic-light list: six tonal bands against target, mono loss, low-band correlation, PLR, harshness, true peak against ceiling, integrated loudness against target. Every light has a plain-language tooltip. It is a list of independent checks, not a single "score".

## 6. Monitoring and translation modules

Monitoring modules change only what the user hears, never what is rendered, and the safety rules below take priority over every convenience feature.

**6.0 Safety rules (non-negotiable)**

1. Monitoring sits after the output tap and after the last processing module. Analysis taps always read before monitoring.
2. When `AudioProcessor::isNonRealtime()` is true (offline render or export), the whole monitoring section is bypassed.
3. While any monitoring feature is active, a red **MONITOR ON** banner is visible at all times, because a live bounce in the host is not necessarily offline.
4. The monitoring state is **not saved** in the host project: the plugin always loads with monitoring Off.
5. **Acceptance test:** with every monitoring feature enabled and `isNonRealtime()` set, the output is bit-identical to the output with monitoring disabled.

The monitoring section contains correction, system emulation, listening tools, reference playback and loudness-matched A/B (the last two are described in section 7 and in 6.4).

**6.1 Monitor correction**

- Selector: Off, Headphones, Speakers. Each of the two slots holds one correction profile.
- Profile sources: (a) a parametric EQ text file in the common AutoEQ / REW export format (lines such as `Filter 1: ON PK Fc 105 Hz Gain -3.2 dB Q 1.41` and a `Preamp:` line); (b) an impulse response WAV, either minimum-phase or linear-phase, played through `juce::dsp::Convolution` with the latency reported.
- Controls: correction strength 0 to 100 percent (scales the gains), automatic preamp so the correction never clips, and a **boost limit** [+3 dB per filter]. Boosting a room-mode null does not work, so below 300 Hz the default behaviour is cut-only.
- No measurement data is bundled in the plugin. The default profile of each slot is flat. The user supplies the DT 990 profile (a per-model average, not a measurement of his unit). The Edifier R1280T slot only becomes trustworthy after the user imports a measurement of his own room (see section 12). The UI must say this next to the slot.

**6.2 Translation check (system emulation)**

A selectable filter chain applied after correction. Presets (all editable as JSON in `%APPDATA%\MetalBus\translation\`, and all clearly labelled as rough approximations, not measurements):

| Preset | Starting point |
| --- | --- |
| Laptop speaker | High-pass about 180 Hz (24 dB/oct), low-pass about 12 kHz, small presence peak, mono |
| Phone speaker | High-pass about 300 Hz, low-pass about 9 kHz, mono |
| Bluetooth speaker | High-pass about 90 Hz, bump of about +4 dB near 120 Hz, low-pass about 14 kHz, soft limiter |
| Earbuds | Gentle bass shelf and treble roll-off |
| Car | Bass shelf of about +5 dB below 80 Hz, dip around 2 to 3 kHz, low-pass about 12 kHz |
| Small bookshelf | High-pass about 60 Hz, otherwise flat |

The purpose is diagnostic: if the main riff disappears on the Laptop preset, the low notes lack harmonics (see the Low-End Translator in 4.4).

**6.3 Listening tools**

- Mono, Mid only, Side only, Left only, Right only.
- *Band listen*: solo any of the 6 tonal bands (steep Linkwitz-Riley filters) to hear what lives in it.
- *Delta listen*: output minus input, latency-aligned, to hear exactly what the chain adds or removes.

**6.4 Loudness-matched A/B (bypass)**

- A button switches the whole processing chain on and off. While it is engaged with Match on, the level of the bypassed signal is adjusted so that both versions have the same short-term loudness (K-weighted, 3 s, slow smoothing, limited to +/-12 dB). The applied compensation is shown in dB. Louder always sounds better, so an unmatched A/B misleads.

## 7. Reference A/B and album consistency

References define what "translating well" looks like, and the album passport table shows how far each track of the album is from one shared target.

**7.1 Reference slots**

- Up to 6 slots. Files by drag and drop or file chooser: WAV, FLAC, MP3, OGG. When a file is loaded, a background job analyzes the **whole file** and stores integrated loudness, LRA, PLR, average spectrum (1/12 octave), 6-band tonal balance, per-band crest factor, stereo width and correlation per band and low-end metrics. Results are cached in `%APPDATA%\MetalBus\refcache\` keyed by path, size and modification time. A 5-minute file must be analyzed in under 5 seconds on a normal desktop CPU.
- *Playback.* A small built-in player (play, pause, seek, loop region) replaces the host signal at the monitoring stage while a reference is selected. Reference playback is a monitoring feature and follows all rules of 6.0, so it can never reach a render.
- *Loudness matching.* Reference playback gain = loudness of our signal minus loudness of the reference. Modes: Auto (short-term for the current section, integrated for the whole track) and Manual. Level differences between A and B must stay within 0.5 LU in Auto mode.

**7.2 Reference corridor and delta view**

- The user picks a subset of the loaded references as the **reference set**. Target curve = the average of their normalised spectra and tonal balances. Corridor = 10th to 90th percentile across the set when it holds 3 or more tracks, otherwise a fixed +/-1.5 dB. The corridor is drawn on the spectrum and on every tonal-balance bar.
- The delta view shows our average spectrum minus the target, per frequency, with the corridor as a band around zero.

**7.3 Match EQ (suggestion, not automatic)**

- From the smoothed difference between our average spectrum and the target, propose up to 8 bell or shelf filters (gain limited to +/-3 dB, Q 0.5 to 2, within 30 Hz to 16 kHz). Buttons: Apply to Dynamic EQ and Undo. A strength slider scales the proposal from 0 to 100 percent. Nothing is applied without the user pressing Apply.

**7.4 Album passport and album table**

- **Passport.** After a full pass of a track, the user presses Store Passport and enters or confirms the track name. It saves: integrated LUFS, LRA, PLR, maximum true peak, average spectrum (1/6 octave), 6-band tonal balance, per-band crest, width and correlation per band, harshness index, date and plugin version.
- **Album project file** (`.metalbus-album`, JSON) in a folder the user chooses via New Album or Open Album. Writes are atomic (write a temporary file, then rename). When another instance changes the file, reload it by modification time.
- **Album view.** A table with one row per track: LUFS-I, LRA, PLR, and the six band values in dB, width, harshness. Each cell shows its deviation from the album target using the same traffic-light colours. Plain-language hints under the table, for example "Track 5: Bass +2.3 dB, Air -1.1 dB vs album target". An overlay of all track spectra with the current track highlighted, and a loudness bar chart in track order so jumps between neighbours are visible.
- **Album target** can be one of: the mean of all passports [default when no reference set exists], the current reference set, or a single **anchor track** the user marks as the best-sounding one.
- **Chain snapshots.** The full processing parameter set can be stored under a track name inside the album file and recalled later in a new session.

**7.5 Offline album analyzer (M7, optional)**

- A small standalone application (`juce_add_gui_app`) that links `metalbus_core`. The user drops the bounced WAV files of the whole album; it computes the passports faster than real time and fills the same album file. This avoids playing each track in real time inside the DAW.

## 8. UI and UX

The interface is a single resizable dark window that shows the big picture (spectrum and checks) first and the module controls underneath, so the user can read the state of the master at a glance.

- **Window.** Resizable to any size with fluid scaling, a 100% / 120% size button and a remembered size (details in the block after this list). Dark theme, colour-blind-safe palette (blue, orange, yellow, magenta plus clear lightness steps), minimum text size 12 px. Software rendering must stay smooth at 30 fps for analyzers: cache `Path` objects and redraw only changed regions.
- **Top bar.** Preset selector with the preset buttons (section 9); size button 100% / 120%; Chain bypass with the loudness-match toggle (6.4); Monitor selector (Off, Headphones, Speakers) with the system-emulation dropdown; reference buttons R1 to R6 with an A/B switch; compact global meters (short-term LUFS, true peak, PLR). The red **MONITOR ON** banner (6.0) lives here.
- **Tabs.**
  - *Master*: large spectrum with the EQ curve and draggable bands on top, the horizontally scrollable module strip below.
  - *Eyes*: dashboard with tonal balance, low-end panel, stereo panel, loudness panel and the checks summary (5.10) all visible together.
  - *Low-End*: the full low-end analyzer (5.4).
  - *Stereo*: vectorscope, correlation and width (5.5).
  - *Spectrogram* (5.6).
  - *Album*: passport table and overlays (7.4).
- **Module strip.** Each module has a header with a bypass LED, its name and its key controls. Controls: mouse wheel and Shift for fine adjustment, double-click to reset to default, right-click or double-click on the value to type a number.
- **Readouts.** Every parameter shows its value with a unit. Every graph has a numeric readout. Every traffic light has a tooltip in plain language.
- **Footer.** Version `0.<milestone>.<build>`, git commit hash, CPU load, reported latency in samples and milliseconds.
- **Localisation.** All user-visible strings go through a string table. The default is English; a Russian table is added in M7 with a language switch in the options menu. Technical terms keep their English form where Russian audio engineers use them (LUFS, true peak, mid/side).
- **No blocking.** The UI never blocks the audio thread. Long operations (file analysis, album recalculation) show a progress indicator and can be cancelled.

**Window size, scaling and the size button**

- **Any size.** The window is resizable by dragging any edge or corner, with no locked aspect ratio, from a minimum of 480 x 300 up to 4K and beyond (the screen is the only upper limit). It works in REAPER in floating and docked FX windows.
- **Fluid scaling.** The reference size is 1280 x 800, which is 100%. Fonts, controls, line widths, meters and graphs are drawn as vectors and scale continuously with the window. Nothing is a stretched bitmap, and any image asset is supplied at 2x or more. Panels fill the whole window without letterbox bars; the layout flexes (JUCE `Grid` or `FlexBox`).
- **Responsive layout.** Three layout modes, each with its own reference size, are chosen by window size: Full from 1000 x 640, Medium from 720 x 460, Compact below that. Medium collapses secondary panels into tabs. Compact shows the spectrum and the checks panel, with other views one tab away. Within a mode the scale factor is the smaller of width and height relative to the mode's reference size, clamped to 0.8 to 4, and text is never smaller than 11 px on screen. At any size no text or control may overlap, be cut off or leave the window. The thresholds are starting values and are confirmed in the approved mockups.
- **Size button.** One button in the top bar, labelled with the current preset: **100%** is 1280 x 800 and **120%** is 1536 x 960. A click toggles between them and the whole interface scales with the window. If the window was dragged to another size, the first click returns to 100%.
- **Size memory.** The plugin remembers the size the window had when it was last used. The size is saved in `%APPDATA%\MetalBus\settings.json` when the user stops resizing (debounced) and when the editor closes. Every newly opened plugin window, in any instance and any project, opens at that size. On load the size is clamped to the visible work area of the current monitors so the window never opens off-screen. The size is a global user setting and is not stored in host projects.
- **High DPI.** Per-monitor DPI awareness, crisp at Windows scaling from 100 to 300 percent.

## 9. Presets, state and persistence

Parameter IDs are stable lowercase identifiers with a module prefix, the default state is transparent, and everything the user sets survives a project reload except the monitoring state.

- **Parameter IDs.** `snake_case` with a module prefix, for example `in_trim`, `hpf_freq`, `eq_b3_freq`, `eq_b3_gain`, `mb_b1_thr`, `mb_xo1_freq`, `lem_mono_freq`, `bus_thr`, `sat_b2_drive`, `clip_thr`, `lim_ceiling`, `out_gain`. Use `juce::ParameterID` with a version hint. IDs are frozen after M1; any rename needs a migration.
- **Automation.** Everything continuous is automatable. Structural switches that rebuild the signal path (oversampling quality, linear-phase mode, crossover type) are exposed as parameters but marked non-automatable, and changing them updates the reported latency.
- **Default state.** All processing modules bypassed, all gains at 0 dB, quality Auto. Null test: output equals input bit-exactly at the default state.
- **Presets.** JSON files (.mbpreset, see the preset management block below) in `%APPDATA%\MetalBus\presets\` (user) plus built-in factory presets. Factory presets are conservative starting points, not finished masters: Init (flat), Tight Modern Metal, Wide and Loud, Djent Low-End Focus, Gentle Glue, Streaming Safe. Their values are tuned with the user after M5.
- **Host project state** (`getStateInformation`): all parameters, reference file paths (not audio), the reference set, the album project path, correction profile paths, the current preset name with its modified flag (the window size is global and not stored per project, see section 8). Not saved: monitoring on/off, reference playback position.
- **Global settings** in `%APPDATA%\MetalBus\settings.json`: last window size, size preset (100% or 120%), language, traffic-light thresholds, loudness target presets, analysis defaults.
- **Analysis reset** on transport start is a setting [on].

**Preset management (create, edit, delete)**

- **Preset bar** in the top bar: previous and next arrows, the preset name with a `*` while it has unsaved changes, and a dropdown list grouped as Factory and User (subfolders act as categories).
- **Buttons.** *Save* overwrites the current user preset. *Save As* creates a new one and asks before overwriting an existing name. *Rename* and *Delete* work on user presets; Delete asks for confirmation and removes the file. *Import* and *Export* handle single `.mbpreset` files, and dropping a `.mbpreset` onto the window imports it.
- **Edit** means: load any preset, change parameters, then Save (overwrite) or Save As (new copy). Factory presets are read-only: they cannot be overwritten, renamed or deleted, but they can be copied with Save As. Save on a factory preset opens Save As.
- **Deleting the loaded preset** keeps the current sound and marks it as unsaved.
- **File format.** JSON with `formatVersion`, the plugin version and a map of parameter ID to value. Unknown IDs are ignored and missing IDs take their defaults, so presets stay compatible between versions. A preset holds processing and analysis settings (targets, thresholds), but not references, albums or monitoring state.
- **Names** are 1 to 64 characters; characters that Windows file names forbid (`\ / : * ? " < > |`) are rejected with a message. Writes are atomic (temporary file, then rename). The list refreshes whenever it is opened.
- **Missing preset file.** If a project refers to a preset that no longer exists, the stored parameter values are used and the name is shown with a "missing" marker.
- **Tests.** Save, load, rename and delete round-trip with identical parameter state. A corrupt preset file produces an error message and never a crash.

## 10. Quality, performance and testing

Every DSP and metering claim in this spec must be backed by an automated test in CI, and anything that can only be judged by ear goes into the Russian REAPER checklist of that milestone.

**Automated tests (Catch2 through CTest, run on every push)**

- **Transparency.** Default state: output equals input bit-exactly. With all modules bypassed, again bit-exact, and the delay of every bypass path equals the reported latency.
- **Latency.** An impulse comes out exactly at the reported latency in every mode (linear phase on or off, oversampling 2x and 4x, limiter look-ahead, convolution).
- **Filters.** EQ response within 0.05 dB of an RBJ biquad reference. Crossover bands sum flat within 0.01 dB. Mono maker leaves signals above its frequency unchanged.
- **Loudness.** A stereo 1 kHz sine at -23 dBFS peak per channel reads -23.0 LUFS +/-0.1 LU as momentary, short-term and integrated. 10 s at -80 dBFS followed by 20 s at -23 dBFS reads -23.0 LUFS integrated +/-0.1 LU (the part below the absolute gate is excluded).
- **True peak.** A sine at one quarter of the sample rate with a 45 degree phase has a sample peak of -3 dBFS and a true peak within 0.2 dB of 0 dBFS.
- **Limiter.** The ceiling test from 4.9.
- **Monitoring safety.** The bit-exact test from 6.0.
- **Robustness.** Sample rates 44.1, 48, 88.2, 96 and 192 kHz. Block sizes 1, 32, 64, 512, 2048 and 8192, plus randomly varying block sizes. NaN and Inf input produces silence without a crash. No allocation on the audio thread (verified in a debug build with an allocation hook).
- **Automation.** Sweeping every automatable parameter produces no sample-to-sample jump above a defined threshold (no clicks).
- **pluginval** at strictness level 5 or higher in CI.

**UI and preset tests**

- **Window size memory.** Saving and loading the last window size round-trips, and a size larger than the monitor is clamped to the visible work area.
- **Layout at any size.** A test harness renders the editor at 480 x 300, 720 x 460, 1000 x 640, 1280 x 800, 1536 x 960 and 3840 x 2160. It fails if any control leaves the window or overlaps a neighbouring control.
- **Presets.** The round-trip and corrupt-file tests of section 9.
- **Manual checks in REAPER for every UI milestone** (Russian checklist): drag-resize from the smallest to a very large size, press the size button twice, close and reopen the window, open a second instance, and compare the result with the approved mockups.

**Performance targets (initial; refine after profiling)**

- Full chain plus analysis at 48 kHz with a 512-sample buffer: at most 12 percent of one core in Normal quality and at most 25 percent in High (4x). UI drawing is not counted.
- Memory use under 300 MB with six references loaded.
- Report the measured numbers to the user at the end of M4 and M5.

**Code quality**

- `clang-format`, compiler warnings enabled (`/W4`) and treated as errors for new code, small functions, no raw owning pointers. DSP classes have a `prepare`, `process`, `reset` interface and are testable without the GUI.
- Unreadable or corrupt user files (references, correction profiles, album files, presets) produce a visible error message and never a crash or a hang.
- Manual QA host: REAPER 64-bit on Windows 10. A Russian checklist for each milestone is part of the delivery.

## 11. Milestones and acceptance criteria

The order puts the measuring tools first, because they give the user usable eyes immediately, and the heavy DSP last.

| ID | Scope | Acceptance criteria |
| --- | --- | --- |
| M0 | Skeleton and CI: JUCE and CMake project, GitHub Actions workflow, empty stereo passthrough VST3 delivered as one `.vst3` file, plain placeholder window with free resizing, the 100% / 120% size button and remembered size, version and commit hash in the footer, `README_RU.md`, `docs/SPEC.md`, `docs/DECISIONS.md` | CI is green and the zip artifact downloads and contains the single `.vst3` file. REAPER loads it as a stereo effect. Passthrough is bit-exact. pluginval passes at level 5. The window resizes freely, the size button toggles 100% and 120%, and the last size is restored after reopening and in a new instance |
| M1 | Mockups of the main window (top bar, Master and Eyes tabs) at three sizes, approved by the user before any UI code. Then the Eyes core: spectrum (5.1), tonal balance (5.2), loudness and true peak (5.3), stereo (5.5), reset on transport start, Eyes tab with the checks panel (5.10) | All loudness and true-peak tests of section 10 pass. On a real track the loudness agrees within 0.3 LU with an independent meter chosen by the user. The UI matches the approved mockups. Parameter IDs frozen |
| M2 | Reference and monitoring: reference slots with cache (7.1), reference set, corridor and delta view (7.2), monitor correction (6.1), system emulations (6.2), listening tools (6.3), loudness-matched A/B (6.4), safety rules (6.0) | A 5-minute reference is analyzed in under 5 s. A/B level difference at most 0.5 LU in Auto mode. The bit-exact monitoring safety test passes. MONITOR ON banner works |
| M3 | Core processing: input and subsonic filter (4.1), dynamic EQ (4.2), Low-End Manager (4.4), output and dither (4.10), preset manager with create, edit, rename and delete (section 9), Low-End tab (5.4), harshness meter (5.7), spectrogram (5.6) | Filter accuracy and latency tests pass. Default state stays bit-exact. Mono maker and phase rotator verified on the low-band waveform view. Presets can be saved, loaded, renamed and deleted and survive a restart |
| M4 | Dynamics: multiband (4.3), bus compressor (4.5), saturation (4.6), stereo (4.7), per-band dynamics view (5.8) | Crossover sum test passes. No clicks under automation. CPU targets reported to the user |
| M5 | Loudness stage: clipper (4.8), limiter (4.9), overs and limiter-damage view (5.9) | Limiter ceiling test passes. Latency is reported exactly. CPU targets for High quality reported. The user checks drum transients by ear in REAPER and with the numeric GR histogram |
| M6 | Album consistency: passports, album file, album view, anchor track, chain snapshots (7.4), Match EQ suggestion (7.3) | A passport measured in the plugin agrees with an offline analysis of the same bounced file within 0.2 LU and 0.2 dB per band. The album file survives reopening and a concurrent write |
| M7 | Polish: factory presets, Russian localisation, resonance suppressor (4.2), offline album analyzer (7.5), documentation | The user can run the full album workflow end to end. Presets load without errors. Russian strings complete |

Every milestone that adds or changes UI starts with a mockup for approval (section 0), and no UI code is written before the user approves it.

## 12. Out of scope and open questions

Version 1 is a Windows stereo VST3 only, and every open question below has a default so the agent never has to stop and wait.

**Out of scope for v1**

- macOS, Linux, AU, AAX and CLAP.
- Measuring the room inside the plugin (sweep plus microphone). Recommendation for the user, outside the build: a calibrated USB measurement microphone and REW, then export parametric filters or an impulse response and import them in 6.1.
- Surround, immersive and mono bus layouts.
- Automatic "AI mastering".
- MP3 or AAC encode simulation (codec preview).
- Sample-accurate reference playback locked to the DAW transport.
- Licensing, copy protection, online features and telemetry.

**Open questions and their defaults**

| Question | Default decision |
| --- | --- |
| Plugin name | MetalBus (working title), kept in one CMake variable so it can be changed later |
| UI language | English, with a Russian string table added in M7 |
| Minimum window size and layout modes | Minimum 480 x 300. Layout modes switch at 1000 x 640 and 720 x 460, confirmed in the approved mockups |
| Size button from a custom size | The first click returns to 100%, the next goes to 120% |
| Single-file VST3 | Try the flat single `.vst3` first; if REAPER or pluginval rejects it, ship the standard bundle and tell the user |
| Mockup format | HTML or SVG rendered to PNG, saved in `docs/mockups/` |
| Standalone build | Not built unless the agent needs it for debugging |
| Reference slots and files | 6 slots. The user supplies the files. No commercial audio in the repository |
| Loudness target lines | -14 LUFS integrated and -1 dBTP, informational only |
| Oversampling default | Auto (2x real time, 4x offline) |
| EQ and multiband phase mode | Minimum phase. Linear phase is optional because of pre-ringing on drums |
| JUCE version and licence | Latest stable 8.x at M0, pinned; licence terms recorded in `docs/DECISIONS.md` |
| Resonance suppressor | Built in M7 only if the CPU budget allows |
| Album file location | Chosen by the user; default `%APPDATA%\MetalBus\albums\` |
