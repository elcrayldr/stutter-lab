# CRAY LDR — STUTTER LAB 0.4.0

Windows x64 VST3 audio plugin built with JUCE 8.0.6 and C++17.

**Workflow:** Capture → Select → Generate → Preview → Drag to DAW / Export WAV.

This repository contains the JUCE plugin source, the JUCE-free DSP/capture/preview core, automated tests, a Windows build workflow and a PowerShell build helper.

## Start here

- [Plugin build and installation](STUTTER_LAB_VST3/README.md)
- [Architecture](STUTTER_LAB_VST3/Docs/ARCHITECTURE.md)
- [QA and release checklist](STUTTER_LAB_VST3/Docs/FINAL_VALIDATION_STATUS.md)
- [Detailed host test plan](STUTTER_LAB_VST3/Docs/TEST_PLAN.md)
- [JUCE licensing review](LICENSING.md)
- [Changelog](STUTTER_LAB_VST3/CHANGELOG.md)

## Build on Windows

Run PowerShell from the repository root:

```powershell
.\scripts\build-windows.ps1
```

For an optional install into the system VST3 folder, run PowerShell as Administrator:

```powershell
.\scripts\build-windows.ps1 -InstallVST3
```

The script configures Visual Studio 2022 x64, builds the VST3 and core tests, runs CTest and verifies the `.vst3` bundle.

## Current validation boundary

The JUCE-free core tests pass with GCC 14.2 and Clang 17 in the development environment. The complete JUCE wrapper still requires a Windows MSVC build, followed by manual tests in FL Studio 26 and Cubase 13. Do not consider the project release-certified until those checks pass. See the final validation status document.

## Licensing

JUCE is dual-licensed under AGPLv3 or a commercial JUCE licence. The product owner must choose and comply with the correct licence route before distributing a closed-source commercial binary. See [LICENSING.md](LICENSING.md).
