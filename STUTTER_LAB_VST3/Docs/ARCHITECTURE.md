# STUTTER LAB 0.4.0 — architecture

## Processing model

The live input path is a transparent pass-through unless the user explicitly auditions the generated preview. `processBlock()` reads host BPM and APVTS values, updates the input meter, writes capture samples into preallocated memory and mixes the preview using a bounded crossfade. Capture and preview publication are coordinated with atomics; the callback does not perform file I/O, resize buffers or wait on a mutex.

The capture engine owns a bounded 60-second stereo store. A non-realtime snapshot operation temporarily excludes the callback, stops an active take if necessary, and copies the requested range. Project-state save serialises the capture into a compressed payload; project-state restore can load before or after `prepareToPlay()`.

## Render model

`generate()` snapshots the selected source range and settings into an immutable job. A dedicated JUCE worker performs the render. A generation epoch prevents stale jobs from publishing after a newer render or cancellation request. The renderer reports progress through an atomic value. A successful render is written to a unique 24-bit PCM WAV, then published to the preview engine and UI. Failed/cancelled renders must not replace the last valid render.

`PreviewEngine` transfers immutable buffers to the audio thread through an atomic pointer hand-off. Retired buffers are reclaimed away from the callback. The preview crossfades between the live input and generated audio; preview does not alter the capture.

## DSP stages

1. Full-selection rhythmic slicing with straight, dotted and triplet divisions, scan, reverse, ping-pong, seeded random direction, swing, gate and humanize.
2. Offline phase-vocoder pitch shift with independent spectral-envelope/formant movement.
3. Offline phone band-limiting and saturation.
4. Tempo-synchronised stereo ping-pong delay.
5. Stereo four-comb reverb.
6. Headroom safeguard when cumulative effects exceed full scale.

All effect stages run on the render worker, not in `processBlock()`. Pitch/formant quality and CPU cost require listening and stress tests in the Windows plugin build.

## State and parameters

- `AudioProcessorValueTreeState` owns stable host-automatable parameter IDs.
- The plugin state blob currently uses version 6 and migrates earlier versions 1–5.
- The capture is stored as compressed stereo float data inside the DAW state chunk.
- User presets store the APVTS parameter tree as XML; factory presets are built in.
- A/B snapshots are in-memory parameter-tree copies. Undo is backed by the APVTS `UndoManager`.

## Main modules

- `PluginProcessor`: JUCE host adapter, APVTS, capture, worker lifecycle, state, presets, A/B and WAV publishing.
- `PluginEditor`: capture waveform, selection, render waveform/progress, effect controls, preset and output actions.
- `Source/Core/CaptureEngine.h`: bounded capture store and safe non-RT snapshots.
- `Source/Core/PreviewEngine.h`: lock-free preview buffer hand-off and audio-thread crossfade.
- `Source/Core/StutterEngine.h`: deterministic offline pattern and effects renderer.
- `Source/Core/SpectralShifter.h`: JUCE-free FFT and phase-vocoder pitch/formant stage.
- `Source/Core/WavEncoder.h`: 24-bit PCM and 32-bit float WAV byte encoder.
- `Tests/CoreTests.cpp`: JUCE-free DSP/capture/preview regression suite.

## Important limitation

The core regression suite does not compile the JUCE wrapper and cannot prove VST3 compatibility. The Windows MSVC build, FL Studio 26, Cubase 13, project-state recall and external drag/drop still require manual/CI validation.
