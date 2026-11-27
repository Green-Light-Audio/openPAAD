# Contributing to openPAAD

Thank you for helping. A few rules keep the project healthy and legally
clean.

## Sign-off (DCO)

Contributions from outside the maintainers carry a `Signed-off-by:` line on
every commit (`git commit -s`), certifying the
[Developer Certificate of Origin](https://developercertificate.org/): you
wrote the change or have the right to submit it under BSD-3-Clause.

## Specification text

Never paste text, tables or figures from the IEEE 802.1/1722 or Avnu Milan
specifications into code, comments, tests, issues or docs. Paraphrase and
cite the clause number. Numeric values (timer values, table figures) are
fine.

## Changes to `third_party/genavb`

- Keep them minimal; prefer hooks in `paad/`, `hal/` or `boards/`.
- One concern per commit, subject starting with `genavb:`.
- Add the change to the list in `third_party/README.md`.

## Code

- C11 for openPAAD code, C99 inside GenAVB. Linux kernel style (tabs), as
  in GenAVB; `.clang-format` is provided.
- openPAAD code builds with `-Wall -Wextra -Werror`.
- New behaviour comes with a test at the cheapest level that proves it:
  unit, simulation, fuzz, hardware-in-the-loop or interop.
- A change that affects a requirement updates its row in
  `docs/requirements/matrix.csv`.

## Before opening a pull request

```sh
scripts/ci.sh        # same steps as CI: host build and tests, firmware build
```

## Reporting security issues

See [SECURITY.md](SECURITY.md).
