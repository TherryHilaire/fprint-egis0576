# Upstream contribution — libfprint MR for egis0576

Target: https://gitlab.freedesktop.org/libfprint/libfprint
Related open MR: !571 ("Added driver for 1c7a:0576", Needs Work)

## Patch series (from `egis0576-submission` branch, base = master)

1. `0001` — egis0576: Add driver for EgisTec EH576 (1c7a:0576)
2. `0002` — meson: register egis0576 driver
3. `0003` — tests: fix foreach over dict when introspection is disabled
   (independent build fix; can land separately)

## Suggested MR description

---

**egis0576: Add support for EgisTec EH576 (1c7a:0576)**

Adds an FpImageDevice driver for the EgisTec EH576 image sensor
(Lenovo IdeaPad Flex 5 16ABR8 / model 82XY and similar), based on the
fp-eh0576 reverse-engineering effort by @Sprayxe and Animeshz's EH575
work, extended with live hardware verification on bcdDevice 15.72.

Key differences from MR !571, all verified with a libusb diagnostic
tool against the device:

* **Response layout**: responses are `SIGE REG VALUE STATUS [data...]`.
  They do NOT echo the command byte as previously assumed.
  Examples:
  - `60 00 00` → `00 aa 01` (read reg 0x00 = 0xaa, status OK)
  - `62 67 03` → `67 03 01 <3 data bytes>` (burst read, live noise)
* The poll check `buffer[6] & 0x01` tests the STATUS byte (always 0x01),
  i.e. it is a no-op — which explains why !571 proceeds to image capture
  on the first poll. Finger presence is decided from image content
  (variance + dark-portion heuristic after background calibration);
  no register exposes finger state (verified over 64 samples).
* **Capture quality gate**: frames yielding fewer than 10 NBIS minutiae
  are rejected as partial/blurred presses (retry prompt) instead of being
  added to the template. On a 35-frame dataset this separated crisp from
  unusable presses perfectly and fixes most flaky-matching reports
  (~1/20 acceptance rate reported for !571).
* Contrast normalization stretches the difference histogram between its
  2nd/98th percentile rather than absolute min/max (+78% within-region
  bozorth scores, zero additional false accepts at threshold 10).
* Sensor tuning register sweep confirmed the INIT table values are
  near-optimal; 0x24 >= 0x40 saturates the sensor.

Tested on Lenovo IdeaPad Flex 5 16ABR8 (82XY), kernel 7.x, fprintd 1.94.5:
enroll + verify work reliably across finger placements.

## How to submit

1. Fork https://gitlab.freedesktop.org/libfprint/libfprint
2. Push branch:
   ```
   cd ~/Work/libfprint-upstream
   git remote add fork git@gitlab.freedesktop.org:<user>/libfprint.git
   git push fork egis0576-submission
   ```
3. Open MR against master, paste the description above, reference !571
   and credit fp-eh0576 / EgisTec-EH575.

Alternatively (lighter touch): comment on existing !571 linking to
https://github.com/TherryHilaire/fprint-egis0576 and offer these fixes
to its author/maintainers.
