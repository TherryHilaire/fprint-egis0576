#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <libusb-1.0/libusb.h>

#define VID 0x1c7a
#define PID 0x0576
#define IID 0
#define EPOUT 0x01
#define EPIN 0x82
#define EPINTR1 0x83
#define EPINTR2 0x84
#define TIMEOUT_MS 10000
#define INTR_TIMEOUT_MS 200
#define POLL_COUNT_MAX 3000

#define IMG_WIDTH 70
#define IMG_HEIGHT 57
#define IMG_SIZE (IMG_WIDTH * IMG_HEIGHT)

#define BG_VARIANCE (2.5 * 2.5)
#define FINGER_VARIANCE (3.2 * 3.2)
#define CONTRAST 4
#define DARK_PORTION_MIN 0.05

#define RESP_BUF_SIZE 8192
#define LOG_PATH "diagnostic.log"

static libusb_context *ctx = NULL;
static libusb_device_handle *dev = NULL;
static FILE *logf = NULL;
static volatile sig_atomic_t intr_thread_run = true;

static const unsigned char POLL_PKT[] = { 0x45, 0x47, 0x49, 0x53, 0x60, 0x00, 0x00 };
static const unsigned char IMAGE_PKT[] = { 0x45, 0x47, 0x49, 0x53, 0x64, 0x0f, 0x96 };

#define INIT_PACKETS_COUNT 30
static const unsigned char *INIT_PKTS[INIT_PACKETS_COUNT] = {
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x00, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x01, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x10, 0xfd },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x35, 0x02 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x80, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x80, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x10, 0xfc },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x01, 0x02, 0x0f, 0x03 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x0c, 0x22 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x09, 0x83 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x26, 0x06, 0x06, 0x60, 0x06, 0x05, 0x2f,
                           0x06 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x10, 0xf4 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x0c, 0x44 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x50, 0x03 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x50, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x64, 0x0f, 0x96 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x40, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x09, 0x0b, 0x83, 0x24, 0x00, 0x44, 0x0f,
                           0x08, 0x20, 0x20, 0x00, 0x00, 0x52 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x26, 0x06, 0x06, 0x60, 0x06, 0x05, 0x2f,
                           0x06 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x23, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x24, 0x38 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x20, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x61, 0x21, 0x45 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x00, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x01, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x2c, 0x02, 0x00, 0x57 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x2d, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x62, 0x67, 0x03 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x0f, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x2c, 0x02, 0x00, 0x13 },
};
static const int INIT_PKT_LENS[INIT_PACKETS_COUNT] = { 7, 7, 7, 7, 7, 7, 7, 9, 7, 7, 13, 7,
                                                       7, 7, 7, 7, 7, 18, 13, 7, 7, 7, 7, 7,
                                                       7, 9, 7, 7, 7, 9 };
static const int INIT_PKT_RESP_LENS[INIT_PACKETS_COUNT] = { 7, 7, 7, 7, 7, 7, 7, 9, 7, 7, 13,
                                                            7, 7, 7, 7, IMG_SIZE, 7, 18, 13,
                                                            7, 7, 7, 7, 7, 7, 9, 7, 10, 7,
                                                            9 };

#define REPEAT_PACKETS_COUNT 5
static const unsigned char *REPEAT_PKTS[REPEAT_PACKETS_COUNT] = {
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x2c, 0x02, 0x00, 0x57 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x2d, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x62, 0x67, 0x03 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x60, 0x0f, 0x00 },
  (const unsigned char[]){ 0x45, 0x47, 0x49, 0x53, 0x63, 0x2c, 0x02, 0x00, 0x13 },
};
static const int REPEAT_PKT_LENS[REPEAT_PACKETS_COUNT] = { 9, 7, 7, 7, 9 };
static const int REPEAT_PKT_RESP_LENS[REPEAT_PACKETS_COUNT] = { 9, 7, 10, 7, 9 };

_Static_assert (sizeof (INIT_PKTS) / sizeof (INIT_PKTS[0]) == INIT_PACKETS_COUNT,
                "INIT_PKTS count mismatch");
