# Mockups

Every visual change starts here (see `CLAUDE.md`, "Visual changes: mockup first"). Each folder holds one version with its date: an HTML source (`mockup.html`) and the PNG renders at 1280 x 800 (100%), 1536 x 960 (120%) and 640 x 400 (compact).

`mockup.html` is interactive when opened without a query string: a bar above the window switches views, size and monitoring, and every control shows its tooltip on hover. With a query string (`view`, `monitor`, `tip`, `knob`, `t`) it renders one static state for the PNGs.

| Version | Date | Status | What changed |
| --- | --- | --- | --- |
| [m1-main-window-v6](m1-main-window-v6-2026-11-26/) | 2026-11-26 | Proposed, awaiting approval | v5 plus: every threshold knob has an outer ring with the live signal (green below the threshold, red above it); graphical Saturation editor (band graph with drive and crossover handles, type list and transfer curve per band); `view=knobs` shows the ring states |
| [m1-main-window-v5](m1-main-window-v5-2026-11-26/) | 2026-11-26 | Superseded by v6 | Master workspace: smaller spectrum, module bar with a full module editor (no double-click panel), balance column with tonal balance, loudness and checks |
| [m1-main-window-v4](m1-main-window-v4-2026-11-26/) | 2026-11-26 | Approved, implemented in 0.7 | Russian tooltips with recommendations on every control (D-011) |
| [m1-main-window-v3](m1-main-window-v3-2026-11-26/) | 2026-11-26 | Superseded by v4 | Smaller spectrum; all ten modules in two rows, no scrolling |
| [m1-main-window-v2](m1-main-window-v2-2026-11-26/) | 2026-11-26 | Superseded by v3 | Knob tooltips; interactive mockup |
| [m1-main-window-v1](m1-main-window-v1-2026-11-26/) | 2026-11-26 | Superseded by v2 | First main window: top bar, Master, Eyes, compact mode, monitoring state |

Any deviation from the approved version in plugin code needs a new mockup version and a new approval.
