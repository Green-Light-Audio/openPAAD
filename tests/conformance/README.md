# ATDECC conformance

A controller built on [la_avdecc](https://github.com/L-Acoustics/avdecc),
the library Hive uses, checks two simulated devices (decision 0017).

```sh
cmake --preset conformance && cmake --build --preset conformance && ctest --preset conformance
```

`run.sh` puts both devices (`openpaad-sim --net`) on one end of a Linux veth
pair and the harness (`conformance.cpp`) on the other. The harness waits
for both entities, reads their models, then sends them commands: lock,
name, identify, clock source, audio mappings, bind and unbind. A second
controller checks that the clock source change is notified.

Each line of its report is one check and the requirement rows it covers:

```text
PASS   CTL-010                  SET_CLOCK_SOURCE to the stream input: Success.
info   CTL-011                  identify through the IDENTIFY control: ...
                                (matrix: CTL-011 partial)
```

A check fails the run (`FAIL`) only if every row it names is marked
`implemented` in `docs/requirements/matrix.csv`; otherwise it reports
(`info`). A passing check on a row that is not `implemented` says so, unless
it covers only part of that row. la_avdecc's own rating of each entity
(IEEE 1722.1, Milan, misbehaving) is reported too, with the reasons it
gives.

la_avdecc is LGPL-3.0: fetched by the `conformance` preset only, linked into
this test program only, never into firmware.
