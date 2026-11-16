# CLAUDE CODE — RELEASE AUDIT AND COMPLETION BRIEF
## CRAY LDR / STUTTER LAB — JUCE 8.0.6 VST3, source version 0.4.0

You are auditing and finishing an existing C++ VST3 project. The code in this package is the working source. Do not replace it with a mock-up, a web app, a standalone-only prototype or a fresh empty JUCE project. Preserve the CRAY LDR / STUTTER LAB identity and the existing capture → select → generate → preview → drag/export workflow.

## 1. Target product

- Windows 10/11 x64 VST3.
- JUCE 8.0.6, C++17, CMake, Visual Studio 2022 / MSVC.
- Primary hosts: FL Studio 26 and Cubase 13.
- User workflow: CAPTURE → SELECT → GENERATE → PREVIEW → DRAG TO DAW / EXPORT WAV.
- Audio capture must survive DAW project save/close/reopen.
- Rendering must stay off the audio and UI threads, report real progress, support cancellation and preserve the original capture.

## 2. Current 0.4.0 implementation to audit

The package already contains:
- Mono/stereo capture with a 60-second cap, sample-rate conversion, stop/clear and compressed captured-take persistence in the plugin state chunk.
- Source waveform and range selection.
- Full-selection stutter generation, straight/dotted/triplet divisions, scan, reverse, ping-pong, seeded random direction, swing, gate and humanize.
- Background render worker, progress, cancellation, preview hand-off and crossfade.
- Unique 24-bit PCM WAV render, drag-out and Save As export.
- Offline phase-vocoder pitch shift and independent spectral-envelope/formant shift.
- Tempo-synchronised ping-pong delay, comb reverb, phone filtering, saturation and a render headroom safeguard when effects are active.
- APVTS host parameters, factory/user presets, A/B snapshots and UndoManager integration.
- Vintage hardware-inspired olive/brass/ivory UI styling.
- JUCE-free DSP/capture/preview tests.

Do not assume a feature is production-ready because it exists in source. Audit its implementation, thread safety, state migration, sound quality and real host behaviour.

## 3. Immediate required work — Windows build

1. Configure using Visual Studio 2022 x64 and JUCE 8.0.6.
2. Build `STUTTER_LAB_VST3` and `StutterLabCoreTests` in Release.
3. Run `ctest --test-dir build -C Release --output-on-failure`.
4. Fix all compiler warnings that reveal real defects and all compile/link errors. In particular, verify JUCE 8 APIs (`FontOptions`, GZIP streams, APVTS attachments, FileChooser async callbacks, parameter layout, VST3 target names).
5. Confirm the generated bundle exists at `STUTTER_LAB_artefacts/Release/VST3/STUTTER LAB.vst3` and upload it as a GitHub Actions artifact.
6. Do not silently downgrade JUCE or change the plugin format to make a build pass.

## 4. Audio/DSP audit

- Confirm `processBlock()` performs no allocation, file I/O, blocking lock or buffer resize.
- Confirm capture/preview buffer ownership and hand-off cannot free memory while the audio thread reads it.
- Stress render cancellation during stutter slicing, pitch/formant, delay and reverb.
- Check phase-vocoder artefacts, stereo stability, transient smearing, formant movement and CPU cost at 44.1/48/88.2/96/192 kHz.
- Verify delay divisions track host BPM and never allocate in the audio callback (all effects are offline render stages).
- Confirm output does not hard-clip after effect summing; preserve headroom and report peak accurately.
- Ensure a failed/cancelled render never replaces the last valid render.
- Test long captures and selections up to the 60-second capture limit.

Do not describe the formant shifter as transparent or artefact-free without listening tests. If the current spectral-envelope method is not good enough, improve it with a credible spectral-envelope method and add objective tests.

## 5. Host integration tests — mandatory

Install the built VST3 and test in both FL Studio 26 and Cubase 13:
- Plugin scan/load, open/close editor, project save/reopen.
- Capture, stop, clear, recapture, selection, generate, preview, stop preview, cancel render, export WAV.
- Drag the generated WAV from the plugin into the DAW arrangement/pool. If a host rejects external drag-out, keep Save As as the guaranteed fallback and document the host limitation.
- Save a project with a captured take, close the DAW, reopen it and verify the capture and settings are restored.
- Test host automation read/write for every APVTS parameter.
- Test factory/user presets, A/B and Undo.
- Test mono/stereo buses, host tempo changes and sample-rate changes.

Record host versions, Windows version, sample rate, buffer size, test result and any host-specific limitation. Never claim drag-and-drop works without an actual import test in each host.

## 6. Interface requirements

Keep the original, premium vintage studio hardware direction: deep graphite, muted olive, aged brass/champagne and warm ivory. It may evoke classic British recording hardware, but must not copy Waves/Abbey Road logos, exact layouts, artwork, fonts or trade dress. Prioritise clear hierarchy, readable labels, tactile controls, useful waveform editing, genuine meters and robust DPI scaling. Test 100%, 125%, 150% and 200% scaling.

## 7. Presets, state and automation

- Verify project state migration from versions 1–6.
- Verify capture audio persists safely and malformed state data cannot trigger excessive allocations.
- Confirm all exposed parameters have stable IDs and ranges.
- Confirm preset save/load, A/B switching and Undo/redo behaviour. Add redo if the UX exposes it.
- Do not break older sessions when adding new parameters; migrate defaults deliberately.

## 8. Licensing gate

Read `LICENSING.md` and the official JUCE 8 EULA. The project uses JUCE 8.0.6, which is dual-licensed under AGPLv3 or the commercial JUCE licence. The product owner must select and document the correct route, revenue tier and developer seats before selling/distributing the plugin. Do not provide legal assurances; flag uncertainty for legal review.

## 9. GitHub and delivery

- Work in a dedicated branch and use small, reviewable commits.
- Keep the Windows build/test workflow green.
- Upload the VST3 bundle and full source package as workflow artifacts.
- Update README, CHANGELOG, licensing notes and final validation status.
- Report exact commit SHA, workflow run, artifact names, build result, test result and outstanding manual host tests.
- Do not mark the project release-certified until MSVC and both DAW host tests pass.