_Static_assert (sizeof (INIT_PKT_LENS) / sizeof (INIT_PKT_LENS[0]) == INIT_PACKETS_COUNT,
                "INIT_PKT_LENS count mismatch");
_Static_assert (sizeof (INIT_PKT_RESP_LENS) / sizeof (INIT_PKT_RESP_LENS[0])
                    == INIT_PACKETS_COUNT,
                "INIT_PKT_RESP_LENS count mismatch");
_Static_assert (sizeof (REPEAT_PKTS) / sizeof (REPEAT_PKTS[0]) == REPEAT_PACKETS_COUNT,
                "REPEAT_PKTS count mismatch");
_Static_assert (sizeof (REPEAT_PKT_LENS) / sizeof (REPEAT_PKT_LENS[0]) == REPEAT_PACKETS_COUNT,
                "REPEAT_PKT_LENS count mismatch");

static void
log_printf (const char *fmt, ...)
{
  va_list ap, ap_copy;
  va_start (ap, fmt);
  va_copy (ap_copy, ap);
  vprintf (fmt, ap);
  vfprintf (logf, fmt, ap_copy);
  va_end (ap_copy);
  va_end (ap);
  fflush (stdout);
  fflush (logf);
}

static void
hexdump (const char *label, const unsigned char *buf, int len)
{
  if (len > 256)
    {
      log_printf ("%s (%d bytes, head/tail only):\n", label, len);
      hexdump ("  head", buf, 32);
      hexdump ("  tail", buf + len - 16, 16);
      return;
    }
  log_printf ("%s (%d bytes):", label, len);
  for (int i = 0; i < len; i++)
    {
      if (i % 16 == 0)
        log_printf ("\n  %04x  ", i);
      log_printf ("%02x ", buf[i]);
    }
  log_printf ("\n");
}

static void
ts_now (char *out, size_t n)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  snprintf (out, n, "%ld.%03ld", ts.tv_sec, ts.tv_nsec / 1000000);
}

static int
send_recv (const unsigned char *cmd, int cmd_len, int expect_len,
           unsigned char *resp, int resp_buf_size, int *actual)
{
  (void) expect_len;
  int sent = 0;
  int r = libusb_bulk_transfer (dev, EPOUT, (unsigned char *) cmd, cmd_len, &sent, TIMEOUT_MS);
  if (r != LIBUSB_SUCCESS || sent != cmd_len)
    {
      log_printf ("!! OUT failed: %s (sent %d/%d)\n", libusb_error_name (r), sent, cmd_len);
      return -1;
    }

  r = libusb_bulk_transfer (dev, EPIN, resp, resp_buf_size, actual, TIMEOUT_MS);
  if (r != LIBUSB_SUCCESS)
    {
      log_printf ("!! IN failed: %s\n", libusb_error_name (r));
      return -1;
    }
  return 0;
}

static bool
check_response (const unsigned char *cmd, const unsigned char *resp, int actual)
{
  (void) cmd;
  if (actual < 7)
    {
      log_printf ("   !! response shorter than 7 bytes\n");
      return false;
    }
  bool ok = true;
  if (resp[0] != 0x53 || resp[1] != 0x49 || resp[2] != 0x47 || resp[3] != 0x45)
    {
      log_printf ("   !! missing SIGE magic: %02x %02x %02x %02x\n",
                  resp[0], resp[1], resp[2], resp[3]);
      ok = false;
    }
  return ok;
}

static void *
intr_monitor (void *arg)
{
  (void) arg;
  unsigned char buf[64];
  int actual;
  while (intr_thread_run)
    {
      const uint8_t eps[2] = { EPINTR1, EPINTR2 };
      for (int i = 0; i < 2 && intr_thread_run; i++)
        {
          actual = 0;
          int r = libusb_interrupt_transfer (dev, eps[i], buf, sizeof (buf), &actual,
                                             INTR_TIMEOUT_MS);
          if (r == LIBUSB_SUCCESS && actual > 0)
            {
              char ts[32];
              ts_now (ts, sizeof (ts));
              log_printf ("[intr @%s ms] EP %02x data:\n", ts, eps[i]);
              hexdump ("[intr]", buf, actual);
            }
        }
    }
  return NULL;
}

