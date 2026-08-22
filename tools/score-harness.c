/*
 * Score harness for EgisTec EH576 (and any raw-frame image driver).
 * Replicates the egis0576 driver preprocessing, runs NBIS minutiae
 * detection and bozorth3 scoring exactly like production matching,
 * and prints a pairwise score matrix over a captured frame dataset.
 *
 * Build: registered in tests/meson.build as 'score-harness'
 * Usage: ./build/tests/score-harness <dataset-dir> [scale]
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FP_COMPONENT "score-harness"

#include "fp-image.h"
#include "fp-print-private.h"
#include "fpi-print.h"

#define IMG_WIDTH 70
#define IMG_HEIGHT 57
#define IMG_SIZE ((IMG_WIDTH) * (IMG_HEIGHT))
#define CANVAS 256
#define CONTRAST 4
#define RIDGE 150
#define CLAMP0 120
#define CLAMP255 190

typedef struct
{
  char   name[64];
  char   phase[32];
  struct xyt_struct *xyt;
  double variance;
  double dark_portion;
  double std_norm;
  double deep_frac;
  double mush_frac;
  int    raw_range;
  int    minutiae_count;
} sample;

typedef struct
{
  GMainLoop *loop;
  gboolean   ok;
} detect_ctx;

static void
on_minutiae (GObject *source, GAsyncResult *res, gpointer user_data)
{
  detect_ctx *ctx = user_data;

  ctx->ok = fp_image_detect_minutiae_finish (FP_IMAGE (source), res, NULL);
  g_main_loop_quit (ctx->loop);
}

static double
raw_variance (const guchar *img)
{
  double sum = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    sum += img[i];
  if (sum == 0)
    return -1;
  double mean = sum / IMG_SIZE;
  double sd = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      double d = img[i] - mean;

      sd += d * d;
    }
  return sd / IMG_SIZE;
}

static int opt_pct = 0;

static void
normalize_img (const guchar *bg, guchar *img, double *dark_portion,
               int *raw_range)
{
  int diff[IMG_SIZE];
  int min = 255;
  int max = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      diff[i] = (int) bg[i] - (int) img[i];
      if (diff[i] < min)
        min = diff[i];
      if (diff[i] > max)
        max = diff[i];
    }

  *raw_range = max - min;

  int lo = min + CONTRAST;
  int hi = max - CONTRAST;

  if (opt_pct > 0 && hi > lo)
    {
      /* Robust bounds: clip opt_pct percent from both tails of the
       * difference histogram before stretching. */
      int hist[512] = { 0 };

      for (int i = 0; i < IMG_SIZE; i++)
        hist[diff[i] + 256]++;

      int tail = IMG_SIZE * opt_pct / 100;
      int acc = 0;
      int plo = -256, phi = 255;

      for (int v = -256; v <= 255; v++)
        {
          acc += hist[v + 256];
          if (acc > tail)
            {
              plo = v;
              break;
            }
        }
      acc = 0;
      for (int v = 255; v >= -256; v--)
        {
          acc += hist[v + 256];
          if (acc > tail)
            {
              phi = v;
              break;
            }
        }

      lo = plo;
      hi = phi;
    }

  int range = hi - lo ? hi - lo : 1;

  int ridges = 0;
  for (int i = 0; i < IMG_SIZE; i++)
    {
      int norm = ((diff[i] - lo) * 255) / range;

      if (norm < RIDGE)
        {
          if (norm < CLAMP0)
            norm = 0;
          ridges++;
        }
      else if (norm > CLAMP255)
        {
          norm = 255;
        }
      img[i] = (guchar) norm;
    }
  *dark_portion = (double) ridges / IMG_SIZE;
}

static void
norm_stats (const guchar *img, double *std_norm, double *deep_frac,
            double *mush_frac)
{
  double sum = 0;

  int deep = 0, mush = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      sum += img[i];
      if (img[i] < 60)
        deep++;
      if (img[i] >= 100 && img[i] <= 200)
        mush++;
    }
  double mean = sum / IMG_SIZE;
  double sd = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      double d = img[i] - mean;

      sd += d * d;
    }
  *std_norm = sd / IMG_SIZE;
  *deep_frac = (double) deep / IMG_SIZE;
  *mush_frac = (double) mush / IMG_SIZE;
}

