# Changelog

## 0.4.0 — expanded production workflow

- Added actual render progress and UI cancellation.
- Added phase-vocoder pitch shifting with independent spectral-envelope/formant movement.
- Added tempo-synchronised ping-pong delay and stereo comb reverb.
- Registered host-automatable parameters with APVTS.
- Added factory presets, user preset save/load, A/B snapshots and UndoManager integration.
- Added compressed capture persistence to the DAW state chunk.
- Refined the UI with a vintage hardware-inspired colour system, meter and control rack.
- Expanded core tests for pitch/formant, ambience and progress.

## 0.3.0 — Milestone A

- Moved render work to a background thread.
- Added full-selection processing, capture/preview engines, deterministic randomisation, unique WAV output and core stress tests.