static int
open_device (void)
{
  int r = libusb_init (&ctx);
  if (r != LIBUSB_SUCCESS)
    {
      log_printf ("libusb_init: %s\n", libusb_error_name (r));
      return -1;
    }

  dev = libusb_open_device_with_vid_pid (ctx, VID, PID);
  if (!dev)
    {
      log_printf ("device %04x:%04x not found or not openable\n", VID, PID);
      return -1;
    }

  libusb_set_auto_detach_kernel_driver (dev, 1);

  r = libusb_claim_interface (dev, IID);
  if (r != LIBUSB_SUCCESS)
    {
      log_printf ("claim_interface: %s\n", libusb_error_name (r));
      return -1;
    }
  return 0;
}

static void
close_device (void)
{
  if (dev)
    {
      libusb_release_interface (dev, IID);
      libusb_close (dev);
    }
  if (ctx)
    libusb_exit (ctx);
  dev = NULL;
  ctx = NULL;
}

static void
mode_info (void)
{
  libusb_device *d = libusb_get_device (dev);
  struct libusb_device_descriptor dd;
  struct libusb_config_descriptor *cfg;
  libusb_get_device_descriptor (d, &dd);

  log_printf ("bcdUSB %04x  class %02x  vid %04x  pid %04x  bcdDevice %04x\n",
              dd.bcdUSB, dd.bDeviceClass, dd.idVendor, dd.idProduct, dd.bcdDevice);
  log_printf ("serial: %s\n", "see lsusb");

  if (libusb_get_active_config_descriptor (d, &cfg) == LIBUSB_SUCCESS)
    {
      for (int i = 0; i < cfg->bNumInterfaces; i++)
        {
          const struct libusb_interface *itf = &cfg->interface[i];
          for (int j = 0; j < itf->num_altsetting; j++)
            {
              const struct libusb_interface_descriptor *alt = &itf->altsetting[j];
              log_printf ("interface %d alt %d class %02x endpoints %d\n",
                          alt->bInterfaceNumber, alt->bAlternateSetting,
                          alt->bInterfaceClass, alt->bNumEndpoints);
              for (int k = 0; k < alt->bNumEndpoints; k++)
                {
                  const struct libusb_endpoint_descriptor *ep = &alt->endpoint[k];
                  log_printf ("  EP %02x attr %02x maxpkt %d interval %d\n",
                              ep->bEndpointAddress, ep->bmAttributes,
                              ep->wMaxPacketSize, ep->bInterval);
                }
            }
        }
      libusb_free_config_descriptor (cfg);
    }
}

static int
run_init_sequence (void)
{
  unsigned char resp[RESP_BUF_SIZE];
  int actual;

  for (int i = 0; i < INIT_PACKETS_COUNT; i++)
    {
      hexdump ("INIT req", INIT_PKTS[i], INIT_PKT_LENS[i]);
      if (send_recv (INIT_PKTS[i], INIT_PKT_LENS[i], INIT_PKT_RESP_LENS[i],
                     resp, RESP_BUF_SIZE, &actual) != 0)
        {
          log_printf ("INIT FAILED at packet %d\n", i);
          return -1;
        }
      hexdump ("INIT resp", resp, actual);
      if (INIT_PKTS[i][4] != 0x64)
        check_response (INIT_PKTS[i], resp, actual);
    }
  log_printf ("INIT sequence complete: %d packets OK\n", INIT_PACKETS_COUNT);
  return 0;
}

static int
run_repeat_sequence (void)
{
  unsigned char resp[RESP_BUF_SIZE];
  int actual;

  for (int i = 0; i < REPEAT_PACKETS_COUNT; i++)
    {
      hexdump ("REPEAT req", REPEAT_PKTS[i], REPEAT_PKT_LENS[i]);
      if (send_recv (REPEAT_PKTS[i], REPEAT_PKT_LENS[i], REPEAT_PKT_RESP_LENS[i],
                     resp, RESP_BUF_SIZE, &actual) != 0)
        {
          log_printf ("REPEAT FAILED at packet %d\n", i);
          return -1;
        }
      hexdump ("REPEAT resp", resp, actual);
      check_response (REPEAT_PKTS[i], resp, actual);
    }
  return 0;
}

