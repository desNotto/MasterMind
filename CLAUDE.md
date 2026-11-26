# MasterMind: instructions for the coding agent

MasterMind (author desNotto, repository https://github.com/desNotto/MasterMind) is a Windows stereo VST3 master-bus plugin for modern metal. The full specification is `docs/SPEC.md` and it is the source of truth. The spec still uses the working title MetalBus; read MasterMind everywhere (see `docs/DECISIONS.md`, D-001).

Build MasterMind in small milestones, talk to the user in Russian, write everything else in English, and never start the next milestone before the user has tested a downloadable CI build in REAPER.

## Language

- Chat with the user: **Russian** only. This covers explanations, questions, progress reports, error explanations and test instructions.
- Everything else is **English**: source code, identifiers, comments, commit messages, files in `docs/`, UI strings. The one exception is `docs/README_RU.md`, a short Russian how-to for the user.
- The user is a guitarist, not a programmer. He cannot build locally and does not read code. Explain in plain Russian, define each DSP term in one sentence at first use (for example: true peak = the real waveform peak between samples), and prefer short lists and tables over code in chat. Paste code into chat only when asked.
- Keep every message short: what was done, what to test, what comes next.

## Workflow

1. One milestone at a time (section 11 of the spec). A milestone ends with: a green GitHub Actions run, a downloadable zip with the plugin, and a Russian test checklist of at most 10 items for REAPER. Then wait for feedback.
2. The spec is the source of truth. Record every deviation and its reason in `docs/DECISIONS.md`. If something is ambiguous, apply the default from section 12 of the spec, tell the user in one Russian sentence, and continue. Ask first only when a wrong guess is costly: architecture, parameter IDs, file formats, licences.
3. Never claim that something works unless a test or a CI run proved it. If a result can only be judged by ear or in REAPER, say so and put it in the checklist.
4. Do not add features that are not in the spec. Propose them at the end of a milestone.
5. Parameter IDs and the saved-state format are frozen after M1. A breaking change needs a migration and the user's approval.

## Visual changes: mockup first

Any change that affects how the plugin looks follows this protocol. That means a new screen or panel, layout, colours, fonts, the look of controls and graphs, icons, window sizes, resize behaviour and animations.

1. Draw a **mockup first**: an image or an HTML preview, no plugin code. Show it at 100 percent (1280 x 800), at 120 percent (1536 x 960) and at one compact size whenever the layout changes with size. Include the states that matter (for example monitoring on, a traffic light in each colour).
2. Send the mockup to the user in Russian with a short description of what changes, and ask for approval.
3. **Wait for explicit approval.** If the user asks for changes, draw a new version of the mockup. Write no UI code in the meantime.
4. Only after approval write the UI code, matching the approved mockup. Save every approved mockup in `docs/mockups/` with a version and a date. Any later deviation needs a new mockup and a new approval.

A pure bug fix that restores the approved appearance needs no mockup. Work that does not change the look (DSP, analysis, files, tests) is not affected.

## Build and delivery

- The user cannot build the plugin. All builds run in GitHub Actions on `windows-latest` (MSVC, Release, x64) and produce an artifact `MasterMind-<version>-win64-vst3.zip` containing the single .vst3 file and `README_RU.md` (details in section 3 of the spec).
- The product is a single `.vst3` file with all assets embedded and the C++ runtime linked statically. Nothing else is installed.
- Tell the user in Russian how to download the artifact from the Actions run, where to copy the plugin (`C:\Program Files\Common Files\VST3\`) and how to rescan plugins in REAPER (Preferences, Plug-ins, VST, Re-scan).
- Version scheme `0.<milestone>.<build>`, shown in the plugin footer together with the git commit hash.

## Real-time rules (non-negotiable)

No heap allocation, locks, file or network I/O, logging or exceptions on the audio thread. The UI and background analysis talk to the audio thread only through lock-free FIFOs and atomics. Use `juce::ScopedNoDenormals` in `processBlock`.

## Agent skills, if installed

If Claude Code skills for JUCE and DSP are available (for example `juce-plugin`, `dsp`, `juce-best-practices`, `juce-review`, `audio-artifacts-debug`), use them, and run the review skills after every DSP milestone. Do not depend on them: the spec must be enough on its own.
