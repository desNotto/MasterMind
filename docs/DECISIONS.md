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

## D-031 Auto match (experimental, mockup v11)

A strip at the top of the Eyes tab names the reference (the selected slot, otherwise the first ready slot in the set) and starts Auto match. It works on the live analysis of the OUT tap while the song plays, with an analysis reset and 4 s of signal per measurement:

1. Undo snapshot of every parameter; B is switched off so that our mix is measured.
2. Dynamic EQ: band 1 low shelf 60 Hz (Sub), bands 2-5 bells at 95 Hz, 270 Hz, 1 kHz, 3.5 kHz (Bass to Presence), band 10 high shelf 8 kHz (Air); dynamic off, placement Stereo, gains start at 0. These bands are overwritten.
3. Band loop: the lowest band up to the current one that is outside +-0.9 dB of the target is corrected (gain -= 0.85 x deviation, limited to +-12 dB); when all bands up to the current one are green, the next band starts. So after every step the lower bands are checked again, as the user asked. A band that stays out of range after 5 attempts is marked and skipped.
4. Loudness: Limiter on, ceiling at most -1 dBTP, Drive moves by 0.9 x (reference integrated - measured) until the difference is within 0.5 LU (6 attempts).
5. Checks: Low-End mono maker (120 Hz) when mono loss or low-band correlation is not green, the resonance suppressor (2-8 kHz, 4 dB) when harshness is not green; then one more pass over the bands if the loudness step moved them.
6. The strip reports what stays out of range. PLR usually cannot be green together with a much louder level; the report says so.

Stop keeps the current knobs; Undo restores the snapshot. The order logic is checked by the layout harness (`AutoMatchStrip::nextBand`).

