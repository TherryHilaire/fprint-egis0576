# HARDWARE.md — EgisTec EH576 fingerprint reader identification

Machine: Lenovo IdeaPad **Flex** 5 16ABR8 (Lenovo model code `82XY`,
board `LNVNB161216`), AMD Cezanne platform, running Arch Linux,
kernel `7.1.8-arch1-3`.

## Device identity

| Property | Value | Status |
|---|---|---|
| Manufacturer | Egis Technology Inc. (aka LighTuning), USB strings say "EgisTec" | CONFIRMED |
| Model | **EgisTec EH576** | CONFIRMED |
| USB VID:PID | `1c7a:0576` | CONFIRMED (lsusb + sysfs + kernel log) |
| bcdDevice | 15.72 (0x1572) | CONFIRMED |
| Serial | `16D95730` | CONFIRMED |
| Bus | USB 2.0 high-speed (480 Mbps), xHCI `0000:04:00.4`, bus 003 port 3 | CONFIRMED |
| Device class | 0xFF/0x00/0x00 vendor-specific, 1 configuration, 1 interface | CONFIRMED |
| Power | bus-powered, 100 mA, remote wakeup capable | CONFIRMED |

## Endpoints (interface 0)

| EP | Dir | Type | MaxPacket | Interval | Role |
|---|---|---|---|---|---|
| 0x01 | OUT | Bulk | 512 | – | Commands (EGIS frames) |
| 0x82 | IN | Bulk | 512 | – | Responses + image data |
| 0x83 | IN | Interrupt | 16 | 8 ms | Unknown (unused by known drivers) |
| 0x84 | IN | Interrupt | 16 | 8 ms | Unknown (unused by known drivers) |

Status: endpoints CONFIRMED from descriptor. Roles of bulk EPs CONFIRMED by
existing driver work; interrupt EPs UNKNOWN.

## Driver binding

- No kernel driver claims interface 0 (`driver: none` in sysfs).
- Kernel logs clean enumeration, no errors.
- Correct architecture is userspace via libfprint/libusb (see below).

## Installed fingerprint stack (this machine)

- libfprint 1.94.100-1 (contains `egismoc` driver family, no `0576` entry)
- fprintd 1.94.5-2 → reports "No devices available"

## Existing support landscape (as of 2026-08)

| Source | State |
|---|---|
| Upstream libfprint master | NOT supported (no 0x0576 in any id_table) |
| libfprint MR !571 ("Added driver for 1c7a:0576") | OPEN, labels: *Needs Work*, *New Driver*; last updated 2026-07-13 |
| github.com/marcel-wrld/fp-eh0576 | Working WIP driver (LGPL-2.1) + Ghidra project of Lenovo Windows UMDF driver `EgisTouchFP0576.dll` + libusb research tools; author runs it daily on Fedora 43 |
| github.com/Animeshz/EgisTec-EH575 + libfprint MR !317 (EH575) | Closed/WIP; laid protocol groundwork; EH575/EH576 protocols closely related |
| linux-hardware.org | Confirms no kernel driver exists up to 6.x |
| python-validity (Synaptics) | Not applicable (different vendor) |

The egismoc match-on-chip family (0582–05a1) is a DIFFERENT device class
(stores templates on-chip, SDCP issues). EH576 is an image-based sensor
delivering raw 70×57 images — do not mix them.

## Sensor characteristics

- Image sensor, press type (not swipe): 70 × 57 px, 1 byte/px, 3990 B/frame
  — INFERRED from working implementation; TO VERIFY on our hardware.
- Small-area sensor: libfprint matching needs image post-processing
  (normalize → 2× upscale → pad into 256×256 white canvas) and a low
  bz3_threshold (~10) — INFERRED, known weak point (FRR/FAR tradeoff).

## Safety notes

- Known protocol only writes sensor control registers over USB (same as the
  Windows driver does on every boot). No firmware/EEPROM/NVRAM flashing is
  involved or planned.
- Interrupt EPs 0x83/0x84 remain unexplored; we will not send anything to
  them until understood.
