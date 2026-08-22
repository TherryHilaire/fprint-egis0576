# fprint-egis0576 — Linux driver work for the EgisTec EH576 fingerprint reader

Community effort to support the **EgisTec EH576** (`USB 1c7a:0576`) fingerprint
reader — found in Lenovo IdeaPad/Flex 5 16ABR8 (model 82XY) and similar
laptops — under libfprint/fprintd on Linux.

## Status

**Working.** Enroll + verify through fprintd function reliably, including a
capture-quality gate that rejects partial or blurred presses (the main cause
of flaky matching reported with earlier attempts, e.g. MR !571).

## What's here

| Path | Contents |
|---|---|
| `HARDWARE.md` | Device identification: USB descriptors, endpoints, prior art |
| `PROTOCOL.md` | Full EGIS/SIGE USB protocol, incl. live-verified response layout |
| `RESEARCH.md` | Source inventory and cross-validation of RE lineages |
| `driver/egis0576.{c,h}` | libfprint FpImageDevice driver (LGPL-2.1), based on Marcel's fp-eh0576 port with our verified fixes |
| `tools/diagnostic.c` | Standalone libusb tool: protocol dumps, finger-bit analysis, frame capture, dataset collection |
| `tools/score-harness.c` | Pairwise NBIS/bozorth3 score matrix over a captured frame dataset |

## Key findings (verified live)

* Responses are `[SIGE][REG][VALUE/N][STATUS][data…]` — they do **not**
  echo the command byte, contrary to what MR !571 assumed.
* MR !571's "finger present" check (`resp[6] & 1`) actually tests the STATUS
  byte (always `0x01`); it is a no-op.
* Finger presence cannot be read from registers; it must be decided from
  image content (variance / dark-portion heuristic).
* On this sensor, crisp presses yield ≥10 NBIS minutiae; partial/blurred
  presses <10. Gating captures on minutiae count eliminates most
  verification failures.

## Using the driver

Build libfprint (upstream master) with `driver/egis0576.{c,h}` added to
`libfprint/drivers/`, registered in `libfprint/meson.build` (`driver_sources`)
and top-level `meson.build` (`drivers_info`):

```
meson setup build -Ddrivers=default -Dintrospection=false -Ddoc=false
ninja -C build && sudo ninja -C build install && sudo ldconfig
sudo systemctl restart fprintd.service
fprintd-enroll && fprintd-verify
```

Note: installing to `/usr/local` shadows the distribution libfprint without
touching pacman/rpm/deb-managed files.

## Tools

```
# protocol diagnostics (needs root for direct USB access)
cd tools && gcc -O2 -o diagnostic diagnostic.c -lusb-1.0 -lpthread
sudo ./diagnostic info|init|poll|capture|dataset

# match-quality analysis over a dataset captured with `diagnostic dataset`
score-harness <dataset-dir> [scale]
```

## Tips for reliable matching

The EH576 window is tiny (70×57 px ≈ 5×4 mm) — it sees a *patch* of your
finger, never the whole print. That has practical consequences:

* **Enroll with full, firm presses.** Cover the whole window and lift your
  finger completely between presses. The driver rejects partial/blurred
  captures with a "center your finger" retry — those would otherwise end up
  as junk entries in your print template.
* **One region per enrollment is fine.** Matching works best when the verify
  press overlaps what was enrolled. If you want broader coverage, enroll the
  *same physical finger* twice under two names (e.g. right-index and
  right-middle) pressing different regions; authentication checks all of
  your enrolled prints.
* Expect the occasional retry. A ~90% first-try accept rate on random-ish
  placements is normal for this sensor class; the retry loop handles the
  rest.

## Credits

* [Marcel (Sprayxe) / fp-eh0576](https://github.com/marcel-wrld/fp-eh0576) —
  Ghidra analysis of the Windows UMDF driver, packet tables, initial
  libfprint port (LGPL-2.1)
* [Animeshz / EgisTec-EH575](https://github.com/Animeshz/EgisTec-EH575) —
  EH575 decompilation groundwork, libfprint MR !317
* Upstream [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint)
  and open MR !571

## License

Driver sources: LGPL-2.1 (inherited from upstream libfprint and fp-eh0576).
Documentation and tools: same license for simplicity.