static void
upscale_and_pad (const guchar *src, guchar *canvas, int scale)
{
  int up_w = IMG_WIDTH * scale;
  int up_h = IMG_HEIGHT * scale;
  guchar upscaled[IMG_WIDTH * IMG_HEIGHT * 9];

  memset (canvas, 255, CANVAS * CANVAS);

  for (int y = 0; y < up_h; y++)
    for (int x = 0; x < up_w; x++)
      upscaled[y * up_w + x] =
        src[(y / scale >= IMG_HEIGHT ? IMG_HEIGHT - 1 : y / scale) * IMG_WIDTH +
            (x / scale >= IMG_WIDTH ? IMG_WIDTH - 1 : x / scale)];

  int off_x = (CANVAS - up_w) / 2;
  int off_y = (CANVAS - up_h) / 2;

  for (int y = 0; y < up_h; y++)
    memcpy (canvas + (y + off_y) * CANVAS + off_x,
            upscaled + y * up_w, up_w);
}

static gboolean
load_file (const char *path, guchar *buf, size_t len)
{
  FILE *f = fopen (path, "rb");

  if (!f)
    return FALSE;
  size_t got = fread (buf, 1, len, f);
  fclose (f);
  return got == len;
}

static int
phase_of (const char *name, char *out, size_t out_len)
{
  const char *p = strstr (name, "frame_");

  if (!p)
    return -1;
  p += strlen ("frame_");
  const char *u = strrchr (p, '_');

  if (!u || u == p)
    return -1;
  size_t n = u - p;

  if (n >= out_len)
    n = out_len - 1;
  memcpy (out, p, n);
  out[n] = 0;
  return 0;
}

static int
cmp_names (const void *a, const void *b)
{
  return strcmp (*(char **) a, *(char **) b);
}

