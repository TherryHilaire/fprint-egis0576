# RESEARCH.md — Source inventory & findings for EgisTec EH576 (1c7a:0576)

Compiled 2026-08-21. Local copies of all sources are under `~/Work/`.

## Primary sources (local)

| Path | What it is |
|---|---|
| `~/Work/fp-eh0576/` | marcel-wrld's RE project: libfprint driver (`egis0576.{c,h}`), libusb test tool (`usb_research/`), Ghidra project of Lenovo `EgisTouchFP0576.dll` |
| `~/Work/libfprint-upstream/` | upstream libfprint master (cloned 2026-08-21); contains `egismoc` family + `egis0570`, NOT 0576 |
| `~/Work/egistec-eh575/` | Animeshz's EH575 effort archive incl. full Ghidra decompilation `findings/EgisTec-EH575/decompiled_source/ghidra/EgisTouchFP0575.c` (annotated) and `libfprint.patch` |

## Upstream tracking

- MR !571 "Added driver for 1c7a:0576": OPEN, labels `Needs Work`, `New Driver`
  (checked 2026-08-21). Review comments not readable anonymously
  (gitlab.freedesktop.org API 401s on notes/discussions; HTML behind Anubis).
  ACTION: open https://gitlab.freedesktop.org/libfprint/libfprint/-/merge_requests/571
  in a browser and save reviewer feedback — tells us what upstream wants changed.
- Predecessor MR !317 (EH575): closed WIP.
- libfprint issues #271/#272 (small-image processing improvements): still open,
  few comments → small sensors remain second-class; canvas-padding workaround
  is the pragmatic path today.
- Issue #418 (egis0570 matching problems): 55 comments; same vendor, same class
  of complaints (small image ⇒ weak matching). Expectation-setting for us.
- `data/autosuspend.hwdb` upstream lists `usb:v1C7Ap0576*` among known
  fingerprint readers WITHOUT drivers — upstream acknowledges the device exists.

## Cross-validation of protocol (two independent RE lineages)

EH575 decompiled `EgisTouchFP0575.c` (Animeshz annotations) vs EH576 packets
(marcel, from 0576 DLL):

| Aspect | EH575 decompilation says | EH576 tables say | Verdict |
|---|---|---|---|
| Request magic | `'E''G''I''S'` at buf start | `45 47 49 53` prefix | CONFIRMED |
| Response magic | 32-bit compare vs `0x45474953` = `'S''I''G''E'` little-endian | `53 49 47 45` echo | CONFIRMED |
| Read reg cmd | `0x60`, req len 7, resp len 7 | `0x60 …`, 7→7 | CONFIRMED |
| Write reg cmd | `0x61` "repeating sequence", 7→7 | `0x61 …`, 7→7 | CONFIRMED |
| Burst read | `0x62` (alt `0x70` when addr bit8 set — "never encountered"), resp len = N+7 | `62 67 03` → res_len 10 = 3+7 | CONFIRMED |
| Burst write | `0x63` "we're interested" | `0x63 …` echo-length responses | CONFIRMED |
| Image fetch | `0x64` "last sequence that asks for image" | `64 0f 96` → 3990 B raw | CONFIRMED (semantics), layout TO VERIFY |
| Poll finger bit | generic read returns value at **resp[5]** | finger bit tested at **resp[6]** (& 1) | DISCREPANCY — resolve by capture |
| Checksums | none seen in either source | none | INFERRED ABSENT |

The resp[5] vs resp[6] mismatch matters: either response layout is
`SIGE CMD REG VAL` (value idx 6) or `SIGE CMD VAL …` (idx 5), or firmware
variants differ. Our diagnostic capture must dump full poll responses.

## Windows driver stack anatomy (both models)

- UMDF USB driver: `%SystemRoot%\System32\drivers\UMDF\EgisTouchFP{0575,0576}.dll`
  (talks WinUSB to the device — no kernel .sys involved)
- WinBio engine/sensor DLLs in `WinBioPlugIns\` handle matching host-side.
  Implication: sensor is dumb (registers + raw frames); matching is host-side,
  consistent with an FpImageDevice design.

## Known weaknesses / expectations

- Sensor frame is tiny (70×57). Host-side post-processing needed:
  normalize → 2× upscale → center in 256×256 white canvas; bz3_threshold ≈ 10
  (LOW ⇒ convenience/security tradeoff). Same pain reported for egis0570 (#418).
- marcel self-reports working daily use on Fedora 43 with occasional FRR.
- EH576 is press (not swipe); EH575 patch treated its sensor as swipe strips —
  approaches differ; we follow the press model.

## Open items

1. Read MR !571 review thread from a logged-in browser; extract requested changes.
2. Capture traffic from OUR unit (diagnostic tool) — verify INIT/REPLAY packet
   tables, poll byte offset, response layouts, interrupt EP behavior.
3. Obtain `EgisTouchFP0576.dll` (Lenovo download page, browser required) for
   optional third-source static analysis.
4. Tune thresholds empirically for this unit; measure FRR before trusting it.
