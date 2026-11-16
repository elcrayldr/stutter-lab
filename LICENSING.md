# JUCE 8.0.6 licensing review — STUTTER LAB

**This is a technical project note, not legal advice.** Review the licence that applies to the actual JUCE 8.0.6 source used by the build and retain proof of the licence purchase/acceptance.

## JUCE is dual-licensed

JUCE is available under the AGPLv3 open-source licence or the commercial JUCE licence. If STUTTER LAB is to be distributed as a closed-source commercial VST3, the commercial JUCE route is the practical route unless the project owner deliberately chooses AGPLv3 and satisfies all of its obligations. Do not assume that merely publishing this repository on GitHub fulfils AGPL obligations.

Official references:

- [JUCE 8 End User Licence Agreement](https://juce.com/legal/juce-8-licence/)
- [JUCE licensing and current plan overview](https://juce.com/get-juce/)
- [JUCE framework repository licence information](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)

## JUCE 8 commercial tiers

The JUCE 8 EULA currently lists these licence types and limits (USD, before applicable taxes):

| Licence | Revenue/funding limit | Subscription | Perpetual |
|---|---:|---:|---:|
| Starter | Up to $20,000 in the previous 12 months | Free | Free |
| Indie | Up to $300,000 in the previous 12 months | $40/user/month, 1-month minimum | $800/user |
| Pro | No limit | $175/user/month, 12-month minimum | $3,500/user |
| Educational | No limit, restricted to qualifying educational use | Free | Free while eligibility remains |

The relevant revenue/funding calculation depends on whether the licensee is an individual or a legal entity. The EULA also places licensing responsibility on the owner of the product, requires a licence for each Framework User contributing to or modifying JUCE-dependent work, and sets conditions for continuing distribution when a subscription is cancelled or revenue limits are exceeded.

## Practical recommendation

1. Identify the legal owner of STUTTER LAB (individual or company) and calculate the relevant revenue/funding over the previous 12 months using the EULA definition.
2. If the owner is within the Starter threshold, confirm that the current Starter terms cover the intended commercial release and each developer seat. Starter is not an AGPL licence; it is a commercial JUCE licence tier with a revenue cap.
3. If the threshold is exceeded, select Indie or Pro before continuing distribution. The perpetual option is a one-time per-user fee for the applicable major version; a subscription requires the stated minimum commitment.
4. If choosing AGPLv3 instead, obtain legal advice on the source-disclosure, licensing, distribution and VST3 SDK interaction before releasing a closed-source binary. Do not combine code under incompatible licence terms.
5. Keep JUCE copyright notices and third-party notices intact. Do not redistribute the JUCE framework itself as a standalone library.

STUTTER LAB produces audio renders (WAV), not software plug-ins or JUCE projects for other people to compile. The JUCE EULA's clause about “Products that create Products” is primarily a concern for software/plugin generators; if the product scope changes to generating JUCE-based software, obtain written guidance from JUCE before distribution.

## Project code and dependency separation

- STUTTER LAB's own source files are separate from JUCE's framework source.
- JUCE is fetched at the pinned `8.0.6` tag by CMake and is not copied into this repository.
- The exact commercial or AGPL licence choice must be made by the product owner; the source tree alone does not prove compliance.