typedef struct
{
  int count;
  unsigned char samples[64][16];
} poll_samples;

static void
sample_polls (poll_samples *ps, int n)
{
  unsigned char resp[RESP_BUF_SIZE];
  int actual;
  ps->count = 0;
  for (int i = 0; i < n; i++)
    {
      if (send_recv (POLL_PKT, sizeof (POLL_PKT), 9, resp, RESP_BUF_SIZE, &actual) != 0)
        continue;
      int keep = actual < 16 ? actual : 16;
      memcpy (ps->samples[ps->count], resp, keep);
      ps->count++;
      usleep (10000);
    }
}

static void
mode_poll_analysis (void)
{
  poll_samples absent = { 0 };
  poll_samples present = { 0 };

  log_printf ("\n=== POLL ANALYSIS ===\n");
  if (run_init_sequence () != 0)
    return;
  if (run_repeat_sequence () != 0)
    return;

  log_printf ("Keep finger OFF the sensor.\n");
  sample_polls (&absent, 32);
  log_printf ("collected %d finger-absent samples\n", absent.count);
  hexdump ("absent sample[0]", absent.samples[0], 9);
  hexdump ("absent sample[1]", absent.samples[1], 9);

  printf ("\n>>> PLACE FINGER FIRMLY ON SENSOR AND HOLD <<<\n");
  fflush (stdout);
  sleep (2);

  sample_polls (&present, 32);
  log_printf ("collected %d finger-present samples\n", present.count);
  hexdump ("present sample[0]", present.samples[0], 9);

  log_printf ("\nbyte-by-byte comparison (finger absent vs present):\n");
  int width = 9;
  for (int b = 0; b < width; b++)
    {
      bool differs = false;
      for (int s = 1; s < absent.count && !differs; s++)
        if (absent.samples[s][b] != absent.samples[0][b])
          differs = true;
      for (int s = 0; s < present.count && !differs; s++)
        {
          if (s < absent.count && present.samples[s][b] != absent.samples[s][b])
            differs = true;
          if (present.samples[0][b] != absent.samples[0][b])
            differs = true;
        }

      log_printf ("[%2d] absent:", b);
      for (int s = 0; s < absent.count && s < 8; s++)
        log_printf (" %02x", absent.samples[s][b]);
      log_printf ("\n     present:");
      for (int s = 0; s < present.count && s < 8; s++)
        log_printf (" %02x", present.samples[s][b]);
      log_printf ("%s\n", differs ? "   <-- varies" : "");
    }

  int bit5_absent_set = 0, bit6_absent_set = 0, bit5_present_set = 0, bit6_present_set = 0;
  for (int s = 0; s < absent.count; s++)
    {
      if (absent.samples[s][5] & 0x01)
        bit5_absent_set++;
      if (absent.samples[s][6] & 0x01)
        bit6_absent_set++;
    }
  for (int s = 0; s < present.count; s++)
    {
      if (present.samples[s][5] & 0x01)
        bit5_present_set++;
      if (present.samples[s][6] & 0x01)
        bit6_present_set++;
    }
  log_printf ("\nverdict:\n");
  log_printf ("  resp[5]&1 set: absent %d/%d, present %d/%d\n",
              bit5_absent_set, absent.count, bit5_present_set, present.count);
  log_printf ("  resp[6]&1 set: absent %d/%d, present %d/%d\n",
              bit6_absent_set, absent.count, bit6_present_set, present.count);

  if (bit5_present_set > bit5_absent_set && bit5_present_set >= present.count * 8 / 10)
    log_printf ("  => finger bit is resp[5]&0x01 (EH575-style)\n");
  else if (bit6_present_set > bit6_absent_set && bit6_present_set >= present.count * 8 / 10)
    log_printf ("  => finger bit is resp[6]&0x01 (marcel/MR-571 style)\n");
  else
    log_printf ("  => INCONCLUSIVE - inspect dumps above\n");

  printf ("\nYou may remove your finger now.\n");
}

