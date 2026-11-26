# MasterMind

A Windows stereo VST3 master-bus plugin for modern metal: mastering processing combined with objective metering. By desNotto.

- Specification: [docs/SPEC.md](docs/SPEC.md)
- Decisions and deviations: [docs/DECISIONS.md](docs/DECISIONS.md)
- Russian install guide: [docs/README_RU.md](docs/README_RU.md)

Builds run in GitHub Actions (`.github/workflows/build-windows.yml`): JUCE 8 + CMake, MSVC Release x64, unit tests, a binary null test of the shipped file and pluginval. The artifact `MasterMind-<version>-win64-vst3.zip` contains the single `MasterMind.vst3` file and `README_RU.md`.

Local build (Windows, Visual Studio 2022 or newer, CMake 3.22+):

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```
