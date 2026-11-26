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