static double
frame_variance (const unsigned char *img)
{
  uint64_t sum = 0;
  for (int i = 0; i < IMG_SIZE; i++)
    sum += img[i];
  if (sum == 0)
    return -1.0;
  double mean = (double) sum / IMG_SIZE;
  double sd = 0;
  for (int i = 0; i < IMG_SIZE; i++)
    {
      double d = img[i] - mean;
      sd += d * d;
    }
  return sd / IMG_SIZE;
}

static void
normalize_frame (unsigned char *bg, unsigned char *img, double *dark_portion)
{
  int diff[IMG_SIZE];
  int min = 255, max = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      diff[i] = bg[i] - img[i];
      if (diff[i] < min)
        min = diff[i];
      if (diff[i] > max)
        max = diff[i];
    }
  max -= CONTRAST;
  min += CONTRAST;
  int range = max - min;
  if (range == 0)
    range = 1;

  int ridges = 0;
  for (int i = 0; i < IMG_SIZE; i++)
    {
      int norm = ((diff[i] - min) * 255) / range;
      if (norm < 150)
        {
          if (norm < 120)
            norm = 0;
          ridges++;
        }
      else if (norm > 190)
        {
          norm = 255;
        }
      img[i] = (unsigned char) norm;
    }
  *dark_portion = (double) ridges / IMG_SIZE;
}

static void
write_pgm (const char *path, const unsigned char *img, int w, int h)
{
  FILE *f = fopen (path, "wb");
  if (!f)
    return;
  fprintf (f, "P5\n%d %d\n255\n", w, h);
  fwrite (img, 1, w * h, f);
  fclose (f);
  log_printf ("saved %s\n", path);
}

static int
fetch_frame (unsigned char *img)
{
  unsigned char resp[RESP_BUF_SIZE];
  int actual;
  hexdump ("IMAGE req", IMAGE_PKT, sizeof (IMAGE_PKT));
  if (send_recv (IMAGE_PKT, sizeof (IMAGE_PKT), IMG_SIZE, resp, RESP_BUF_SIZE, &actual) != 0)
    return -1;
  log_printf ("image response: %d bytes (expected %d)\n", actual, IMG_SIZE);
  if (actual != IMG_SIZE)
    {
      hexdump ("image resp head", resp, actual > 64 ? 64 : actual);
      return -1;
    }
  memcpy (img, resp, IMG_SIZE);
  return 0;
}

static void
mode_capture (bool with_poll_analysis)
{
  unsigned char frame[IMG_SIZE];
  unsigned char background[IMG_SIZE];
  bool have_background = false;
  pthread_t intr_tid;
  bool intr_started = false;

  if (with_poll_analysis)
    mode_poll_analysis ();

  log_printf ("\n=== CAPTURE ===\n");
  if (pthread_create (&intr_tid, NULL, intr_monitor, NULL) == 0)
    intr_started = true;

  if (run_init_sequence () != 0)
    goto done;

  printf ("Lift your finger and wait. Calibrating background...\n");

  for (int iter = 0; iter < POLL_COUNT_MAX; iter++)
    {
      if (iter > 0 && run_repeat_sequence () != 0)
        goto done;

      if (fetch_frame (frame) != 0)
        goto done;

      double variance = frame_variance (frame);

      if (!have_background)
        {
          if (variance >= 0 && variance < BG_VARIANCE)
            {
              memcpy (background, frame, IMG_SIZE);
              have_background = true;
              log_printf ("background captured (variance %.2f)\n", variance);
              write_pgm ("background.pgm", frame, IMG_WIDTH, IMG_HEIGHT);
              printf ("Calibrated! Place your finger on the sensor...\n");
            }
          else
            {
              log_printf ("waiting for clean background, variance %.2f\n", variance);
            }
        }
      else if (variance > FINGER_VARIANCE)
        {
          double dark_portion = 0;
          unsigned char processed[IMG_SIZE];
          memcpy (processed, frame, IMG_SIZE);
          normalize_frame (background, processed, &dark_portion);
          log_printf ("finger candidate: variance %.2f dark_portion %.4f\n",
                      variance, dark_portion);

          if (dark_portion > DARK_PORTION_MIN)
            {
              FILE *f = fopen ("frame_raw.bin", "wb");
              fwrite (frame, 1, IMG_SIZE, f);
              fclose (f);
              write_pgm ("frame_raw.pgm", frame, IMG_WIDTH, IMG_HEIGHT);
              write_pgm ("frame_normalized.pgm", processed, IMG_WIDTH, IMG_HEIGHT);
              log_printf ("CAPTURE SUCCESS on iteration %d\n", iter);
              goto done;
            }
        }

      usleep (50000);
    }
  log_printf ("capture gave up after %d iterations\n", POLL_COUNT_MAX);

done:
  if (intr_started)
    {
      intr_thread_run = false;
      pthread_join (intr_tid, NULL);
    }
}

