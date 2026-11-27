# Mockups

Every visual change starts here (see `CLAUDE.md`, "Visual changes: mockup first"). Each folder holds one version with its date: an HTML source (`mockup.html`) and the PNG renders at 1280 x 800 (100%), 1536 x 960 (120%) and 640 x 400 (compact).

`mockup.html` is interactive when opened without a query string: a bar above the window switches views, size and monitoring, and every control shows its tooltip on hover. With a query string (`view`, `monitor`, `tip`, `knob`, `t`) it renders one static state for the PNGs.

| Version | Date | Status | What changed |
| --- | --- | --- | --- |
| [m1-main-window-v15](m1-main-window-v15-2026-11-27/) | 2026-11-27 | Proposed, awaiting approval | v14 plus: graphical Limiter (level over the last 6 s with the gain reduction strip and the ceiling, GR and true-peak meters, modes with pictures) |
| [m1-main-window-v14](m1-main-window-v14-2026-11-27/) | 2026-11-27 | Included in v15 | v13 plus: Clipper with Drive (up to +24 dB), waveform before and after with the cut peaks, transfer curve, modes with pictures; Output with a loudness target that sets the Clipper and the Limiter (`outrun=run|done`) |
| [m1-main-window-v13](m1-main-window-v13-2026-11-27/) | 2026-11-27 | Approved 2026-11-27, implemented in 0.7 | v12 plus: graphical Low-End editor (low spectrum with the mono zone and the rotator frequency, phase dial with the waveform before and after, translator harmonics), tooltip advice on every knob (18 new texts, `view=newtips`), knob tooltips without the module section |
| [m1-main-window-v12](m1-main-window-v12-2026-11-27/) | 2026-11-27 | Approved 2026-11-27, superseded by v13 | v11 plus: graphical Stereo editor (width per band vs your mix and the reference, vectorscope, correlation with the guard threshold) with tooltips; Saturation band Solo and Mute; Multiband tooltips with advice per band (`view=mbtips`) and whole-number values; Auto match over all ten modules |
| [m1-main-window-v11](m1-main-window-v11-2026-11-27/) | 2026-11-27 | Approved 2026-11-27, superseded by v12 | v10 plus: References tab (library folder, song list with slot 1-6 assignment, removal, drop zone), a guide panel on the Album tab, an experimental Auto match strip on Eyes (reference name, progress band by band, Stop, Undo) |
| [m1-main-window-v10](m1-main-window-v10-2026-11-27/) | 2026-11-27 | Approved with v11 | v9 without the Monitor block in the top bar (correction, system emulation, listen menu). A red MONITOR ON badge appears only while reference B or a band listen plays |
| [m1-main-window-v9](m1-main-window-v9-2026-11-26/) | 2026-11-26 | Approved 2026-11-27, superseded by v11 | v8 plus: the Dynamic EQ editor repeats the band panel (Freq with the note, Gain or Slope, Q, Snap, Delete as typed fields; On, type, placement and Dynamic work there too); EQ points stay inside the graph |
| [m1-main-window-v8](m1-main-window-v8-2026-11-26/) | 2026-11-26 | Superseded by v9 | v7 plus: EQ bands are edited on the spectrum (double-click adds a band, low cut at the left edge on band 1, high cut at the right edge on band 10, bell elsewhere; drag, wheel for Q, Alt+click deletes, double-click bypasses, right-click menu; floating band panel with typed frequency or note entry and note snap). The Master spectrum is 280 px high instead of 214 and the multiband crossover bar is slimmer, so every multiband knob still fits |
| [m1-main-window-v7](m1-main-window-v7-2026-11-26/) | 2026-11-26 | Superseded by v8 | v6 plus: a yellow or red tonal balance row (Master, Eyes, compact checks) shows an advice tooltip with the status, what you hear and two or three fix steps with MasterMind modules; `view=tbtips` shows all twelve texts |
| [m1-main-window-v6](m1-main-window-v6-2026-11-26/) | 2026-11-26 | Superseded by v7 | v5 plus: every threshold knob has an outer ring with the live signal (green below the threshold, red above it); graphical Saturation editor (band graph with drive and crossover handles, type list and transfer curve per band); `view=knobs` shows the ring states |
| [m1-main-window-v5](m1-main-window-v5-2026-11-26/) | 2026-11-26 | Superseded by v6 | Master workspace: smaller spectrum, module bar with a full module editor (no double-click panel), balance column with tonal balance, loudness and checks |
| [m1-main-window-v4](m1-main-window-v4-2026-11-26/) | 2026-11-26 | Approved, superseded by v9 | Russian tooltips with recommendations on every control (D-011) |
| [m1-main-window-v3](m1-main-window-v3-2026-11-26/) | 2026-11-26 | Superseded by v4 | Smaller spectrum; all ten modules in two rows, no scrolling |
| [m1-main-window-v2](m1-main-window-v2-2026-11-26/) | 2026-11-26 | Superseded by v3 | Knob tooltips; interactive mockup |
| [m1-main-window-v1](m1-main-window-v1-2026-11-26/) | 2026-11-26 | Superseded by v2 | First main window: top bar, Master, Eyes, compact mode, monitoring state |

Any deviation from the approved version in plugin code needs a new mockup version and a new approval.
