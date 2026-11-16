# CRAY LDR — STUTTER LAB

Capture-based vocal stutter, pitch/formant and ambience processor. Windows x64 VST3, JUCE 8.0.6, C++17, CMake.

## Product workflow

`CAPTURE → SELECT → GENERATE → PREVIEW → DRAG TO DAW / EXPORT WAV`

- Captures mono or stereo input for up to 60 seconds.
- Stores the captured take in the DAW project state using a compressed payload.
- Displays source and generated waveforms; drag across the source waveform to select a region.
- Generates on a background worker, reports progress and supports cancellation.
- Processes the full selected region using repeat scanning, rhythmic divisions, reverse, ping-pong, seeded random direction, swing, gate and humanize.
- Offline pitch shift and independent spectral-envelope/formant shift.
- Tempo-synchronised stereo ping-pong delay, comb reverb, phone filtering and drive saturation.
- Preview and WAV export use the same completed render buffer. Preview uses a 4 ms transition ramp to avoid clicks, then reproduces the render samples without additional DSP.
- The editor uses a source waveform, a separate render waveform, a dedicated rhythm panel and a tone/space panel. Capture waveform analysis runs after capture stops so it cannot steal blocks from an active take.
- Pitch/formant remain optional experimental spectral tools; they are not enabled in the default or reverse factory preset.
- Generates unique 24-bit PCM WAV files, supports external file drag-out and Save As export.
- Host-automatable parameters through `AudioProcessorValueTreeState`.
- Five factory presets, user preset save/load, A/B snapshots and Undo.

## Build requirements

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.22+
- Git and internet access for the first JUCE FetchContent configure

From PowerShell in the repository root:

```powershell
cmake -S STUTTER_LAB_VST3 -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DSTUTTERLAB_BUILD_TESTS=ON
cmake --build build --config Release --target STUTTER_LAB_VST3 StutterLabCoreTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

Expected VST3 bundle:

`build\STUTTER_LAB_artefacts\Release\VST3\STUTTER LAB.vst3`

Copy the complete `.vst3` bundle to:

`C:\Program Files\Common Files\VST3\`

Then rescan in FL Studio 26 or Cubase 13.

## Automated tests

The JUCE-free DSP/capture/preview test target can also be built independently on Linux or Windows:

```bash
cmake -S STUTTER_LAB_VST3/Tests -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The test suite covers rhythmic divisions, full-selection scanning, gate/swing, fades, deterministic randomisation, cancellation, pitch/formant processing, delay/reverb tails, WAV encoding, capture persistence primitives, exact preview/render sample equality after the safety ramp, 192 kHz processing and concurrent capture/preview stress.

## GitHub Actions

`.github/workflows/windows-vst3.yml` builds the Windows x64 VST3, runs the JUCE-free core tests, verifies the bundle and uploads both the VST3 and a complete source handoff artifact.

## Validation status

Do not treat a successful core test as proof that the JUCE wrapper or VST3 host integration works. The JUCE plugin must still pass the Windows MSVC build and manual validation in FL Studio 26 and Cubase 13. In particular, verify project-state recall with a captured take, drag-out from both hosts, parameter automation, undo/A-B, preset recall, mono/stereo buses, sample-rate changes and editor close/reopen.

See `Docs/FINAL_VALIDATION_STATUS.md` and `LICENSING.md` before distribution.