#define DATASET_PHASES 7
static const char *DATASET_PHASE_NAMES[DATASET_PHASES] = {
  "center", "left", "right", "top", "bottom", "rotate-cw", "rotate-ccw",
};
#define DATASET_PER_PHASE 5

static void
wait_for_enter (const char *prompt)
{
  char line[64];
  printf ("%s [press ENTER]", prompt);
  fflush (stdout);
  if (!fgets (line, sizeof (line), stdin))
    exit (0);
}

static int
capture_gated_frame (unsigned char *background, unsigned char *frame)
{
  for (int iter = 0; iter < POLL_COUNT_MAX; iter++)
    {
      if (run_repeat_sequence () != 0)
        return -1;
      if (fetch_frame (frame) != 0)
        return -1;

      double variance = frame_variance (frame);
      if (variance > FINGER_VARIANCE)
        {
          double dark_portion = 0;
          unsigned char processed[IMG_SIZE];
          memcpy (processed, frame, IMG_SIZE);
          normalize_frame (background, processed, &dark_portion);
          if (dark_portion > DARK_PORTION_MIN)
            {
              log_printf ("gated capture ok: variance %.2f dark %.3f\n",
                          variance, dark_portion);
              return 0;
            }
        }
      usleep (30000);
    }
  return -1;
}

static int
mode_dataset (void)
{
  unsigned char frame[IMG_SIZE];
  unsigned char background[IMG_SIZE];
  bool have_background = false;
  int saved = 0;

  if (mkdir ("dataset", 0755) != 0 && errno != EEXIST)
    {
      log_printf ("cannot create dataset dir: %s\n", strerror (errno));
      return 1;
    }

  if (run_init_sequence () != 0)
    return 1;

  printf ("Lift your finger and wait. Calibrating background...\n");
  for (int iter = 0; iter < POLL_COUNT_MAX && !have_background; iter++)
    {
      if (fetch_frame (frame) != 0)
        return 1;
      double variance = frame_variance (frame);
      if (variance >= 0 && variance < BG_VARIANCE)
        {
          memcpy (background, frame, IMG_SIZE);
          have_background = true;
        }
      usleep (50000);
    }
  if (!have_background)
    {
      log_printf ("failed to calibrate background\n");
      return 1;
    }

  char path[256];
  snprintf (path, sizeof (path), "dataset/background.bin");
  FILE *f = fopen (path, "wb");
  fwrite (background, 1, IMG_SIZE, f);
  fclose (f);
  log_printf ("background saved to %s\n", path);

  for (int p = 0; p < DATASET_PHASES; p++)
    {
      wait_for_enter ("");
      printf ("\n>>> PHASE %d/%d: %s - press the finger in this position %d times <<<\n",
              p + 1, DATASET_PHASES, DATASET_PHASE_NAMES[p], DATASET_PER_PHASE);
      fflush (stdout);

      for (int k = 0; k < DATASET_PER_PHASE; k++)
        {
          if (capture_gated_frame (background, frame) != 0)
            {
              log_printf ("gated capture failed\n");
              return 1;
            }
          snprintf (path, sizeof (path), "dataset/frame_%s_%02d.bin",
                    DATASET_PHASE_NAMES[p], k);
          f = fopen (path, "wb");
          fwrite (frame, 1, IMG_SIZE, f);
          fclose (f);
          saved++;
          log_printf ("saved %s (%d total)\n", path, saved);
          printf (".");
          fflush (stdout);
          usleep (400000);
        }
      printf ("\nDone with phase. Lift finger.\n");
    }

  log_printf ("dataset complete: %d frames in %d phases\n", saved, DATASET_PHASES);
  printf ("Dataset complete: %d frames.\n", saved);
  return 0;
}

