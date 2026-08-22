# PROTOCOL.md — EgisTec EH576 USB protocol

Derived from: static analysis (Ghidra) of Lenovo's Windows UMDF driver
`EgisTouchFP0576.dll` by the fp-eh0576 project, prior EH575 work by Animeshz,
and the working libfprint MR !571 implementation.

**UPDATE 2026-08-22: verified live against our unit (bcdDevice 15.72) with
`tools/diagnostic.c` — see "Live verification" section at the bottom.**

Status legend: CONFIRMED (multiple independent sources or verified on our
hardware), INFERRED (single-source / plausible), UNKNOWN.

Cross-validated against the annotated Ghidra decompilation of the sibling
EH575 driver (see RESEARCH.md). Summary of confidence:

- EGIS/SIGE framing, cmds 0x60–0x64, burst res_len=N+7: CONFIRMED (2 sources)
- Poll finger-detect bit position: DISCREPANT between sources
  (marcel: resp[6]&0x01; EH575 dll: value at resp[5]) — must be settled by
  capturing our unit's actual responses before any driver logic trusts it.
- Presence of any checksum/CRC: none observed in either source (INFERRED absent).

## Transport

- Bulk OUT `0x01` commands, Bulk IN `0x82` responses, libusb, 10 s timeouts.
- Strict request→response pairing: every command gets exactly one response.
- No CRC/checksum observed in framing. INFERRED (absence noted in all sources;
  responses echo request bytes which act as implicit sanity check). TO VERIFY.

## Frame format

Request:
```
'E' 'G' 'I' 'S'  [CMD] [REG] [VALUE(s)...]
 45  47  49  53
```

Response (all except image data):
```
'S' 'I' 'G' 'E'  ...echoed CMD/REG/VALUE...
 53  49  47  45
```
Response header echo: INFERRED (from RE sources); exact payload layout per
command UNKNOWN until captured.

## Commands

| CMD | Meaning | Notes |
|---|---|---|
| 0x60 | Read register `[REG]` | req len 7, resp len 7 (9 for poll) |
| 0x61 | Write value to `[REG]` | req len 7, resp len 7 |
| 0x62 | Burst read N bytes from `[REG]` | N = VALUE; resp len = N+? |
| 0x63 | Burst write N bytes to `[REG]` | VALUE = [N][bytes...]; resp echoes full frame |
| 0x64 | Fetch image | req `45 47 49 53 64 0f 96` (3990 = 0x0F96); resp len 3990, raw pixel bytes, NO SIGE header |

## Session flow (as implemented by MR !571 driver)

1. INIT: replay 30 fixed packets (register config; includes one IMAGE_PACKET
   fetch mid-sequence to flush the buffer).
2. LOOP (REPEAT): replay 5 packets (sensor tuning / status regs).
3. POLL: cmd `60 00 00`; response byte[6] bit0 == 1 ⇒ finger present.
4. IMAGE: cmd `64 0f 96` → 3990-byte raw frame.
5. Back to LOOP; repeat poll.

POLL_COUNT max ≈3000 polls before giving up (per DLL analysis).

## Host-side image processing (required for usable match scores)

1. Background capture: frame while no finger present (variance < ~2.5²);
   used as reference.
2. Per-pixel diff(bg, img), contrast trim ±4, normalize to 0..255.
3. Ridge binarization hints: <150 dark, clamp <120→0, >190→255.
4. Finger-present heuristic: variance > ~3.2² AND dark portion > 5%.
5. Nearest-neighbor 2× upscale (140×114), centered on 256×256 white canvas.
6. bz3_threshold must be lowered to ~10 (security-relevant weakness).

All thresholds are empirical (tuned on other units) — TO RE-TUNE on ours.

## Open questions / verification checklist for our unit

- [x] Capture real traffic with our diagnostic tool; confirm INIT/REPEAT
      packet tables behave identically on EH576 bcdDevice 15.72. **DONE:
      INIT 30/30 OK, REPEAT stable, tables work verbatim.**
- [x] Confirm response header layout for each command type. **DONE (see
      Live verification).**
- [x] Determine purpose of interrupt EPs 0x83/0x84 (finger detect?). **Not
      needed: image-content detection works; interrupt EPs left unexplored.**
- [x] Verify poll byte position ([6] & 1) semantics. **DONE: MR !571's test
      is on the STATUS byte (always 0x01) — a no-op, NOT finger detect.
      Register VALUE does not encode finger presence either.**
- [x] Check whether any command differs between finger present/absent. **NO
      register-level difference observed; only image content differs.**
- [ ] Measure realistic FRR/FAR with our fingers; tune thresholds.

## Live verification (2026-08-22, tools/diagnostic.c, our unit)

### True response layout — CONFIRMED

Responses do NOT echo the CMD byte. Layout is:

```
[SIGE '53 49 47 45'][REG][VALUE or N][STATUS=0x01][data...]
```

Examples from live captures:

| Request | Response | Interpretation |
|---|---|---|
| `60 00 00` (read reg 00) | `00 aa 01` / `00 ab 01` | reg00 value aa/ab, status OK |
| `60 80 00` | `80 02 01` | read reg 80 = 0x02 |
| `60 50 00` | `50 82 01` | read reg 50 = 0x82 |
| `60 2d 00` | `2d 47 01` | read reg 2d = 0x47 |
| `61 10 fd` | `10 fd 01` | write echo + status |
| `63 01 02 0f 03` | `01 02 01 0f 03` | REG,N,status,data echo |
| `62 67 03` | `67 03 01 5d 6c 63` | burst read: last 3 bytes are LIVE sensor noise (vary per run) |
| `64 0f 96` | 3990 B raw frame | mid-init flush frame is all-zero |

### Finger presence — CONFIRMED mechanism

- Register polling does NOT reveal finger presence: poll responses were
  constant (`00 aa/ab 01`) across 32 samples with and without finger contact.
- Image-content heuristic WORKS and is the only working detection path:
  background variance 4.59 (< 6.25 threshold), finger frame variance 27.97,
  dark_portion 0.36 → capture succeeded, clean ridge image saved
  (`tools/frame_raw.pgm`, `tools/frame_normalized.pgm`).
- Consequence for driver design: after INIT+REPEAT, fetch frames directly
  and decide by variance/dark_portion (marcel's usb_research flow). The
  poll-bit gate in MR !571 is vestigial (always true) and must NOT be used
  as a finger-detect condition.