int
main (int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : "dataset";
  int scale = argc > 2 ? atoi (argv[2]) : 2;
  opt_pct = argc > 3 ? atoi (argv[3]) : 0;

  if (scale < 1 || scale > 3)
    scale = 2;
  if (opt_pct < 0 || opt_pct > 20)
    opt_pct = 0;

  guchar bg[IMG_SIZE];
  char path[512];

  snprintf (path, sizeof (path), "%s/background.bin", dir);
  if (!load_file (path, bg, IMG_SIZE))
    {
      fprintf (stderr, "cannot load %s\n", path);
      return 1;
    }

  DIR *d = opendir (dir);

  if (!d)
    {
      fprintf (stderr, "cannot open %s\n", dir);
      return 1;
    }

  char **names = NULL;
  int n = 0;
  struct dirent *ent;

  while ((ent = readdir (d)))
    {
      if (!g_str_has_prefix (ent->d_name, "frame_") ||
          !g_str_has_suffix (ent->d_name, ".bin"))
        continue;
      names = g_realloc (names, sizeof (char *) * (n + 1));
      names[n++] = g_strdup (ent->d_name);
    }
  closedir (d);
  qsort (names, n, sizeof (char *), cmp_names);

  if (n == 0)
    {
      fprintf (stderr, "no frames in %s\n", dir);
      return 1;
    }

  sample *samples = g_new0 (sample, n);

  fprintf (stderr, "processing %d frames (scale=%dx, pct-clip=%d%%)...\n",
           n, scale, opt_pct);

  for (int i = 0; i < n; i++)
    {
      snprintf (path, sizeof (path), "%s/%s", dir, names[i]);
      guchar raw[IMG_SIZE];
      guchar proc[IMG_SIZE];
      guchar canvas[CANVAS * CANVAS];

      if (!load_file (path, raw, IMG_SIZE))
        {
          fprintf (stderr, "short read: %s\n", path);
          return 1;
        }

      memcpy (proc, raw, IMG_SIZE);
      samples[i].variance = raw_variance (raw);
      normalize_img (bg, proc, &samples[i].dark_portion, &samples[i].raw_range);
      norm_stats (proc, &samples[i].std_norm, &samples[i].deep_frac,
                  &samples[i].mush_frac);
      upscale_and_pad (proc, canvas, scale);

      phase_of (names[i], samples[i].phase, sizeof (samples[i].phase));
      g_strlcpy (samples[i].name, names[i], sizeof (samples[i].name));

      FpImage *img = fp_image_new (CANVAS, CANVAS);

      memcpy (img->data, canvas, CANVAS * CANVAS);

      detect_ctx ctx;

      ctx.loop = g_main_loop_new (NULL, FALSE);
      fp_image_detect_minutiae (img, NULL, on_minutiae, &ctx);
      g_main_loop_run (ctx.loop);
      g_main_loop_unref (ctx.loop);

      if (!ctx.ok)
        {
          fprintf (stderr, "minutiae detection failed for %s\n", names[i]);
          samples[i].xyt = NULL;
          samples[i].minutiae_count = 0;
          g_object_unref (img);
          continue;
        }

      samples[i].minutiae_count = img->minutiae ? (int) img->minutiae->len : 0;

      FpPrint *print = g_object_new (FP_TYPE_PRINT,
                                     "driver", "score-harness",
                                     "device-id", "0",
                                     NULL);

      fpi_print_set_type (print, FPI_PRINT_NBIS);
      if (!fpi_print_add_from_image (print, img, NULL))
        {
          fprintf (stderr, "add_from_image failed for %s\n", names[i]);
          samples[i].xyt = NULL;
        }
      else
        {
          samples[i].xyt = g_memdup2 (g_ptr_array_index (print->prints, 0),
                                      sizeof (struct xyt_struct));
        }

      g_object_unref (print);
      g_object_unref (img);
      free (names[i]);
    }
  g_free (names);

  gint *scores = g_new0 (gint, n * n);

  for (int i = 0; i < n; i++)
    {
      if (!samples[i].xyt)
        continue;
      int probe_len = bozorth_probe_init (samples[i].xyt);

      for (int j = 0; j < n; j++)
        {
          if (i == j || !samples[j].xyt)
            continue;
          scores[i * n + j] =
            bozorth_to_gallery (probe_len, samples[i].xyt, samples[j].xyt);
        }
    }

  printf ("pairwise bozorth3 scores (rows=probe, cols=template):\n\n%-14s", "probe\\tmpl");
  for (int j = 0; j < n; j++)
    printf ("%5.4s ", samples[j].name + 6);
  printf ("\n");

  for (int i = 0; i < n; i++)
    {
      printf ("%-14s", samples[i].name + 6);
      for (int j = 0; j < n; j++)
        {
          if (i == j)
            printf ("    . ");
          else
            printf ("%5d ", scores[i * n + j]);
        }
      printf ("\n");
    }

  printf ("\nper-frame quality stats:\n");
  printf ("%-22s %5s %6s %6s %7s %7s %8s\n",
          "frame", "var", "dark%", "minut", "deep%", "mush%", "verdict");
  for (int i = 0; i < n; i++)
    {
      int mx = 0;

      for (int j = 0; j < n; j++)
        if (scores[i * n + j] > mx)
          mx = scores[i * n + j];
      printf ("%-22s %5.1f %6.3f %6d %7.3f %7.3f %8s\n",
              samples[i].name, samples[i].variance,
              samples[i].dark_portion, samples[i].minutiae_count,
              samples[i].deep_frac, samples[i].mush_frac,
              mx >= 10 ? "alive" : "DEAD");
    }

  printf ("\nscore distributions by pair class:\n");
  printf ("threshold:");
  for (int t = 6; t <= 14; t += 2)
    printf (" %3d", t);
  printf ("\n");

  for (int cls = 0; cls < 2; cls++)
    {
      const char *label = cls == 0 ? "same-phase" : "cross-phase";
      int total = 0;
      int hits[5] = { 0 };
      int sum = 0, mx = 0, mn = 1000;

      for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
          {
            if (i == j || !samples[i].xyt || !samples[j].xyt)
              continue;
            gboolean same = strcmp (samples[i].phase, samples[j].phase) == 0;

            if ((cls == 0) != same)
              continue;
            int s = scores[i * n + j];

            total++;
            sum += s;
            if (s > mx)
              mx = s;
            if (s < mn)
              mn = s;
            for (int k = 0; k < 5; k++)
              if (s >= 6 + k * 2)
                hits[k]++;
          }

      printf ("%-11s n=%-4d avg=%-5.1f min=%-3d max=%-3d match-rate:",
              label, total, total ? (double) sum / total : 0.0, mn, mx);
      for (int k = 0; k < 5; k++)
        printf (" %3.0f%%", total ? 100.0 * hits[k] / total : 0.0);
      printf ("\n");
    }

  snprintf (path, sizeof (path), "%s/score-matrix.csv", dir);
  FILE *csv = fopen (path, "w");

  if (csv)
    {
      fprintf (csv, "probe,template,same_phase,score\n");
      for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
          if (i != j && samples[i].xyt && samples[j].xyt)
            fprintf (csv, "%s,%s,%s,%d\n",
                     samples[i].name, samples[j].name,
                     strcmp (samples[i].phase, samples[j].phase) == 0 ? "yes" : "no",
                     scores[i * n + j]);
      fclose (csv);
      fprintf (stderr, "\nwrote %s\n", path);
    }

  return 0;
}
