# 0003. Licence, naming and specification text

- Status: accepted
- Date: 2026-11-02

## Decision

1. **Licence: BSD-3-Clause** for all openPAAD code, matching GenAVB and the
   MCUXpresso SDK, so that commercial products can ship it without
   obligations beyond attribution. Contributions are accepted under the
   Developer Certificate of Origin (`Signed-off-by` on every commit).
2. **Name: openPAAD.** "PAAD" (pro audio AVB device) describes the class of
   device. The project does not use "Milan" in its name, logo or product
   claims: Milan is a trademark of Avnu Alliance, and openPAAD is neither
   affiliated with nor certified by Avnu. Documentation may say "designed to
   interoperate with Milan devices" and cite the specification by name.
3. **No specification text in the repository.** IEEE and Avnu documents are
   copyrighted. Code, comments, tests and the requirement matrix paraphrase
   and cite clause numbers. Numeric values (timer values, table figures) are
   facts and may appear.
4. **Third-party licences** stay with their files. `third_party/genavb` is
   BSD-3-Clause except `linux/modules` (GPL-2.0, Linux kernel modules), which
   openPAAD never builds.

## Consequences

A device built on openPAAD that wants the Milan logo must go through Avnu
certification on its own. GET_MILAN_INFO must report a certification
version of 0 until then (requirement CTL-030).
