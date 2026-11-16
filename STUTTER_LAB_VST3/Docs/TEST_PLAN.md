# STUTTER LAB 0.4.0 — QA test plan

## Build gate — Windows
- [ ] Windows 10/11 x64 configure with Visual Studio 2022 succeeds.
- [ ] JUCE 8.0.6 VST3 target compiles in Release.
- [ ] `StutterLabCoreTests` builds and CTest passes in the same CI run.
- [ ] VST3 bundle exists at `STUTTER_LAB_artefacts/Release/VST3/STUTTER LAB.vst3`.
- [ ] Plugin scans and opens in FL Studio 26 and Cubase 13.

## Capture and project state
- [ ] Mono input records dual-mono; stereo input preserves L/R.
- [ ] Capture starts/stops on audio blocks without interrupting host playback.
- [ ] 60-second limit stops safely; repeated takes contain no stale samples.
- [ ] Captured take is present after DAW save, close and reopen.
- [ ] Project restore works whether `setStateInformation()` occurs before or after `prepareToPlay()`.
- [ ] Malformed/truncated/oversized state chunks fail safely.

## Selection and render
- [ ] Selection start/end clamp to the captured take.
- [ ] Full selection is available to the scan renderer; no silent truncation.
- [ ] Straight, dotted and triplet divisions match host BPM.
- [ ] Forward, reverse, ping-pong and deterministic random directions are correct.
- [ ] Swing, gate, humanize and seed produce expected, repeatable patterns.
- [ ] Render progress advances through DSP stages; cancellation during each stage is safe.
- [ ] A cancelled or stale render never replaces the last valid render.
- [ ] WAV is 24-bit PCM at the captured sample rate and opens in both hosts.

## Effects
- [ ] Pitch shift changes fundamental frequency while preserving render duration.
- [ ] Formant movement changes the spectral envelope independently of pitch; listen for phase-vocoder artefacts.
- [ ] Phone filter and drive operate only on the render.
- [ ] Delay division follows host tempo; ping-pong feedback remains bounded.
- [ ] Reverb remains finite and stable at high size/mix values.
- [ ] Combined effect peaks do not hard-clip the 24-bit export.

## Preview and DAW integration
- [ ] Preview crossfades in/out without clicks and does not alter capture.
- [ ] Preview loop on/off behaves correctly.
- [ ] External drag-out imports the exact rendered WAV in FL Studio 26.
- [ ] External drag-out imports the exact rendered WAV in Cubase 13.
- [ ] Save As export writes to the selected path and opens in both hosts.
- [ ] If a host rejects external drag-out, document it and retain export as the fallback.

## Parameters and UI
- [ ] Host automation read/write works for every APVTS parameter.
- [ ] Factory and user presets save/load correctly.
- [ ] A/B snapshots switch reliably.
- [ ] Undo works for parameter edits; add redo if exposed.
- [ ] Editor close/reopen does not lose parameter/capture state.
- [ ] Resize and DPI at 100%, 125%, 150% and 200% remain usable.

## Core tests executed in this environment

- GCC 14.2 / CMake 3.31: core test target compiled; CTest 1/1 passed; direct report 82,350 checks, 0 failures.
- Clang 17 / CMake 3.31: core test target compiled; CTest 1/1 passed.
- Tests cover rhythmic divisions, full selection, gate/swing, fades, seeded randomness, cancellation, pitch/formant, delay/reverb stability, WAV encoding, capture, preview and thread stress.

## Not run yet — mandatory before release

- [ ] MSVC 2022 build of the complete JUCE wrapper.
- [ ] FL Studio 26 and Cubase 13 plugin validation.
- [ ] Drag & drop in each host.
- [ ] State recall with large captures in each host.
- [ ] Host automation, A/B, Undo and preset tests in each host.
- [ ] CPU/memory stress at 44.1/48/88.2/96/192 kHz and 64–1024 sample buffers.

No release certification until all applicable Windows and DAW checks are recorded.