static int
write_reg (unsigned char reg, unsigned char val)
{
  unsigned char resp[RESP_BUF_SIZE];
  int actual;
  const unsigned char cmd[7] = { 0x45, 0x47, 0x49, 0x53, 0x61, reg, val };

  if (send_recv (cmd, sizeof (cmd), 7, resp, RESP_BUF_SIZE, &actual) != 0)
    return -1;
  return check_response (cmd, resp, actual) ? 0 : -1;
}

typedef struct
{
  unsigned char reg;
  unsigned char val;
} sweep_cfg;

#define SWEEP_N 16
static const sweep_cfg SWEEP_CONFIGS[SWEEP_N] = {
  { 0x24, 0x30 }, { 0x24, 0x34 }, { 0x24, 0x38 }, { 0x24, 0x3c },
  { 0x24, 0x40 }, { 0x21, 0x35 }, { 0x21, 0x3d }, { 0x21, 0x45 },
  { 0x21, 0x4d }, { 0x21, 0x55 }, { 0x20, 0x00 }, { 0x20, 0x08 },
  { 0x20, 0x10 }, { 0x20, 0x18 }, { 0x20, 0x20 }, { 0x20, 0x28 },
};

static int
mode_sweep (void)
{
  unsigned char frame[IMG_SIZE];
  unsigned char background[IMG_SIZE];

  printf ("This sweep writes tuning registers (0x24 gain, 0x21, 0x20)\n");
  printf ("one at a time on top of a fresh INIT. Values revert on next init.\n\n");
  if (run_init_sequence () != 0)
    return 1;

  printf ("Lift finger - capturing no-finger background...\n");
  for (int i = 0; i < 60; i++)
    {
      if (fetch_frame (frame) != 0)
        return 1;
      double v = frame_variance (frame);
      if (v >= 0 && v < BG_VARIANCE)
        {
          memcpy (background, frame, IMG_SIZE);
          break;
        }
      usleep (50000);
    }

  run_repeat_sequence ();

  printf ("\n>>> PLACE FINGER FIRMLY AND HOLD STEADY UNTIL SWEEP COMPLETES <<<\n\n");
  sleep (2);

  log_printf ("\n=== REGISTER SWEEP ===\n");
  log_printf ("%6s %6s %10s %10s %8s\n", "reg", "val", "variance", "contrast", "dark%");
  printf ("%6s %6s %10s %10s %8s\n", "reg", "val", "variance", "contrast", "dark%");

  int best_i = -1;
  double best_score = -1;

  for (int c = 0; c < SWEEP_N; c++)
    {
      write_reg (SWEEP_CONFIGS[c].reg, SWEEP_CONFIGS[c].val);
      usleep (20000);

      double var_sum = 0, con_sum = 0, dark_sum = 0;
      int ok_frames = 0;

      for (int k = 0; k < 3; k++)
        {
          if (run_repeat_sequence () != 0)
            goto out;
          if (fetch_frame (frame) != 0)
            goto out;

          double variance = frame_variance (frame);
          if (variance <= FINGER_VARIANCE)
            continue;

          unsigned char processed[IMG_SIZE];
          double dark_portion = 0;
          memcpy (processed, frame, IMG_SIZE);
          normalize_frame (background, processed, &dark_portion);

          /* contrast: stddev of the normalized image */
          double sum = 0;
          for (int i = 0; i < IMG_SIZE; i++)
            sum += processed[i];
          double mean = sum / IMG_SIZE;
          double sd = 0;
          for (int i = 0; i < IMG_SIZE; i++)
            {
              double d = processed[i] - mean;
              sd += d * d;
            }

          var_sum += variance;
          con_sum = sd / IMG_SIZE;
          dark_sum += dark_portion;
          ok_frames++;
        }

      if (ok_frames == 0)
        {
          log_printf ("0x%02x   0x%02x %10s\n",
                      SWEEP_CONFIGS[c].reg, SWEEP_CONFIGS[c].val, "no-finger?");
          printf ("0x%02x   0x%02x %10s\n",
                  SWEEP_CONFIGS[c].reg, SWEEP_CONFIGS[c].val, "no-finger?");
          continue;
        }

      double var_avg = var_sum / ok_frames;
      double con_avg = con_sum / ok_frames;
      double dark_avg = dark_sum / ok_frames;

      log_printf ("0x%02x   0x%02x %10.2f %10.2f %8.3f\n",
                  SWEEP_CONFIGS[c].reg, SWEEP_CONFIGS[c].val,
                  var_avg, con_avg, dark_avg);
      printf ("0x%02x   0x%02x %10.2f %10.2f %8.3f\n",
              SWEEP_CONFIGS[c].reg, SWEEP_CONFIGS[c].val,
              var_avg, con_avg, dark_avg);

      if (con_avg > best_score && dark_avg > DARK_PORTION_MIN)
        {
          best_score = con_avg;
          best_i = c;
        }
    }

out:
  if (best_i >= 0)
    {
      log_printf ("BEST: reg 0x%02x = 0x%02x (contrast %.2f)\n",
                  SWEEP_CONFIGS[best_i].reg, SWEEP_CONFIGS[best_i].val,
                  best_score);
      printf ("\nBEST: reg 0x%02x = 0x%02x (normalized-contrast %.2f)\n",
              SWEEP_CONFIGS[best_i].reg, SWEEP_CONFIGS[best_i].val, best_score);

      char path[64];
      snprintf (path, sizeof (path), "sweep_best_%02x_%02x.pgm",
                SWEEP_CONFIGS[best_i].reg, SWEEP_CONFIGS[best_i].val);
      fetch_frame (frame);
      write_pgm (path, frame, IMG_WIDTH, IMG_HEIGHT);
    }
  printf ("\nYou may remove your finger.\n");
  return 0;
}

