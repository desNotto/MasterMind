# Decisions and deviations from the spec

Every deviation from `docs/SPEC.md` and every default chosen for an open question is recorded here with its reason.

## D-001 Product name: MasterMind (2026-11-26, M0)

The user named the plugin **MasterMind**; "MetalBus" in the spec was a working title. The name lives in one CMake variable (`MM_PRODUCT_NAME`). Everywhere the spec says MetalBus, read MasterMind:

| Spec | Actual |
| --- | --- |
| `MetalBus.vst3` | `MasterMind.vst3` |
| `MetalBus-<version>-win64-vst3.zip` | `MasterMind-<version>-win64-vst3.zip` |
| `%APPDATA%\MetalBus\` | `%APPDATA%\MasterMind\` |
| `metalbus_core` | `mastermind_core` |
| `.metalbus-album` | `.mastermind-album` (M6) |
| `.mbpreset` | `.mbpreset` (kept, M3) |

## D-002 Vendor identity (2026-11-26, M0)

- Company (author): `desNotto`. Repository: https://github.com/desNotto/MasterMind
- Bundle ID `com.desnotto.mastermind`, manufacturer code `Dnto`, plugin code `Mmnd`, VST3 categories `Fx|Mastering`.
- `JUCE_VST3_CAN_REPLACE_VST2=0` (there is no VST2 version to replace).
- These values form the VST3 class ID that hosts store in projects. They must never change, or saved REAPER projects lose the plugin.

## D-003 JUCE version and licence (2026-11-26, M0)

- JUCE **8.0.17-security** (the latest 8.x release at M0; the spec asks for 8.x, JUCE 9 exists but is not used). Pinned by tag via `FetchContent`.
- Catch2 **v3.16.1** for unit tests. pluginval **v1.0.4** (latest release) in CI.
- JUCE 8 is dual-licensed: AGPLv3 or a commercial JUCE licence (the free Starter tier covers small revenue). Personal use is assumed. If the plugin is ever distributed, either publish the source under AGPLv3 or obtain a JUCE licence. Catch2 (BSL-1.0) and pluginval (GPLv3, CI tool only, not linked) do not affect the plugin.

## D-004 `mastermind_core` is a CMake INTERFACE (source) library (2026-11-26, M0)

The spec asks for a static library. JUCE modules are source modules compiled into every target that uses them; a static library holding JUCE code linked into a plugin that also compiles JUCE would duplicate symbols or mix module configurations. `mastermind_core` is therefore an INTERFACE library: its sources compile into the plugin and into the test executables. It links only `juce_core` and `juce_audio_basics` (later `juce_dsp`, `juce_audio_formats`); the unit test executable builds without any GUI module, which enforces the "no GUI dependency" rule.

## D-005 M0 placeholder window without a mockup (2026-11-26, M0)

The spec's M0 scope asks for a "plain placeholder window" and M1 starts with the mockups of the real interface. The placeholder is a technical stub (title, size button, size readout, footer) to test resizing, the size button and size memory; it is not the product's look and is replaced completely in M1 after mockup approval.

## D-006 Size button label at a custom size (2026-11-26, M0)

The button shows `100%` or `120%` when the window has exactly that size and `Custom` otherwise. A click from a custom size goes to 100% (spec section 12).

## D-007 Version and build number (2026-11-26, M0)

Version `0.<milestone>.<build>`, where `<build>` is the GitHub Actions run number (local builds use 0). The git hash is the first 7 characters of the commit SHA.

## D-008 Settings file format (2026-11-26, M0)

`%APPDATA%\MasterMind\settings.json`:

```json
{ "formatVersion": 1, "window": { "width": 1280, "height": 800, "sizePreset": "100%" } }
```

Unknown keys are preserved on write. Writes are atomic (temporary file, then rename). A missing or corrupt file means defaults. The stored size is clamped to the work area of the primary display when a window opens.

## D-009 Single-file VST3 (2026-11-26, M0)

CI copies the inner binary of the JUCE bundle to a flat `MasterMind.vst3`. CI proves it with pluginval (strictness 5) and with `MasterMindBinaryTest`, which loads the flat file as a VST3 host and checks bit-exact passthrough at 44.1 to 192 kHz and block sizes 1 to 8192. Whether REAPER accepts the flat file is item 1 of the M0 checklist.

## D-010 CPU load measurement (2026-11-26, M0)

`juce::AudioProcessLoadMeasurer` uses a (try-)lock, which the real-time rules forbid. `mm::dsp::LoadMeter` measures the block time with high-resolution ticks and publishes it through an atomic instead.

## D-011 Tooltips in Russian on every control (2026-11-26, M1, approved with mockup v4)

The user asked for a tooltip on every knob of the module strip that explains what the module and the knob do, written in Russian and following the ASD-STE100 writing rules. This deviates from the spec in two ways:

- The spec requires plain-language tooltips only for traffic lights (section 8). Knob tooltips are an addition at the user's request.
- The spec makes English the default UI language and adds Russian in M7. Knob tooltip text is Russian from the first build that has tooltips. The text lives in the string table under its own keys, so the M7 language switch can add English versions without code changes.

Writing rules applied to the Russian text: one idea per sentence, at most 20 words per sentence, active voice, present tense, the imperative for instructions, one term for one meaning (for example "низкие частоты", never mixed with synonyms), and a warning label ("Внимание") instead of a tip label ("Совет") where a setting can damage the sound. Technical terms stay in English (Hz, dB, Side, Bypassed). Each tooltip has four parts: module and knob name with the parameter range, what the module does, what the knob does, and one tip or warning. A bypassed module adds a status line first. The footer repeats the control gestures from section 8 (double-click resets, Shift for fine adjustment). Tooltips appear after 500 ms of hover and hide on mouse leave or mouse down.

Mockup: `docs/mockups/m1-main-window-v2-2026-11-26/` (static PNGs plus an interactive `mockup.html`).

Mockup v4 (2026-11-26) extends the same tooltips to every control the user can operate, not only knobs: preset controls, Bypass and Match, Monitor and System selectors, reference slots and A/B, the size button, all tabs, analysis controls, spectrum view controls, EQ nodes on the spectrum, band buttons, module headers (bypass LED), polarity buttons, mode switches and drop-down lists. Control tooltips use the parts "Что делает" and "Совет" or "Внимание", plus a gesture line where the control has one. Readouts and graphs are not controls and are not covered; traffic lights keep their own tooltips from section 8. Mockup: `docs/mockups/m1-main-window-v4-2026-11-26/`.

## D-012 Whole plugin in one pass (2026-11-26)

After M0 passed in REAPER and mockup v4 was approved, the user asked to build the whole plugin (M1 to M7) without stopping for REAPER tests or approvals between milestones, and to test the finished product once. Every milestone's automated tests still run in CI. The REAPER test happens once, with a single Russian checklist at the end. New tabs follow the visual language of the approved mockup v4 without separate mockups. M0 result: REAPER loads the flat single-file `.vst3` (D-009 confirmed).

## D-013 DC blocker only with the subsonic filter

The spec asks for an always-on DC blocker and also for a bit-exact default state. Both cannot hold at once. The 5 Hz DC blocker therefore runs together with the subsonic filter (`hpf_on`). Input trim at 0 dB and Output gain at 0 dB skip the multiplication, so the default state is bit-exact.

## D-014 Constant latency with latency-compensated bypass

Every module keeps its latency when it is bypassed (its bypass path is a delay line of the same length), and module on/off crossfades over 20 ms. The reported latency changes only with structural settings: Quality, Limiter lookahead (non-automatable), EQ linear phase and Multiband linear-phase crossovers. With the defaults it is 270 samples at 48 kHz (oversampling filters and the 2 ms limiter lookahead). The default state is a bit-exact copy of the input delayed by exactly that latency; the binary test checks this through the VST3 interface.

## D-015 True-peak interpolation

The BS.1770 example filter is replaced by Kaiser-windowed sinc interpolators that are centred on an input sample, so phase 0 reproduces the samples exactly. Meters use 4x with 16 taps per phase. The limiter uses 8x with 96 taps per phase, aims 0.15 dB below the ceiling and ends with a sample clamp at the ceiling. The acceptance test measures with a separate 8x, 256-tap meter and passes at ceiling + 0.05 dB for pink noise, a sine sweep and a synthetic kick with +24 dB drive.

## D-016 Default tonal target and spectrum tilt

Without references the tonal-balance target is a spectrum whose FFT-bin level falls 4.5 dB per octave, the same slope that the default spectrum tilt makes flat (5.1). With references the target and corridor come from the reference set (7.2). The spectrum shows smoothed band power converted to a density slope, so tilt 3 dB/oct makes pink noise flat and 4.5 dB/oct makes a typical mix flat, as the spec intends. Smoothing interpolates fractional FFT bins, which keeps the curve smooth below 100 Hz.

## D-017 Linear phase

Linear-phase EQ builds one FIR per routing (L/R and M/S combined into four kernels) from the static band curves; dynamic bands add their dynamic part with minimum-phase filters on top. Linear-phase multiband uses complementary FIR low-pass kernels whose differences form the bands, so the bands sum exactly to a delayed input. Kernels are built on a worker thread, never on the audio thread.

## D-018 Low-End Translator without oversampling

The translator shapes a 30 to 90 Hz band with 2nd- and 3rd-order Chebyshev polynomials. Its highest generated harmonic is 270 Hz, far below Nyquist, so oversampling would change nothing; it is omitted.

## D-019 Auto quality

Auto uses 2x/4x (saturation/clipper) in real time and 4x/8x offline. Both oversamplers are prepared and the shorter path is padded, so the latency is the same in real time and in an offline render.

## D-020 Font

The mockup names Inter with Segoe UI as fallback. The plugin uses Segoe UI (part of Windows), so no font file has to be embedded; the look and metrics are close to the approved mockup.

## D-021 Reference memory

Only the selected reference is decoded into memory for playback; analysis results of all slots come from the on-disk cache. Six loaded references stay well under the 300 MB target.

## D-022 Interface details beyond the mockup

The mockup shows the key controls of each module. Every other parameter was reachable through a module detail panel (double-click a module name) with generic controls and generic tooltips. Superseded by D-028: since mockup v9 every module editor shows all of its controls and the detail panel is gone. Also added: a Match EQ panel in the spectrum header, a reference player strip in the tab bar while B is active, a medium layout and a compact top bar for narrow windows, band listen buttons on the Low-End tab, and the harshness (5.7), dynamics per band (5.8) and limiter damage (5.9) panels under the spectrogram. Turning Rotate or Translator above zero switches that sub-feature on; touching a Release knob turns Auto release off.

## D-023 Localisation scope

The options menu (gear in the footer) switches English and Russian. Headings, status texts and explanations are translated. Control, module and tab names stay English because the approved Russian tooltips refer to them by these names, and the spec keeps technical terms in English.

## D-024 Reset on play and chain bypass

Analysis resets on every transport start while "Reset on play" is on (the user can switch it off to measure a song in parts). The chain Bypass with Match is a monitoring feature (6.4): it is never saved and never affects an offline render.

## D-025 Resonance suppressor (4.2, optional)

Implemented inside the Dynamic EQ module, after the ten bands, with new parameter IDs `rs_on`, `rs_low`, `rs_high`, `rs_depth`, `rs_sharp`, `rs_speed`, `rs_mix`, `rs_delta` (adding IDs keeps old projects compatible). STFT with a frame of 2048 samples at 44.1/48 kHz (4096 up to 96 kHz, 8192 above), Hann windows, 75 % overlap, linked L/R gains. A bin is cut when it rises above the log-spectrum averaged over a fraction of an octave (Sharpness sets width and threshold), limited by Depth, smoothed by Speed. `rs_on` is not automatable because it adds one frame of latency, which is then reported. Its controls are in the Dynamic EQ editor on the Master tab (section "Resonance suppressor", D-028).

## D-026 Offline album analyzer (7.5, optional)

`MasterMind Album Analyzer.exe` is a small standalone application in the same zip. Dropping the bounced songs of an album computes their passports with the same analysis as references and stores them in the album file. The plugin itself stays a single `.vst3`; the analyzer is an optional extra and needs no installation.

## D-027 Two factory presets removed (2026-11-27)

At the user's request the factory presets "Tight Modern Metal" and "Wide and Loud" were removed. The remaining factory presets are Init, Djent Low-End Focus, Gentle Glue and Streaming Safe. A project that refers to a removed preset keeps its stored parameter values and shows the preset as modified.

## D-028 Master workspace (approved mockup v9, 2026-11-27)

The Master tab follows mockup v9 (which includes v5 to v8): spectrum, module bar with ten tiles, the editor of the selected module with every control, and the balance column (tonal balance, loudness, non-tonal checks). The module strip and the double-click detail panel are removed. Details the mockup does not fix:

- **Sizes.** At 1280 x 800 the spectrum is 280 units high. In smaller full-mode windows it shrinks (down to 150) so that the tallest editor (Multiband) still fits without scrolling. The balance column is 31 % of the width, 300 to 392 units. In the medium layout mode the balance column is left out (it stays on the Eyes tab) and an editor that does not fit scrolls; in the compact mode (640 x 400) the module bar scrolls with the selection and Multiband and Saturation show one band at a time, as in the mockup.
- **EQ bands on the spectrum.** There is one on/off parameter per band, so a band "exists" on the spectrum when it is on or when any of its settings differs from the default. Double-click on a point (bypass) switches it off and keeps it visible as a dashed point; Delete (Alt+click, menu, the cross button) resets every parameter of the band to its default, which removes the point. Only band 1 can be a low cut and only band 10 a high cut (parameter IDs are frozen): a double-click below 30 Hz makes band 1 a low cut, above 14 kHz band 10 a high cut; if that band is in use, a free band 2-9 becomes a low or high shelf and a short message says so. Frequency fields accept Hz, kHz and note names (E1, D#2, Bb3; A4 = 440 Hz). Snap rounds to the nearest semitone.
- **Threshold rings.** The ring shows the level the threshold acts on: the band detector of Multiband and of a dynamic EQ band, the Bus Comp detector, the Clipper input peak (oversampled) and, for the correlation guard, the lowest correlation of the guarded bands (inverse colours). The DSP stores the block maximum in relaxed atomics; the UI rises at once and falls at about 20 % of the scale per second. No ring when the module (or band) is off.
- **Multiband Auto release.** The Release knob shows "Auto" when `mb_bN_rel_auto` is on; right-click the knob to switch Auto release, moving it switches Auto off.
- **Tonal advice.** The twelve approved texts (mockup v7) are generated into `TooltipText.cpp` by `tools/gen_tooltips.py`. The first step names the Dynamic EQ band that cuts a band below target ("Dynamic EQ уменьшает этот диапазон: полоса N, ...") or boosts a band above target ("Dynamic EQ поднимает этот диапазон: полоса N, ... Сделайте подъём меньше."), when there is one; the second wording follows the approved first. Without such a band, Low-mid below target starts with "Dynamic EQ: Bell 250 Hz, Q 1.0, +1 dB." A tooltip shows at most three steps. Green rows show no tooltip.
- **Saturation transfer curve.** Drawn with the module's own shaper (`Saturation::shape`) at the band's drive, asymmetry and mix; the bright part spans the band's input peak of the last block.

## D-029 Monitor block removed (user decision, mockup v10, 2026-11-27)

At the user's request the Monitor block is gone from the top bar: monitor correction (6.1), system emulation (6.2) and the listen menu (mono, mid, side, band solo and delta listen from the top bar). These modes were never stored in the project, so they can no longer be switched on; their processing code stays in `Source/monitoring/` (dormant, still covered by the bit-exact safety tests) so that the feature can come back without new DSP work. Kept: reference playback with loudness-matched A/B (6.4, 7.1), chain Bypass with Match, band listen on the Low-End tab. While reference B or a band listen plays, the window keeps its red frame and a red MONITOR ON badge appears in the top bar (6.0 rule 3).

## D-030 Reference library (References tab, mockup v11)

A new tab next to Album keeps reference songs in a library folder (settings.json key `referenceFolder`, default `Documents\MasterMind References`). The list shows the audio files of that folder (WAV, FLAC, MP3, OGG, AIFF). Dropped or added files are copied into the folder in the background (through a temporary `.part` name, the originals stay) and analysed into the same cache as the slots, so assigning a song to a slot 1-6 is instant. Remove moves the file to the Windows Recycle Bin after a confirmation and empties any slot that holds it. Slot assignment uses the existing ReferenceLibrary (project state unchanged). The Album tab shows a guide panel (approved Russian text, ASD-STE100) when the window is at least 900 units wide.

## D-031 Auto match (experimental, mockups v11 and v12)

A strip at the top of the Eyes tab names the reference (the selected slot, otherwise the first ready slot in the set) and starts Auto match. It works on the live analysis while the song plays, with an analysis reset and 4 s of signal per measurement, and it moves the knobs of all ten modules in signal order (v12):

1. Undo snapshot of every parameter; B is switched off so that our mix is measured.
2. **Input**: subsonic filter on at 25 Hz; Trim brings the input integrated loudness to about -16 LUFS (gain staging for the dynamics).
3. **Dynamic EQ**: band 1 low shelf 60 Hz (Sub), bands 2-5 bells at 95 Hz, 270 Hz, 1 kHz, 3.5 kHz (Bass to Presence), band 10 high shelf 8 kHz (Air); these bands are overwritten. Band loop: the lowest band up to the current one that is outside +-0.9 dB of the target is corrected (gain -= 0.85 x deviation, limited to +-12 dB); when all bands up to the current one are green, the next band starts, so the lower bands are checked again after every step. A band that stays out of range after 5 attempts is marked and skipped.
4. **Dynamics**: Multiband on (ratio 2:1, auto release); the target gain reduction of a band is the crest factor above the reference in the matching tonal bands (0.5 to 4 dB), the threshold starts at the measured band level minus twice that and is refined twice from the measured gain reduction. Bus Comp on (VCA, 2:1, soft knee, auto release) refined to about 1.5 dB of gain reduction. Saturation on with gentle density (Low Tape 2 dB, Mid Tube 1 dB at 50 %, High clean). Clipper on (Soft, low protect) 1.5 dB below the measured input peak, at most 6 dB below 0 dBFS. Then one more pass over the tonal bands.
5. **Width**: Stereo on, Correlation guard at 0.00, split 4.5 kHz; Mid and High width move towards the reference width (analysis bands 150 Hz-4 kHz and above 4 kHz), 50-150 %, up to 3 steps. Mono material is left alone.
6. **Loudness**: Limiter on, ceiling at most -1 dBTP, Output 0 dB; Drive moves by 0.9 x (reference integrated - measured) until the difference is within 0.5 LU (6 attempts).
7. **Checks**: Low-End mono maker (120 Hz) when mono loss or low-band correlation is not green, the resonance suppressor (2-8 kHz, 4 dB) when harshness is not green; then one more pass over the bands if needed (at most two extra passes in total).
8. The strip reports what stays out of range. PLR usually cannot be green together with a much louder level; the report says so.

Stop keeps the current knobs; Undo restores the snapshot. The order logic is checked by the layout harness (`AutoMatchStrip::nextBand`).

## D-032 Saturation band solo and mute (mockup v12)

New parameters `sat_b1_solo`, `sat_b1_mute` ... `sat_b3_mute` (Off by default). They are appended at the end of the parameter list so that the order and the IDs of all older parameters stay the same; older projects load unchanged. Solo keeps only the soloed bands, Mute removes a band; both change the saved sound (they are processing parameters, not monitoring) and fade in 1 block to avoid clicks. A silent band is hatched on the graph and its card is dimmed.

## D-033 Stereo editor scales and Multiband steps (mockup v12)

The stereo graph shows two different quantities on one plot, each with its own scale: the module's width setting (0-200 %, 100 % = no change) and the measured stereo width of your mix and of the reference from the analysis (0-100 %, side energy share). The labels give the numbers. Mid uses the mean of the analysis bands 150-500 Hz and 0.5-4 kHz, High the band above 4 kHz. Double-click on a band resets its width to 100 %.

Multiband knobs move in whole steps (dB, ms, %) and show whole numbers; Ratio moves in steps of 0.5 so that the default 1.5:1 stays reachable. The parameters keep their ranges and saved values; only the knob steps and the display change. Each Multiband knob tooltip has a section with advice for its band (45 approved texts, generated by `tools/gen_tooltips.py`).

All file and folder dialogs are owned by the plugin window (a folder dialog could disappear behind the plugin window in REAPER).

## D-034 Low-End editor and tooltip completeness (mockup v13)

The Low-End editor is graphical: the low spectrum 20-300 Hz of the output (mid, side and the side after the mono maker, estimated from the filter slope and Amount), the mono zone with a draggable Mono handle and a draggable Rotator handle; a phase dial (drag around it for the angle) with the low-band waveform before (IN tap) and after (OUT tap) and their peak asymmetry; the translator view with the strongest note 30-90 Hz of the output and the 2nd and 3rd harmonics the module adds (bar heights follow Translator and 2nd/3rd; an illustration, not a measurement) against the lower limit of a phone speaker (100 Hz). The vertical scale of the low spectrum follows its loudest point.

Every knob of the plugin now has an approved tooltip with advice (18 new texts in mockup v13). Knob tooltips no longer have the module section; the tooltip of a module tile still describes the module.

## D-035 Clipper Drive, Limiter view and the Output loudness target (mockups v14 and v15, 2026-11-27)

- **Clipper Drive** (`clip_drive`, 0 to 24 dB, new parameter appended to the list) raises the level before the clipper, so the clipper can cut much deeper than the -12 dB threshold range alone allows; the threshold range stays the same so that older automation keeps its meaning. The clipper hands a mono copy of its input (after Drive) and output to the UI through a lock-free single-producer FIFO; the editor aligns them by the clipper latency and shows 40 ms around the loudest hit of the last 0.3 s, refreshed 5 times per second. The transfer curve and the mode pictures use the DSP's own clip function.
- **Limiter view**: the input after Drive, the output (peak per block) and the gain reduction of the last 6 s at 30 Hz, the ceiling, GR now and maximum, true peak and overs of the output; the mode pictures illustrate how fast the gain reduction recovers in each mode. The parameters are unchanged.
- **Output loudness target** (`out_target`, -30 to -3 LUFS, default -9, not automatable, appended): Reach target measures 4 s of signal after an analysis reset and moves Drive: the Clipper takes 40 % of each step while it cuts less than 3 dB of the peaks, the Limiter the rest (ceiling at most -1 dBTP), until the integrated loudness is within 0.3 LU of the target (10 steps at most). Undo restores Clipper on, Clipper Drive, Limiter on, Limiter Drive and the ceiling.
- The loudness target menu on the Eyes tab also offers -6 and -5 LUFS.

## D-036 Limiter rework, reference player, grey Bypass (2026-11-27, no mockup at the user's request)

The user asked to skip the mockup step for this change set and to go straight to code ("в этот раз не создавай макет сразу переходи к написанию кода"). The CLAUDE.md protocol is waived for these items only.

- **Limiter gain computer** (spec 4.9 "two stages"). The old computer released the gain within a bass cycle (35 to 150 ms release after a 2 ms look-ahead), so the gain followed the bass waveform: harmonics on bass (THD 0.33 % in Balanced and 0.68 % in Aggressive on a 55 Hz sine with 7 dB of reduction) and intermodulation on guitars. The user heard this as early crackle. The new computer per channel: required gain -> **hold** (sliding minimum over 25, 18 or 13 ms by mode, longer than half a period of 40 Hz) -> **fast stage** (instant attack, release 120, 60 or 25 ms) and **slow program-dependent stage** (one-pole on the reduction, attack 250, 350 or 500 ms, release 900, 700 or 500 ms); the deeper of the two wins, so the fast stage only releases down to the average reduction of the music -> look-ahead minimum (window + 2 samples, unchanged) -> **two cascaded moving averages** that together span the look-ahead (an S-shaped attack instead of a linear ramp). The latency and the ceiling guarantee are unchanged. Transients 0 to 100 % make the slow attack up to 3 times slower (drum hits pass the slow stage) and the fast release up to 60 % shorter. Manual release sets the fast release and turns the slow stage off. New tests: THD below 0.3 % on a 55 Hz sine with 7 dB of reduction (measured about 1e-6 %), 50 Hz + 1 kHz intermodulation below 0.3 % (about 1e-6 %), and less than 1 dB of extra reduction 700 ms after a single 3 ms peak 12 dB over the ceiling. The ceiling test is unchanged and passes.
- **Buttons 1-6** only select the reference. They no longer switch B on or start playback; while B plays, the new song continues at the same position. Play and Play from start stay in the right-click menu.
- **Reference player**: a strip under the tab bar while B is on, with Play/Pause, From start, the song name, time and loudness match, a waveform overview of the whole song (1200 bins of peak and RMS, computed by the decode job) that shows the played part, and click or drag to seek; a button returns to A. It replaces the player strip in the tab bar. Switching to a shorter song or a loop end beyond the song can no longer read past the end of the audio.
- **Library preview**: a play/pause button in every row of the References table plays the song through B without a slot (`ReferenceLibrary::preview`); the loudness curve comes from the analysis cache when the song is analysed. Selecting a slot ends the preview.
- **Auto match progress**: a bar along the bottom of the strip and a percentage before the status: finished steps plus the running 4 s measurement, never moving back; green when finished, grey after Stop. The strip is 42 px high (was 36).
- **Bypass** greys out the window below the top bar and shows "BYPASS: you hear the input" in the footer; the controls stay usable. **The MONITOR ON badge and the red window frame (spec 6.0 rule 3) are removed** at the user's request; B is visible through the player and the B button, band listen through its button.

## D-037 Spectral limiter after Newfangled Elevate (mockup v16, approved and implemented 2026-11-28)

The user asked to rebuild the per-band ("pinpoint") behaviour of Newfangled Audio Elevate in our Limiter. User decisions (2026-11-28): a linear-phase filter bank (larger latency accepted), the core features first, a mockup before the code.

**What Elevate does** (its user guide, Rev. 5, and public statements of its developer; no code or binary was examined):
- An auditory filter bank splits the signal into up to 26 triangular bands on the Mel scale (critical bands of hearing); the band count is selectable, 1 band turns it into a standard look-ahead limiter. The bands reconstruct the input exactly; the developer describes FIR filters with perfect reconstruction. Users report about 100 ms of latency.
- Limiter: Gain (drive), Speed (combined attack and release), Ceiling, an optimally soft knee for fast release with low distortion, optional true-peak mode.
- Adaptive Gain: the number of dB by which the gain of one band may differ from the others, so the loudest bands near the limit get more reduction.
- Adaptive Speed: adapts attack, release and look-ahead per band; reduces pumping, breathing and perceived distortion. The developer attributes low-end artifacts to intermodulation and cures them with slower speed.
- Per-band gain (EQ inside the limiter), Transient Emphasis after the limiter (0 to 200 % per band, Adaptive Transient smooths it across neighbouring bands), then a spectral clipper (optimal soft curve, up to 12 dB drive, a frequency-domain correction against the dull sound of a clipped full-band signal) that also enforces the ceiling.

**Our design** (Limiter module, `Source/dsp/SpectralLimiter.*`):
1. Drive.
2. Filter bank: B = 26 (or 16, 8) triangular bands with centres from 30 Hz to 16 kHz, closer in the low end than Mel so that kick and bass fall into different bands. Each band is a linear-phase FIR designed from the same window; the triangles sum to one, so the bands sum exactly to the input delayed by half the FIR length. FIR length 4096 at 44.1/48 kHz, scaled with the sample rate (about 43 ms); uniformly partitioned FFT convolution (512-sample partitions, juce::dsp::FFT), one input FFT shared by all bands. Total latency about 55 ms; it does not change when the limiter is switched on or off.
3. Band gain computer per band and channel (stereo link as now): a fast peak envelope and a slow (about 1 s) program envelope. A band whose peak rises above its own program level more than the other bands gets extra reduction: offset = -min(Adaptive Gain, excess above the energy-weighted mean excess), applied only while the limiter is near the limit (scaled by the broadband reduction). With Adaptive Gain at 0 every band has the same gain. The raw input is available half the FIR length earlier than the band signals, so this stage looks ahead for free.
4. Adaptive Speed: hold and release of every band scale with the band period (a hold of at least one period of the band centre, low bands slower, high bands faster); 0 % uses the same time constants in every band (the D-036 values of the mode).
5. Sum of the gained bands -> the D-036 broadband gain computer on the sum (hold, fast and slow stages, look-ahead, cascaded averages) as the shared part of the reduction.
6. Transients (existing `lim_transient`, new meaning): per-band attack emphasis = ratio of a fast to a slow envelope, up to +6 dB, after the limiting; Adaptive Trans smooths it across neighbouring bands (0 % = one emphasis for all bands).
7. A soft clipper (the Clipper's soft curve, 4x oversampled) catches the emphasised attacks at the ceiling, then the D-036 true-peak brickwall stage guarantees the ceiling (unlike Elevate's original sample-peak behaviour).
8. Not in the core step: per-band EQ and per-band transient sliders (Elevate's Limiter/EQ and Transient pages) and the frequency-domain clipper correction.

**Parameters** (appended, IDs frozen afterwards): `lim_bands` (Classic, 8, 16, 26; default 26; not automatable, it changes the latency), `lim_adapt_gain` (0 to 12 dB, default 6), `lim_adapt_speed` (0 to 100 %, default 70), `lim_adapt_trans` (0 to 100 %, default 50). `lim_transient` keeps its ID and range and becomes the attack emphasis. Classic is the D-036 limiter with its 2 ms look-ahead.

**Deviation from the spec**: with the defaults the plugin latency grows from 270 samples to about 2,900 samples at 48 kHz (spec section 10 quotes 270). Classic restores the old latency.

**Tests planned**: perfect reconstruction of the filter bank (bands sum to the delayed input within -100 dB); the ceiling test with every band count; the D-036 THD and IMD tests; a pinpoint test: a 60 Hz kick burst plus a steady 1 kHz tone, the 1 kHz tone is pumped at least 3 dB less with Adaptive Gain 6 dB than with 0 dB; CPU at 26 bands reported.

## D-038 Inflator replaces the Clipper (mockup v17, approved and implemented 2026-11-28)

The user asked to replace the Clipper module with an Inflator after the open-source JS Inflator (https://github.com/Kiriki-liszt/JS_Inflator, GPL-3.0, itself a recreation of the Sonnox Inflator curve).

**How JS Inflator works** (read from its source): Input gain (-12 to +12 dB), optional clip to +-1 (0 dBFS) before and after the curve, an odd-symmetric polynomial on |x| with a Curve parameter c in -0.5..+0.5: y = A|x| + B|x|^2 + C|x|^3 - D(|x|^2 - 2|x|^3 + |x|^4) with A = 1.5 + c, B = -2c, C = c - 0.5, D = 0.0625 - c/4 + c^2/4 for |x| <= 1 (unity gain at 0 and at 1, so quiet parts rise more than peaks and only odd harmonics are added); above 1 the curve folds back (2|x| - |x|^2), which is why the clip option exists. Effect mixes dry and curve output (0 to 100 %), Output -12 to 0 dB. Optional Band split: three bands at 240 Hz and 2.4 kHz through the curve separately. Oversampling 1x to 8x.

**Our module** (slot 7, between Stereo and Limiter, where the Clipper was): Input, Effect, Curve, Output, Clip 0 dB, Band split (complementary split at 240 Hz and 2.4 kHz, sum equal to the input), oversampled by the Quality setting (4x normal, 8x high) with the Clipper's oversampler, so the plugin latency does not change. The editor shows the waveform before and after, the transfer curve with the current Effect, the odd harmonics of a sine at the current level and the loudness the module adds (RMS after minus before).

**Parameters**: the `clip_*` IDs stay in the parameter list so old projects load, but they no longer act; `clip_on` stays the module switch and is renamed "Inflator On". New, appended: `infl_input` (-6 to +12 dB, 0), `infl_effect` (0 to 100 %, 30), `infl_curve` (-50 to +50, 0), `infl_output` (-12 to 0 dB, 0), `infl_clip` (on), `infl_split` (off).

**Other modules**: Output Reach target moves only the Limiter Drive (the Inflator stays as set). Auto match replaces its Clipper step: when the mix is more dynamic than the reference (crest factor), it raises Inflator Effect up to 50 %.

**Licence (open question to the user)**: copying the GPL-3.0 code would make MasterMind GPL-3.0 as a whole. The alternative is our own implementation of the published curve formula with credit in the docs, without copying code.

## D-039 How D-037 and D-038 were built (2026-11-28)

The user approved mockups v16 and v17 and asked to write the code without further mockup confirmations.

**Licence**: the user chose to copy the JS Inflator code, so MasterMind is now distributed under GPL-3.0 (`LICENSE`). The Inflator curve, coefficients, clip stages and band split in `Source/dsp/Inflator.*` follow JS Inflator by yg331 (Copyright (c) 2024, GPL-3.0); the file headers say so. JUCE 8 is used under its AGPLv3 option, which is compatible with GPL-3.0 (section 13).

**Spectral limiter, as built** (differs in detail from the D-037 plan):
- `SpectralBank`: filter length 4096 at 44.1/48 kHz (8192 at 88.2/96, 16384 above), frames of 256 (512, 1024) samples, uniformly partitioned FFT convolution with 16 partitions. Per frame the effective filter `sum_b g_b H_b` is built in the frequency domain; gains change per frame and are interpolated linearly inside it (the output with the previous and with the new gains is crossfaded), so the output is exactly the gain-weighted band sum. With all gains at one the bands reconstruct the input to about -126 dB (test).
- Band reductions come from a Hann analysis over the newest half filter length (about 21 ms ahead of the output, every second frame); the attack emphasis comes from the frame spectrum and is delayed to line up. Reductions act only while the input is above the ceiling (pressure, feed-forward), never more than that pressure, and at most Adaptive Gain dB beyond the energy-weighted mean.
- The broadband D-036 core listens to the band output without emphasis and applies its gain to the band output with emphasis; a 4x oversampled hard clipper at the ceiling and a 1 ms true-peak safety Limiter catch the emphasised attacks. Without emphasis (Transients 0 %) a delay line replaces the clipper and the safety stage (crossfaded when Transients changes), which halves the cost.
- CPU at 48 kHz on the build machine (20 s benchmark, one core): Classic 3.4 %, 26 bands 6.3 %, 26 bands with Transients 12.2 %.
- Latency with 26 bands at 48 kHz: 2,735 samples (57 ms). The chain latency is cached per module, and the processor's worker thread does not update the host latency while `prepareToPlay` runs (a race found by the binary null test).
- Tests: band reconstruction (3 sample rates, 3 band sets), the ceiling test for Classic and 26 bands in every mode plus 26 bands with full emphasis, THD and IMD (below 0.01 %), and the pinpoint test (a steady 1 kHz tone under a 55 Hz kick pumps 7.9 dB with Adaptive Gain 0 and 5.2 dB with 9 dB).
- The mockup's separate GR and meter column moved into the mode list footer (max GR, true peak, overs), as drawn in v16.

**Inflator, as built**: Effect mixes in the oversampled domain (the dry part passes the same filters), so the module latency equals the old Clipper's and the total latency does not change from the Inflator. Output Reach target: the Inflator takes up to 40 % of each step through Effect (up to 50 %, dB per percent from the measured inflate gain), the Limiter Drive the rest; Undo restores both. Auto match: Effect = 12 x the mean crest-factor excess over the reference, at most 50 %. The tooltip check in the UI harness confirms that every knob on every screen has a tooltip with advice.

## D-040 Mockup v18: no A/B, hint bar, Momentary readouts, phase Auto, three Auto match modes (2026-11-28)

The user asked for a batch of changes and approved mockup v18 with "давай реализуем" after choosing to implement all three Auto match variants.

**Interface** (mockup v18):
- The A/B switch is gone. The buttons 1-6 only choose the reference for the analysis, the checks and Auto match (`ReferenceLibrary::choose`); they never start or switch playback, which belongs to the References tab. The Play items left the right-click menu of the buttons.
- Match is renamed Match Volume (label only; the parameter-free monitoring flag is unchanged).
- The tooltip bubbles are replaced by a two-line hint bar at the bottom of the window where the version was: line 1 name, range and what it does, line 2 the advice and the other sections. The hint appears at once (no 0.5 s delay). One line does not fit: the hints with advice are about 280 characters (about 1700 px at 12 px). The bar is 38 px high (30 px in the compact layout); the BYPASS label moved into its right half.
- The Integrated row left the checks (it was information only); `computeChecks` returns 11 rows.
- The Match EQ button left the spectrum header (Auto match sets the EQ).
- The user found the loudness readouts slow. The analysis keeps up (about 3 % of one core for both taps, benchmark in the tests); the slowness is the 3 s Short-term window and values held since the start. The top bar (M) and the Master loudness panel now show Momentary (0.4 s), and the panel shows the true peak of the last 3 s. Eyes keeps Short-term and the held values.
- Low-End phase Auto: two looks 2 s apart at the analysed input low band (20-150 Hz of the mid signal at the low analysis rate), then every 5 degrees with a copy of the rotator at that rate (an allpass commutes with the analysis filter), and the angle with the smallest mean peak asymmetry (`dsp/PhaseAuto.h`). A second click restores the previous rotator settings; a manual change of the angle ends the Auto state. Test: a 55 Hz note with a lopsided 2nd harmonic goes from 49 % to 0.5 % asymmetry.

**Auto match** (`Source/automatch/MatchEngine.*`, `ui/AutoMatch.*`), replacing the live loop of D-031:
- Research. Matchering 2.0 (sergree, GPL-3.0) matches RMS, frequency response (FIR EQ from smoothed average spectra, mid and side), peak and width, then limits. The Ozone 11 Master Assistant analyses the loudest 8 s, sets the EQ from the tonal balance, Impact per band from microdynamics, the Imager per band from mid/side, and the Maximizer gain from the loudness. ST-ITO (Steinmetz et al., ISMIR 2024) searches the parameters of an effect chain at inference time with CMA-ES (population 64, up to 25 steps, about 1 min) on an audio-embedding distance; ITO-Master (Koo et al., Sony, ISMIR 2025) applies the idea to mastering; a 2025 follow-up adds a Gaussian prior on the parameters. Neural parameter estimators (DeepAFx-ST) were rejected: they need a trained model and large runtime files.
- Common to all modes: the engine thread records 12 s of the plugin input (silent chunks skipped, never the audio thread), the loudest 8 s plus 1 s of preroll are used. A private copy of the chain (`Twin`: its own parameter tree with the real layout and its own `MasterChain`) runs the excerpt on background threads (hardware threads minus 2, at most 8, plus one for the final check), and a lightweight `TapAnalyzer` measures it. The fixed setup of D-031 (subsonic filter, EQ band layout, neutral dynamics, gentle saturation, stereo guard, Inflator neutral, Limiter ceiling at most -1 dBTP, input gain staging to -16 LUFS) stays; mono maker and the resonance suppressor are switched on when the neutral pass shows low-band correlation below 0.8 or harshness 1.5 dB above the reference.
- The distance (lower is better): tonal deviation per band over the corridor (at least 0.75 dB), squared; loudness error over 0.5 LU, weight 1.5; crest factor per band over 1.5 dB, weight 0.4; width per stereo band over max(8, 15 %), weight 0.5; true peak above the ceiling, weight 2; harshness more than 1 dB above the reference, weight 0.3; plus a prior term in the search.
- Sixteen knobs: six EQ gains (-9..+9 dB), five Multiband thresholds (-40..0 dB), Bus Comp threshold (-30..0), Inflator Effect (0-60 %), Limiter Drive (0-24 dB), mid and high width (60-150 %, 60-160 %).
- Fast: neutral pass, formulas (EQ = -0.85 x tonal difference; Multiband target gain reduction from the crest factor above the reference, threshold = band level - 2 x target; Bus Comp 3 dB under its detector level; Inflator 12 x the mean crest excess up to 50 %; width ratio^0.8; Drive from the loudness gap), then correction passes. Hybrid: Fast, then separable CMA-ES (12 x 10, sigma 0.07) around it with a prior weight 0.4, then two correction passes. Precise: separable CMA-ES from neutral settings with the Drive seeded from the loudness gap (16 x 22, sigma 0.22, prior 0.15), then two correction passes. Every mode ends with a check on the user's limiter setting and a secant correction of the Drive.
- The search runs the user's limiter setting: with the classic limiter in the search and the band limiter in the final check the results got worse (the two limiters shape the tone differently).
- Measured on the build machine (8 threads, 8.5 s excerpt, synthetic mix vs reference, distance 557 at neutral): Fast 10 s to 8.9, Hybrid 46 s to 7.7, Precise 81 s to 8.7. On a 4-core computer Precise takes about 4 minutes.
- The mode is a global setting (`automatch_mode` in settings.json), not a parameter.

## D-041 Gentle low end in Auto match; buttons 1-6 switch only; pause plays the project; metrics region (2026-11-28)

**The user's report**: every Auto match mode made the low end boomy and bubbly although the numbers matched the reference; the plugin simply raised the low EQ bands to reach the Sub and Bass shares. I named three causes (the reference measured over the whole song while our mix is measured over its loudest 8 s; a static low boost lifts sustained bass notes and resonances, not only the kick; more low energy makes the limiter pump with the bass) and seven remedies. The user chose remedies 3 to 7 and, for the comparison of like with like, a manual metrics region on the References tab instead of remedy 1.

**Auto match, as built** (`automatch/MatchEngine.*`):
- (3) Sub and Bass EQ boosts are capped at +3 dB, Low-mid at +2 dB (mud), the other bands at +-4 dB, Sub and Bass cuts at -6 dB. A boost of Sub or Bass costs (gain / 2 dB)^2 in the distance, a Low-mid boost 0.5 (gain / 1.5 dB)^2.
- (4) EQ bands 1 and 2 are dynamic (compress mode, range 3 dB, attack 15 ms, release 120 ms); after the neutral pass their threshold is set 1 dB above the average detector level of the band, so a boost backs off on loud low passages (long notes) and a kick keeps its weight.
- (5) Two new search knobs: Low-End Translator amount (0-60 %, mix 50 %) and the low band drive of Saturation (0-8 dB, neutral 2 dB). The formula gives what the capped boost cannot: Translator 12 % per missing dB (at most 50 %), low drive 2 dB plus the missing dB.
- (6) Two new terms in the distance: boom (Sub or Bass crest factor more than 0.5 dB below the reference, squared) and pumping (standard deviation of the limiter gain reduction in 10 ms means above 1 dB, weight 0.8 per (0.5 dB)^2).
- (7) The fixed setup: subsonic filter 30 Hz 24 dB/oct, mono below 120 Hz at 100 %.
- A test showed that capping the low boost alone made the formula push the other bands to the limits: the tonal balance is a share of the total, so one band that misses energy makes all others look too loud. The tonal deviations are now median-centred before the EQ formula, the corrections and the distance, so the missing band stays the only one to correct.
- Tests: a thin mix against a reference with far more low end keeps Sub and Bass at +3 dB or less, uses the Translator, has the dynamic bands with a measured threshold, the 30 Hz filter and mono maker, and still gets closer to the reference.

**Buttons 1-6** (mockup v19): they only switch between loaded references. Empty, analysing or failed slots are dimmed and inactive; the right click does nothing (the slot menu is gone; the reference loudness matching mode moved to the options menu at the bottom right); audio files dropped on the top bar are ignored (presets still import). When a reference is in the player, a click moves the player to the new song and keeps its play or pause state (default of section 12: "switch" applied to the player too); otherwise nothing starts.

**Pause**: a paused reference now lets the project play (the monitoring section uses the reference only while it plays); the player stays open and keeps its position, seeking works while paused, and the window greys for Bypass again while paused.

**Metrics region**: the backend is in (`ReferenceLibrary::setRegion`, `Slot::metrics()`, `analyseFile` with a time range, cached per region, saved in the project). The target, the checks and Auto match use the region analysis of a slot when one exists, including the reference loudness. Test: a 60 Hz tone followed by noise gives a Sub share of -2.3 dB for the whole file and -26 dB for a region in the noise. The selection on the player waveform (mockup v19) waits for the user's approval.

## D-042 Louder limiting: spectral clipper, leveler, density, softening, longer lookahead, adaptive release, masking, Reach target with a distortion budget (2026-11-28)

**The user's report**: the band limiter does not reach the loudness of Elevate. I proposed seven additions; the user asked for all of them, without a mockup ("макет не нужен, пиши код"), and approved mockup v19 (metrics region) in the same message. v20 holds only the new tooltip texts.

**Measurement first.** A loudness benchmark (`[.bench]`, dense metal-like mix: kick and snare on a grid, a distorted guitar chord, a bass note, plus a quiet steady 2 kHz pilot tone whose level variation measures pumping) showed the real limit: at Drive +24 dB the limiter stops at about -8.6 LUFS with 1.9-2.2 dB of pilot pumping. More Drive mostly becomes more gain reduction: the program-dependent slow stage of D-036 keeps the reduction at the level of the drum peaks, and the band stage (43 ms analysis) is too slow to treat single kicks.

**Added** (all new parameters appended, defaults keep the earlier sound):
1. `lim_clip` 0-9 dB, **spectral clipper** (`dsp/BandClipper.*`) after Drive, before the band stage or the Classic core: four Linkwitz-Riley bands (120 Hz, 1 kHz, 5 kHz, allpass-compensated), soft clip (linear to 0.7 t, tanh knee to t) at 4x oversampling, threshold `amount` dB below each band's own peak envelope (instant attack, 1.5 s release). Only the clipping residual is added to the band sum, through an 18 kHz 4th-order low-pass: content above it was underestimated by the true-peak detection (a sweep went to -0.86 dBTP with 2x oversampling and no filter; now -1.135). Bypassed through a delay of the same latency at 0 dB (20 ms crossfade). Measured: Clip 6 dB, -8.6 -> -7.6 LUFS and pumping 1.9 -> 1.1 dB.
2. `lim_leveler` 0-100 %, **slow first stage**: up to 3 dB of the overshoot over the ceiling, 40 ms attack, 500 ms release, applied before the clipper. It does not add loudness at the same Drive (it lowers the level before the limiter); it shares the work.
3. **Program-dependent band release** (always on, scaled by Adaptive Speed): the release of each band runs 0.45x after hits and 1.6x on sustained notes, from the attack detector of the band.
4. **Masking** (always on): a band under the spread of a louder neighbour (12 dB per band upward, 27 dB downward, scaled to the band count) may take up to 1.5x the Adaptive Gain and reacts to a lower share of the mean excess.
5. `lim_soften` 0-100 %, **transient softening** in the band stage: the attack detector in the other direction (up to -6 dB), in both band outputs so the core hears it. Band modes only ("off" in Classic). Soften 100 %: +0.8 LU.
6. **Lookahead up to 10 ms** (the range of `lim_lookahead` grew from 0.5-5 to 0.5-10 ms; saved projects keep their value, host automation of this structural parameter would map differently). On the benchmark it changed little.
7. `lim_density` 0-100 %: the fast stage may release up to 12 dB below the slow stage. Measured +0.4 LU at 100 % with more pumping; useful with Clip.
- **Reach target with a distortion budget** (`automatch::LoudnessSearch`): 12 s of the song are recorded (engine thread), the loudest 8 s plus 1 s preroll run on twins of the chain: 11 candidate mixes of Inflator, Clip, Leveler, Density (plus 3 with Soften in band modes), each with the Drive found by secant steps (at most 4 passes), and the candidate that reaches the target within 0.25 LU with the lowest artefact cost wins: (mean limiter reduction / 4 dB)^2 + (pumping / 1 dB)^2 + 0.7 (clip / 3 dB)^2 + 0.5 (Inflator / 50 %)^2 + 0.5 (Soften / 50 %)^2 + 0.3 (Density)^2. Without a candidate that reaches it, the loudest one is set and the message says so. Test: 8 LU louder than the input in about 4 s for a 4.5 s excerpt (8 threads).
- All together at their maximum: -8.6 -> -7.3 LUFS on the benchmark with pumping 2.2 -> 1.1 dB; the ceiling test now also covers Classic and 26 bands with every new stage at maximum. CPU at 48 kHz: 26 bands 6.0 %, with all stages 8.5 %.
- The Limiter editor has two knob rows: Drive, Ceiling, Release, Lookahead, Link | Clip, Leveler, Density; then Adaptive Gain, Adaptive Speed, Transients, Soften, Adaptive Trans.

**Metrics region UI** (mockup v19, approved): a drag over the waveform of the reference player selects the region (at least 1 s), the edges can be dragged, the label in the zone shows the time and "analysing..." and has a cross that clears it; a click without a drag still seeks. The library table has a Metrics column.

## D-043 EQ band panel removed; static Auto match EQ; fast band limiting; loudness limits measured (2026-11-28)

**The user's report** (build 0.7.19): the floating EQ band panel covers the curve; points 2, 5 and 10 cannot be deleted with its cross; the limiter is still much weaker than Elevate; Auto match now makes huge dynamic low bands that pump; the gain of EQ point 5 once fell by itself.

- **EQ band panel removed** (the user asked for it, so no mockup). The panel followed its point; a dynamic band moves its point with the gain changes, so the panel moved away under the mouse between press and release and the cross never fired. The delete itself works for every band (a new check in the UI harness). The band is edited in the Dyn EQ editor and with the right click on a point (Delete is in that menu).
- **"Gain fell by itself"**: band 5 had Dynamic on (the screenshot shows it). The dashed curve and the moving point show the live dynamic gain (the default Range of 6 dB at -24 dB threshold cuts while the band is loud); the Gain knob does not move. Not changed; explained to the user. A host automation envelope written during Auto match would look the same, also mentioned.
- **Auto match EQ is static again**: the dynamic Sub and Bass bands of D-041 (3 dB range, 15/120 ms) pumped with the kick. The +3 dB cap, the boost penalty, the harmonics, the boom and pumping terms, the 30 Hz filter and mono below 120 Hz stay.
- **Fast band limiting** in the band stage (after Elevate): the overshoot of the newest input frame over the ceiling (known about 40 ms before it reaches the output, because of the bank latency) goes to the bands that carry it: weight ((share of the loudest band - 0.2) / 0.8)^1.5, at most Adaptive Gain (x1.5 when masked) dB; applied at the output frame as the deepest need of that frame and the next two (smooth attack), released per band (80 ms at 150 Hz, scaled by frequency^-0.3, 30-150 ms, faster after hits). It lowers the kick in its bands before the broadband core, so the guitars are ducked less. Measured: 26 bands Balanced at Drive 24 dB -8.6 -> -8.4 LUFS; with Adaptive Gain 12 and Aggressive -7.3.
- **What limits the loudness, measured**: on the benchmark mix the clean limit is about -7 LUFS. The chord of distorted guitars has a crest factor of 6-7 dB, so a gain that does not change the wave shape cannot bring it closer to the ceiling; Release (auto, 50 ms, 20 ms) changes nothing there. A hard clip at +24 dB reaches -1.3 LUFS: everything louder than about -7 comes from changing the wave shape (clipping, saturation), with distortion. Aggressive + Adaptive Gain 12 + Clip 6 + Density 100 reaches -7 LUFS already at Drive 14 dB with half the pumping of the plain band limiter. This cannot be compared with Elevate here (no Elevate in the tests); the user is asked to compare at equal output loudness.
- **Reach target** also tries the Aggressive mode, Clip up to 9 dB and (with bands) Adaptive Gain 12; Undo restores them.

## D-044 Black-box comparison with Newfangled Elevate; output clipper; tuned band stage (2026-11-28)

**The user asked** to look at how Elevate works, pointing at its installed .vst3. I declined to decompile it (licence and copyright; MasterMind is GPL) and proposed a black-box comparison; the user agreed.

**Tool**: `tests/CompareLimiters.cpp` (target `MasterMindCompare`, local only, not in CTest). It loads a VST3 like a host, lists its parameters (what any host shows), and drives both limiters with the same signals: the dense metal-like mix with a quiet 2 kHz pilot tone, a 55 Hz and a 1 kHz sine. For each, the gain is found by bisection for -9, -8, -7 and -6 LUFS; it reports the gain, the pilot pumping (level variation in 20 ms windows), the crest factor and the true peak, and the THD of the sines 12 dB into limiting. Elevate 1.14 ran with Match Level off (on by default: it matches the output level to the input), Ceiling -1.0, True Peak on.

**What the measurements showed about Elevate**: it is not "louder without distortion". It reaches -6 LUFS at +11.3 dB with 0.77 dB of pilot pumping and a crest factor of 5.0 dB, and it distorts a 55 Hz sine at +12 dB to -21 dB THD (about 9 %) and a 1 kHz sine to -34 dB. It lowers the crest factor (it changes the wave shape), its bands follow the peaks fast, and its output has a clipper (parameters Clipper Drive and Shape). Its band centres are Mel-spaced from 0 Hz (0, 102, 218, 351 Hz ...), so the low end is two bands.

**Changes, each measured with the tool**:
- `lim_out_clip` (0-6 dB, band modes): the core aims that many dB above the ceiling and a soft-knee clipper at 4x shaves the peaks back; the true-peak safety stage follows. A wide tanh knee was worse (it started early and lost 1 dB of headroom).
- The spectral clipper (`lim_clip`) clips only peaks that stand more than 6 dB above the band's own mean-square level (300 ms): a steady tone passes clean, drum hits lose up to the amount. Bass THD with Clip 2 dB: -16 -> -21 dB.
- Density (`lim_density`) also shortens the hold of the core, by up to 92 %: the gain may follow the waves, which lowers the crest factor (bass THD at Density 100 %: -37 dB).
- Fast band limiting: release by band 0.02 s x (1 kHz / f)^0.5 within 12-120 ms, x2 Transparent, x0.5 Aggressive, faster after hits; the band weight ((share - 0.1) / 0.9), cap 2 x Adaptive Gain. Broader weights (from 0.05, square root) pumped less but let a kick reach -0.93 dBTP in the ceiling test, so they were not kept.
- Factory preset **Loud Band Limiter**: 26 bands, Aggressive, Adaptive Gain 12, Clip 2, Out Clip 2, Density 100, Drive 6. Reach target also tries this set (with and without Inflator, and with Out Clip 1 and 3).

**Result** (same signals, same output loudness; gain = Elevate Gain or our Drive):

| LUFS | Elevate gain / pumping | MasterMind Loud Band Limiter gain / pumping | MasterMind 26 Balanced |
| --- | --- | --- | --- |
| -9 | 6.5 / 0.38 dB | 7.2 / 0.40 dB | 9.3 / 1.20 dB |
| -8 | 8.0 / 0.45 dB | 8.7 / 0.49 dB | max -8.3 at 24 dB, 1.87 dB |
| -7 | 9.4 / 0.56 dB | 10.3 / 0.64 dB | not reached |
| -6 | 11.3 / 0.77 dB | 12.3 / 0.85 dB | not reached |

Crest factor at -6 LUFS: Elevate 5.0 dB, MasterMind 5.3 dB. THD at +12 dB: 55 Hz both -21 dB; 1 kHz Elevate -34 dB, MasterMind -23 dB. Balanced stays the clean mode (-75 dB on the bass sine).

**Also in this round** (the user's report on 0.7.20): see D-043 for the EQ band panel and the static Auto match EQ.

## D-045 Limiter presets in the module header; the metrics region loops (2026-11-28)

- The user asked to remove the plugin-wide factory preset "Loud Band Limiter" (D-044): it only set the Limiter. It is gone; a "Loud Band Limiter" saved in an old project shows as missing, its values stay.
- Instead the Limiter header has a preset list (where the user pointed, left of Bands): Clean (26 bands, Balanced, every D-042/D-044 stage off), Punchy (Transients 30, Clip 1, Out Clip 1, Density 50), Loud (the set measured against Elevate: Aggressive, Adaptive Gain 12, Clip 2, Out Clip 2, Density 100), Classic (no bands). A preset sets the character, never Drive or Ceiling. The list shows the preset whose values match, otherwise "Custom". The user placed it on the screenshot, so no separate mockup.
- The metrics region of the song in the reference player now also loops the playback (the user's request): the player sets the loop of the monitoring section to the region of the song it plays, and clears it without a region. Only the region is analysed for the target (D-041), so what you hear is what the Eyes tab and Auto match compare with.

## D-046 Limiter grit above +3 dB Drive; value fields close on any click; advice texts stand alone; factory presets removed (2026-11-28)

- **Factory presets**: the user asked to delete all presets. Every factory preset except Init is gone (Djent Low-End Focus, Gentle Glue, Streaming Safe); user preset files and the Limiter module presets stay (they are not plugin presets).
- **Grit**: the user hears the Limiter "crunch" above +3 dB of Drive. A new measurement in `MasterMindCompare grit`: a steady 1 kHz tone and a distorted guitar chord under hard kicks; the energy 950-1050 Hz without the tone, relative to the tone, collects the modulation (ducking at the kick rate) and distortion products on a held note. Before this change: Clean (26 bands, Balanced) -47 / -29 / -20 / -17 dB at +3 / +6 / +9 / +12 dB; Classic -46 / -24 / -17 / -16; Elevate -34 / -37 / -38 / -30. Our steady notes are ducked by every kick: the broadband core takes the part of the peak the bands do not.
- Change: the fast band limiting also weights the bands with an attack (fast level over slow level, from 3 dB), at 0.6, next to the dominant bands (full weight); the cap is 3 x Adaptive Gain. Clean now -48 / -32.5 / -23 / -20 dB, pumping of the pilot on the benchmark mix at -9 LUFS 1.20 -> 0.93 dB. Loud: pumping 0.40 -> 0.50 dB at -9 LUFS (the snare attacks now lower the bands of the pilot), grit at +9 dB -27 -> -28 dB.
- Tried and not kept: (a) attack weight 1.0 with the dominant weight halved: grit better (-38 at +6) but the pinpoint test failed (the kick bands got less reduction and the core ducked more); (b) a larger cap: no change (the bands already take the full need); (c) lowering the attacked bands by what the whole frame needs given the share of the peak they carry: over-reduction, less loudness, more grit. Measured: Drive mode, Density, lookahead and Out Clip barely change the grit of Clean; without the band adaptation (Adaptive Gain 0) Clean equals Classic.
- Conclusion: the remaining gap to Elevate needs real per-band limiting (each band limited from its own peaks with lookahead, the core only as a safety stage), a larger rework of the band stage; proposed to the user.
- **Value fields**: a click anywhere in the window closes an open knob value field and commits it like Enter (the editor listens to every mouse press and takes the keyboard focus from a text field that was not clicked). Not testable in the headless harness (no keyboard focus without a window); in the REAPER checklist.
- **Advice texts** (mockup v22, texts only): the second line of the hint bar must read on its own; seven advices that began with "Так" or "Тогда" (pointing back to the first line) were rewritten, among them SC HPF of Bus Comp, which the user found unclear.
