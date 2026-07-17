# Legal and takedown readiness

This document is project process, not legal advice. A qualified intellectual-property lawyer
should review the repository before a public playable release and before any counter-notice.

## What the license does and does not do

Apache-2.0 licenses only the project's original work. It supplies explicit copyright and patent
grants from contributors, patent-litigation termination, warranty disclaimers, liability limits,
and no general trademark permission. It does not license Sony/Bend material and cannot prevent a
third party from sending a copyright, circumvention, or trademark complaint.

Contributor commits require DCO 1.1 sign-off to preserve a durable representation that the
submitter had the right to contribute the work. This strengthens provenance; it is not immunity
from a claim.

## Preventive controls

1. Ship no BIOS, disc image, retail executable, overlay, save, key, extracted asset, or retail
   instruction translation.
2. Execute only independently written native host code. Keep PCSX2, MIPS disassembly, and retail
   modules in the offline research environment.
3. Require the user to supply lawfully obtained retail data. Validate identity locally by hash;
   never upload it to CI or telemetry.
4. Preserve evidence states, tool commands, hashes, independent implementations, and behavioral
   comparisons in reviewable history.
5. Use original branding (`OpenOmega`) for binaries and artwork. Use game/trademark names only to
   identify compatibility and display the affiliation disclaimer.
6. Run `python -B tools/check_public_tree.py` before every public push and in CI.
7. Require DCO sign-off and review any contribution touching reverse-engineering metadata.

## If GitHub receives a complaint

1. Preserve the complete notice, repository state, commit hashes, evidence ledger, and relevant
   private research logs. Do not publish private data while preserving it.
2. Do not argue facts publicly or delete history impulsively. Identify the exact files and legal
   theory alleged: copyright, trademark, private information, or circumvention.
3. Consult an IP lawyer promptly. GitHub's policy may provide roughly one business day to change
   specifically identified repository content, while whole-repository claims can result in faster
   disabling.
4. For a circumvention claim, request the detailed technical identification required by GitHub's
   policy and ask about its Developer Defense Fund referral.
5. Submit a counter-notice only on counsel's advice. It is a sworn legal statement, reveals full
   contact information to the claimant, consents to court jurisdiction, and may prompt a lawsuit.
6. If a narrow item crossed the clean-room boundary, remove it from all Git history and releases,
   document the remediation, and strengthen the automated gate.

## Interoperability law

In the United States, 17 U.S.C. §1201(f) describes a limited reverse-engineering exception for a
person who lawfully obtained the right to use a program and acts solely to identify elements
necessary for interoperability of an independently created program, subject to its conditions.
Whether a specific act qualifies is fact-dependent. The project does not treat that provision as
a blanket authorization or distribute circumvention tools.

## Primary references

- Apache-2.0 terms: https://www.apache.org/licenses/LICENSE-2.0
- Developer Certificate of Origin 1.1: https://developercertificate.org/
- 17 U.S.C. §1201: https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section1201
- GitHub DMCA policy: https://docs.github.com/en/site-policy/content-removal-policies/dmca-takedown-policy
- GitHub counter-notice guide: https://docs.github.com/en/site-policy/content-removal-policies/guide-to-submitting-a-dmca-counter-notice
- GitHub content-removal categories: https://docs.github.com/en/site-policy/content-removal-policies/submitting-content-removal-requests
