# Milestone 0 — Audit of v0.2.1 (inspected from source, not from the handoff notes)

## Confirmed defects in 0.2.1
| # | Finding | Evidence | Severity |
|---|---------|----------|----------|
| 1 | Render ran synchronously on the UI thread (`renderToWav` called from button handlers) | PluginEditor.cpp `renderAndStartDrag`, `exportWav` | High |
| 2 | Only the first grid-length slice of the selection was used (`targetLength = min(sourceLength, gridSamples)`) — long selections were silently truncated | PluginProcessor.cpp `renderToWav` | High |
| 3 | `SpinLock` shared with the audio callback (capture, preview, waveform peaks); audio thread used `tryEnter` and could skip capture blocks, and `prepareToPlay` resized buffers under the same lock | PluginProcessor.cpp | High |
| 4 | Preview buffer was `makeCopyOf` (allocation) under a lock while the audio thread reads it | `renderToWav` | High |
| 5 | Fixed render filename `STUTTER_LAB_Render.wav`, overwritten on every render (DAW may still be importing it) | PluginEditor.cpp | High |
| 6 | RENDER + DRAG started an OS drag automatically from a button click | `renderAndStartDrag` | Medium |
| 7 | Division list lacked dotted/triplet values; rate index was 1-based and doubled as legacy state | `rateDivision` | Medium |
| 8 | No parameters registered with the host (no APVTS) — no automation; state was a hand-rolled binary blob, no capture persistence | `getStateInformation` | High |
| 9 | Fade was a linear 3 ms ramp, not equal-power; phone filter was a single one-pole HP+LP with `exp(-2*pi*f/sr)` coefficients | `renderToWav` | Medium |
| 10 | `getLastRenderFile()` returned an empty File whenever the lock was busy | PluginProcessor.cpp | Low |
| 11 | No unit tests; CI only built the VST3 (no validation) | workflows | Medium |

## Not verifiable here
Everything that needs a Windows toolchain, JUCE headers or a DAW: the JUCE layer of this milestone has **not been compiled**, and nothing has been loaded in FL Studio 26 or Cubase 13.