int
main (int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "full";

  logf = fopen (LOG_PATH, "a");
  if (!logf)
    {
      fprintf (stderr, "cannot open %s: %s\n", LOG_PATH, strerror (errno));
      return 1;
    }
  setvbuf (logf, NULL, _IOLBF, 0);

  signal (SIGPIPE, SIG_IGN);

  if (open_device () != 0)
    {
      log_printf ("FAILED to open device (try running as root?)\n");
      close_device ();
      fclose (logf);
      return 1;
    }
  log_printf ("\n===== diagnostic start, mode=%s =====\n", mode);

  int exit_code = 0;

  if (strcmp (mode, "info") == 0)
    {
      mode_info ();
    }
  else if (strcmp (mode, "init") == 0)
    {
      if (run_init_sequence () != 0)
        exit_code = 1;
    }
  else if (strcmp (mode, "poll") == 0)
    {
      mode_poll_analysis ();
    }
  else if (strcmp (mode, "intr") == 0)
    {
      mode_info ();
      pthread_t intr_tid;
      if (pthread_create (&intr_tid, NULL, intr_monitor, NULL) == 0)
        {
          printf ("Monitoring interrupt EPs 0x83/0x84 for 30 s - touch and release the sensor a "
                  "few times...\n");
          sleep (30);
          intr_thread_run = false;
          pthread_join (intr_tid, NULL);
        }
    }
  else if (strcmp (mode, "capture") == 0)
    {
      mode_capture (false);
    }
  else if (strcmp (mode, "dataset") == 0)
    {
      exit_code = mode_dataset ();
    }
  else if (strcmp (mode, "sweep") == 0)
    {
      exit_code = mode_sweep ();
    }
  else if (strcmp (mode, "full") == 0)
    {
      mode_info ();
      mode_capture (true);
    }
  else
    {
      fprintf (stderr, "usage: %s [info|init|poll|capture|dataset|sweep|full]\n", argv[0]);
      exit_code = 2;
    }

  log_printf ("===== diagnostic end (exit %d) =====\n", exit_code);
  close_device ();
  fclose (logf);
  return exit_code;
}
