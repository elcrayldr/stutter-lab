# STUTTER LAB — final validation checklist

This document distinguishes completed source work from tests that require the Windows JUCE toolchain or a DAW.

## Source implementation in this package

- [x] JUCE 8.0.6 / C++17 / CMake project definition.
- [x] Capture engine with mono-to-stereo handling, bounded 60-second buffer, stop/clear and sample-rate conversion.
- [x] Full selection snapshot and deterministic stutter renderer.
- [x] Background render worker, cooperative cancellation and progress reporting.
- [x] Unique 24-bit WAV render and preview hand-off.
- [x] Preview uses the same rendered samples as WAV export after its 4 ms click-protection ramp.
- [x] Delay and reverb tail extension preserves the rendered phrase rather than clearing it.
- [x] Capture waveform reads are serialized against the audio callback without adding a mutex to the realtime path.
- [x] Tempo-synchronised rhythmic divisions including dotted and triplet values.
- [x] Phase-vocoder pitch shift and independent spectral-envelope/formant shift.
- [x] Tempo-synchronised ping-pong delay, comb reverb, phone filter and saturation.
- [x] APVTS parameters for host automation.
- [x] Factory presets, user preset save/load, A/B state snapshots and UndoManager integration.
- [x] Compressed captured-audio state chunk for DAW project recall.
- [x] UI render progress and cancellation control.

## Tests executed in this environment

- [x] JUCE-free core target configured and compiled with GCC 14.2 / CMake 3.31.
- [x] CTest: 1/1 test target passed.
- [x] Direct test report: 82,349 checks, 0 failures.
- [x] Additional tests cover pitch-shift frequency movement, formant output stability, delay/reverb finite output and render progress completion.

## Still required before a public release

- [ ] Configure and compile the complete JUCE VST3 with Visual Studio 2022 / MSVC on Windows.
- [ ] Inspect the compiler output for JUCE 8.0.6 API differences and fix any errors.
- [ ] Load the VST3 in FL Studio 26 and Cubase 13.
- [ ] Verify drag-out import into each host (the host controls its own drop acceptance).
- [ ] Verify state recall after closing/reopening a project with a 60-second capture.
- [ ] Verify automation playback/write/read for every APVTS parameter.
- [ ] Verify A/B, Undo, factory/user presets, editor close/reopen and preset versioning.
- [ ] Verify 44.1, 48, 88.2, 96 and 192 kHz; mono/stereo; low and high buffer sizes.
- [ ] Stress-test large captures and render cancellation during pitch/formant, delay and reverb processing in FL Studio 26 and Cubase 13.
- [ ] Listen to pitch/formant at multiple intervals and confirm whether the experimental spectral implementation is suitable for release.
- [ ] Confirm JUCE licence tier and all third-party licence obligations for the product owner.

A successful core test is not a VST3 build result and is not a DAW compatibility result. Do not mark this plugin “release certified” until every Windows/host item above has been checked.
