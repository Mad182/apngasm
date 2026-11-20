/* APNG Assembler 2.91-ezgif
 *
 * This program creates APNG animation from PNG/TGA image sequence.
 *
 * http://apngasm.sourceforge.net/
 *
 * Copyright (c) 2009-2016 Max Stepin
 * maxst at users.sourceforge.net
 *
 * Modified 2026 by Madars (ezgif.com): performance improvements and bug
 * fixes, see readme.txt. This is an altered version of the original.
 *
 * zlib license
 * ------------
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */
#include "image.h"
#include "png.h" /* original (unpatched) libpng is ok */
#include "zlib.h"
#include <cctype>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef FEATURE_7ZIP
#include "7z.h"
#endif
#ifdef FEATURE_ZOPFLI
#include "zopfli.h"
#endif
#ifdef FEATURE_LIBDEFLATE
#include "libdeflate.h"
#endif

typedef struct {
  Image *image;
  unsigned int size;
  int x, y, w, h, valid, filters;
} OP;
OP op[6];

unsigned char *op_zbuf1;
unsigned char *op_zbuf2;
z_stream op_zstream1;
z_stream op_zstream2;
/* Trial streams still running. A stream is dropped as soon as its output
 * exceeds the best candidate size found so far for the current frame (it can
 * no longer win), which avoids compressing the rest of a losing candidate. */
int op_z1_active;
int op_z2_active;
#define OP_SIZE_LOST 0xFFFFFFFFu

/* compression level for -z0 (1-9, default 9) and -z3 (1-12, default 12) */
int zlib_level = -1;
#ifdef FEATURE_LIBDEFLATE
struct libdeflate_compressor *ld_compressor = NULL;
#endif

unsigned int next_seq_num;
unsigned char *row_buf;
unsigned char *sub_row;
unsigned char *up_row;
unsigned char *avg_row;
unsigned char *paeth_row;
unsigned char png_sign[8] = {137, 80, 78, 71, 13, 10, 26, 10};
#define APNGASM_VERSION "2.91-ezgif"
/* tEXt chunk: keyword, NUL separator, text */
static const char png_Software[] = "Software\0APNG Assembler " APNGASM_VERSION;

void write_chunk(FILE *f, const char *name, unsigned char *data,
                 unsigned int length) {
  unsigned char buf[4];
  unsigned int crc = (unsigned int)crc32(0, Z_NULL, 0);

  png_save_uint_32(buf, length);
  fwrite(buf, 1, 4, f);
  fwrite(name, 1, 4, f);
  crc = (unsigned int)crc32(crc, (const Bytef *)name, 4);

  if (memcmp(name, "fdAT", 4) == 0) {
    png_save_uint_32(buf, next_seq_num++);
    fwrite(buf, 1, 4, f);
    crc = (unsigned int)crc32(crc, buf, 4);
    length -= 4;
  }

  if (data != NULL && length > 0) {
    fwrite(data, 1, length, f);
    crc = (unsigned int)crc32(crc, data, length);
  }

  png_save_uint_32(buf, crc);
  fwrite(buf, 1, 4, f);
}

void write_IDATs(FILE *f, unsigned int frame, unsigned char *data,
                 unsigned int length, unsigned int idat_size) {
  unsigned int z_cmf = data[0];
  if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70) {
    if (length >= 2) {
      unsigned int z_cinfo = z_cmf >> 4;
      unsigned int half_z_window_size = 1 << (z_cinfo + 7);
      while (idat_size <= half_z_window_size && half_z_window_size >= 256) {
        z_cinfo--;
        half_z_window_size >>= 1;
      }
      z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
      if (data[0] != (unsigned char)z_cmf) {
        data[0] = (unsigned char)z_cmf;
        data[1] &= 0xe0;
        data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
      }
    }
  }

  while (length > 0) {
    unsigned int ds = length;
    if (ds > 32768)
      ds = 32768;

    if (frame == 0)
      write_chunk(f, "IDAT", data, ds);
    else
      write_chunk(f, "fdAT", data, ds + 4);

    data += ds;
    length -= ds;
  }
}

void process_rect(Image *image, int xbytes, int rowbytes, int y, int h, int bpp,
                  unsigned char *dest) {
  int i, j, v;
  int a, b, c, pa, pb, pc, p;
  unsigned char *prev = NULL;
  unsigned char *dp = dest;
  unsigned char *out;

  for (j = y; j < y + h; j++) {
    unsigned char *row = image->rows[j] + xbytes;
    unsigned int sum = 0;
    unsigned char *best_row = row_buf;
    unsigned int mins = ((unsigned int)(-1)) >> 1;

    out = row_buf + 1;
    for (i = 0; i < rowbytes; i++) {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    mins = sum;

    sum = 0;
    out = sub_row + 1;
    for (i = 0; i < bpp; i++) {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    for (i = bpp; i < rowbytes; i++) {
      v = out[i] = row[i] - row[i - bpp];
      sum += (v < 128) ? v : 256 - v;
      if (sum > mins)
        break;
    }
    if (sum < mins) {
      mins = sum;
      best_row = sub_row;
    }

    if (prev) {
      sum = 0;
      out = up_row + 1;
      for (i = 0; i < rowbytes; i++) {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins)
          break;
      }
      if (sum < mins) {
        mins = sum;
        best_row = up_row;
      }

      sum = 0;
      out = avg_row + 1;
      for (i = 0; i < bpp; i++) {
        v = out[i] = row[i] - prev[i] / 2;
        sum += (v < 128) ? v : 256 - v;
      }
      for (i = bpp; i < rowbytes; i++) {
        v = out[i] = row[i] - (prev[i] + row[i - bpp]) / 2;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins)
          break;
      }
      if (sum < mins) {
        mins = sum;
        best_row = avg_row;
      }

      sum = 0;
      out = paeth_row + 1;
      for (i = 0; i < bpp; i++) {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
      }
      for (i = bpp; i < rowbytes; i++) {
        a = row[i - bpp];
        b = prev[i];
        c = prev[i - bpp];
        p = b - c;
        pc = a - c;
        pa = abs(p);
        pb = abs(pc);
        pc = abs(p + pc);
        p = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
        v = out[i] = row[i] - p;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins)
          break;
      }
      if (sum < mins) {
        best_row = paeth_row;
      }
    }

    if (dest == NULL) {
      // deflate_rect_op()
      if (op_z1_active) {
        op_zstream1.next_in = row_buf;
        op_zstream1.avail_in = rowbytes + 1;
        deflate(&op_zstream1, Z_NO_FLUSH);
        if (op_zstream1.avail_out == 0)
          op_z1_active = 0;
      }

      if (op_z2_active) {
        op_zstream2.next_in = best_row;
        op_zstream2.avail_in = rowbytes + 1;
        deflate(&op_zstream2, Z_NO_FLUSH);
        if (op_zstream2.avail_out == 0)
          op_z2_active = 0;
      }

      if (!op_z1_active && !op_z2_active)
        break;
    } else {
      // deflate_rect_fin()
      memcpy(dp, best_row, rowbytes + 1);
      dp += rowbytes + 1;
    }

    prev = row;
  }
}

void deflate_rect_fin(int deflate_method, int iter, unsigned char *zbuf,
                      unsigned int *zsize, int bpp, unsigned char *dest,
                      int zbuf_size, int n) {
  Image *image = op[n].image;
  int xbytes = op[n].x * bpp;
  int rowbytes = op[n].w * bpp;
  int y = op[n].y;
  int h = op[n].h;

  if (op[n].filters == 0) {
    unsigned char *dp = dest;
    for (int j = y; j < y + h; j++) {
      *dp++ = 0;
      memcpy(dp, image->rows[j] + xbytes, rowbytes);
      dp += rowbytes;
    }
  } else
    process_rect(image, xbytes, rowbytes, y, h, bpp, dest);

#ifdef FEATURE_LIBDEFLATE
  if (deflate_method == 3 && ld_compressor != NULL) {
    size_t size = libdeflate_zlib_compress(ld_compressor, dest,
                                           (size_t)h * (rowbytes + 1), zbuf,
                                           (size_t)zbuf_size);
    if (size > 0) {
      *zsize = (unsigned int)size;
      return;
    }
    /* didn't fit - fall through to zlib */
  }
#endif
#ifdef FEATURE_ZOPFLI
  if (deflate_method == 2) {
    ZopfliOptions opt_zopfli;
    unsigned char *data = 0;
    size_t size = 0;
    ZopfliInitOptions(&opt_zopfli);
    opt_zopfli.numiterations = iter;
    ZopfliCompress(&opt_zopfli, ZOPFLI_FORMAT_ZLIB, dest, h * (rowbytes + 1),
                   &data, &size);
    int ok = size < (size_t)zbuf_size;
    if (ok) {
      memcpy(zbuf, data, size);
      *zsize = (unsigned int)size;
    }
    free(data);
    if (ok)
      return;
    /* didn't fit (shouldn't happen) - fall through to zlib instead of
       leaving the previous frame's size in *zsize */
  }
#endif
#ifdef FEATURE_7ZIP
  if (deflate_method == 1) {
    unsigned size = zbuf_size;
    if (compress_rfc1950_7z(dest, h * (rowbytes + 1), zbuf, size,
                            iter < 100 ? iter : 100, 255)) {
      *zsize = size;
      return;
    }
    /* 7zip failed - fall through to zlib */
  }
#endif
  {
    z_stream fin_zstream;

    fin_zstream.data_type = Z_BINARY;
    fin_zstream.zalloc = Z_NULL;
    fin_zstream.zfree = Z_NULL;
    fin_zstream.opaque = Z_NULL;
    deflateInit2(&fin_zstream, zlib_level > 9 ? 9 : zlib_level, 8, 15, 8,
                 op[n].filters ? Z_FILTERED : Z_DEFAULT_STRATEGY);

    fin_zstream.next_out = zbuf;
    fin_zstream.avail_out = zbuf_size;
    fin_zstream.next_in = dest;
    fin_zstream.avail_in = h * (rowbytes + 1);
    deflate(&fin_zstream, Z_FINISH);
    *zsize = (unsigned int)fin_zstream.total_out;
    deflateEnd(&fin_zstream);
  }
}

/* Returns 1 if candidate n would encode exactly the same pixels as an already
 * evaluated candidate. Such a candidate compresses to the same size and, since
 * the selection only takes strictly smaller sizes, can never win. */
static int same_as_earlier_op(Image *image, int x, int y, int w, int h,
                              int bpp, int n) {
  size_t rowbytes = (size_t)w * bpp;
  size_t xbytes = (size_t)x * bpp;
  for (int m = 0; m < n; m++) {
    if (!op[m].valid || op[m].x != x || op[m].y != y || op[m].w != w ||
        op[m].h != h)
      continue;
    Image *other = op[m].image;
    if (other == image)
      return 1;
    int j;
    for (j = y; j < y + h; j++)
      if (memcmp(other->rows[j] + xbytes, image->rows[j] + xbytes, rowbytes))
        break;
    if (j == y + h)
      return 1;
  }
  return 0;
}

void deflate_rect_op(Image *image, int x, int y, int w, int h, int bpp,
                     int zbuf_size, int n) {
  if (n > 0 && same_as_earlier_op(image, x, y, w, h, bpp, n))
    return; /* op[n].valid stays 0 */

  /* Any output larger than the best size so far can't win, so cap the trial
   * buffers at best + 1 bytes and abandon a stream once it fills up. */
  unsigned int cap = (unsigned int)zbuf_size;
  for (int m = 0; m < n; m++)
    if (op[m].valid && op[m].size < cap - 1)
      cap = op[m].size + 1;

  op_zstream1.data_type = Z_BINARY;
  op_zstream1.next_out = op_zbuf1;
  op_zstream1.avail_out = cap;

  op_zstream2.data_type = Z_BINARY;
  op_zstream2.next_out = op_zbuf2;
  op_zstream2.avail_out = cap;

  op_z1_active = op_z2_active = 1;
  process_rect(image, x * bpp, w * bpp, y, h, bpp, NULL);

  if (op_z1_active && deflate(&op_zstream1, Z_FINISH) != Z_STREAM_END)
    op_z1_active = 0;
  if (op_z2_active && deflate(&op_zstream2, Z_FINISH) != Z_STREAM_END)
    op_z2_active = 0;
  op[n].image = image;

  unsigned int size1 =
      op_z1_active ? (unsigned int)op_zstream1.total_out : OP_SIZE_LOST;
  unsigned int size2 =
      op_z2_active ? (unsigned int)op_zstream2.total_out : OP_SIZE_LOST;

  if (size1 < size2) {
    op[n].size = size1;
    op[n].filters = 0;
  } else {
    op[n].size = size2;
    op[n].filters = 1;
  }
  op[n].x = x;
  op[n].y = y;
  op[n].w = w;
  op[n].h = h;
  op[n].valid = 1;
  deflateReset(&op_zstream1);
  deflateReset(&op_zstream2);
}

void get_rect(unsigned int w, unsigned int h, Image *image1, Image *image2,
              Image *temp, unsigned int bpp, int zbuf_size,
              unsigned int has_tcolor, unsigned int tcolor, int n) {
  unsigned int i, j, x0, y0, w0, h0;
  unsigned int x_min = w - 1;
  unsigned int y_min = h - 1;
  unsigned int x_max = 0;
  unsigned int y_max = 0;
  unsigned int diffnum = 0;
  unsigned int over_is_possible = 1;

  if (!has_tcolor)
    over_is_possible = 0;

  if (bpp == 1) {
    for (j = 0; j < h; j++) {
      unsigned char *pa = image1->rows[j];
      unsigned char *pb = image2->rows[j];
      unsigned char *pc = temp->rows[j];
      if (memcmp(pa, pb, w) == 0) {
        memset(pc, tcolor, w);
      } else {
        if (j < y_min)
          y_min = j;
        y_max = j;
        for (i = 0; i < w; i++) {
          unsigned char c = *pb++;
          if (*pa++ != c) {
            diffnum++;
            if (has_tcolor && c == tcolor)
              over_is_possible = 0;
            if (i < x_min)
              x_min = i;
            if (i > x_max)
              x_max = i;
          } else
            c = tcolor;

          *pc++ = c;
        }
      }
    }
  } else if (bpp == 2) {
    for (j = 0; j < h; j++) {
      unsigned short *pa = (unsigned short *)image1->rows[j];
      unsigned short *pb = (unsigned short *)image2->rows[j];
      unsigned short *pc = (unsigned short *)temp->rows[j];
      if (memcmp(pa, pb, w * 2) == 0) {
        memset(pc, 0, w * 2);
      } else {
        if (j < y_min)
          y_min = j;
        y_max = j;
        for (i = 0; i < w; i++) {
          unsigned int c1 = *pa++;
          unsigned int c2 = *pb++;
          if ((c1 != c2) && ((c1 >> 8) || (c2 >> 8))) {
            diffnum++;
            if ((c2 >> 8) != 0xFF)
              over_is_possible = 0;
            if (i < x_min)
              x_min = i;
            if (i > x_max)
              x_max = i;
          } else
            c2 = 0;

          *pc++ = c2;
        }
      }
    }
  } else if (bpp == 3) {
    unsigned int rowbytes = w * 3;
    for (j = 0; j < h; j++) {
      unsigned char *pa = image1->rows[j];
      unsigned char *pb = image2->rows[j];
      unsigned char *pc = temp->rows[j];
      if (memcmp(pa, pb, rowbytes) == 0) {
        for (i = 0; i < w; i++)
          memcpy(pc + i * 3, &tcolor, 3);
      } else {
        if (j < y_min)
          y_min = j;
        y_max = j;
        for (i = 0; i < w; i++) {
          if (pa[0] != pb[0] || pa[1] != pb[1] || pa[2] != pb[2]) {
            diffnum++;
            unsigned int c2 = (pb[2] << 16) + (pb[1] << 8) + pb[0];
            if (has_tcolor && c2 == tcolor)
              over_is_possible = 0;
            if (i < x_min)
              x_min = i;
            if (i > x_max)
              x_max = i;
            memcpy(pc, pb, 3);
          } else {
            memcpy(pc, &tcolor, 3);
          }
          pa += 3;
          pb += 3;
          pc += 3;
        }
      }
    }
  } else if (bpp == 4) {
    for (j = 0; j < h; j++) {
      unsigned int *pa = (unsigned int *)image1->rows[j];
      unsigned int *pb = (unsigned int *)image2->rows[j];
      unsigned int *pc = (unsigned int *)temp->rows[j];
      if (memcmp(pa, pb, w * 4) == 0) {
        memset(pc, 0, w * 4);
      } else {
        if (j < y_min)
          y_min = j;
        y_max = j;
        for (i = 0; i < w; i++) {
          unsigned int c1 = *pa++;
          unsigned int c2 = *pb++;
          if ((c1 != c2) && ((c1 >> 24) || (c2 >> 24))) {
            diffnum++;
            if ((c2 >> 24) != 0xFF)
              over_is_possible = 0;
            if (i < x_min)
              x_min = i;
            if (i > x_max)
              x_max = i;
          } else
            c2 = 0;

          *pc++ = c2;
        }
      }
    }
  }

  if (diffnum == 0) {
    x0 = y0 = 0;
    w0 = h0 = 1;
  } else {
    x0 = x_min;
    y0 = y_min;
    w0 = x_max - x_min + 1;
    h0 = y_max - y_min + 1;
  }

  deflate_rect_op(image2, x0, y0, w0, h0, bpp, zbuf_size, n * 2);

  if (over_is_possible)
    deflate_rect_op(temp, x0, y0, w0, h0, bpp, zbuf_size, n * 2 + 1);
}

void hstrip2images(Image *image, unsigned int w, std::vector<Image> &img,
                   int delay_num, int delay_den) {
  unsigned int i, x, y;
  unsigned int rowbytes = w * image->bpp;

  for (i = 0, x = 0; i < img.size(); i++, x += rowbytes) {
    img[i].init(w, image->h, image);
    img[i].delay_num = delay_num;
    img[i].delay_den = delay_den;
    for (y = 0; y < image->h; y++)
      memcpy(img[i].rows[y], image->rows[y] + x, rowbytes);
  }
}

void vstrip2images(Image *image, unsigned int h, std::vector<Image> &img,
                   int delay_num, int delay_den) {
  unsigned int i, y, row;
  unsigned int rowbytes = image->w * image->bpp;

  for (i = 0, row = 0; i < img.size(); i++) {
    img[i].init(image->w, h, image);
    img[i].delay_num = delay_num;
    img[i].delay_den = delay_den;
    for (y = 0; y < h; y++)
      memcpy(img[i].rows[y], image->rows[row++], rowbytes);
  }
}

int load_from_horizontal_strip(char *szImage, int hs, std::vector<Image> &img,
                               int delay_num, int delay_den,
                               unsigned char *coltype) {
  Image image;
  printf("loading %s\n", szImage);
  if (load_image(szImage, &image))
    return 1;

  unsigned int w = image.w / hs;
  if (w * hs != image.w) {
    printf("Error loading image strip: Image width %d not divisible by %d\n",
           image.w, hs);
    return 1;
  }
  *coltype = image.type;

  img.resize(hs);
  hstrip2images(&image, w, img, delay_num, delay_den);

  return 0;
}

int load_from_vertical_strip(char *szImage, int vs, std::vector<Image> &img,
                             int delay_num, int delay_den,
                             unsigned char *coltype) {
  Image image;
  printf("loading %s\n", szImage);
  if (load_image(szImage, &image))
    return 1;

  unsigned int h = image.h / vs;
  if (h * vs != image.h) {
    printf("Error loading image strip: Image height %d not divisible by %d\n",
           image.h, vs);
    return 1;
  }
  *coltype = image.type;

  img.resize(vs);
  vstrip2images(&image, h, img, delay_num, delay_den);

  return 0;
}

int load_image_sequence(char *szImage, unsigned int first,
                        std::vector<Image> &img, int delay_num, int delay_den,
                        unsigned char *coltype) {
  char szFormat[256];
  char szNext[256];
  char *szExt = strrchr(szImage, '.');
  unsigned int i, cur = 0, frames = 0;
  FILE *f;

  /* szFormat/szNext are fixed 256 byte buffers built with strcpy/sprintf;
     leave room for the "%0Nd" expansion */
  if (strlen(szImage) > 240) {
    printf("Error: file name too long: '%s'\n", szImage);
    return 1;
  }

  /* the file name is turned into a printf format below - a '%' in it would
     be interpreted as a conversion, so load such files as a single frame */
  if (strchr(szImage, '%') != NULL) {
    img.resize(1);
    printf("loading %s\n", szImage);
    if (load_image(szImage, &img[0]))
      return 1;
    img[0].delay_num = delay_num;
    img[0].delay_den = delay_den;
    *coltype = img[0].type;
    return 0;
  }

  if (szExt == NULL || szExt == szImage) {
    printf("Error: sequence not found for '%s'\n", szImage);
    return 1;
  } else if (*(szExt - 1) == '*') {
    f = NULL;
    for (i = 1; i < 6; i++) {
      strcpy(szFormat, szImage);
      sprintf(szFormat + (szExt - 1 - szImage), "%%0%dd%%s", i);
      cur = 0;
      sprintf(szNext, szFormat, cur, szExt);
      if ((f = fopen(szNext, "rb")) != 0)
        break;
      cur = 1;
      sprintf(szNext, szFormat, cur, szExt);
      if ((f = fopen(szNext, "rb")) != 0)
        break;
    }

    if (f != NULL)
      fclose(f);
    else {
      printf("Error: *%s sequence not found\n", szExt);
      return 1;
    }
  } else {
    for (i = 0; i < 6; i++) {
      if (szImage == szExt - i)
        break;
      if (*(szExt - i - 1) < '0')
        break;
      if (*(szExt - i - 1) > '9')
        break;
    }
    cur = atoi(szExt - i);
    strcpy(szFormat, szImage);
    if (i > 0)
      sprintf(szFormat + (szExt - i - szImage), "%%0%dd%%s", i);
    strcpy(szNext, szImage);
  }

  if ((f = fopen(szNext, "rb")) == 0) {
    printf("Error: can't open the file '%s'\n", szNext);
    return 1;
  }

  if (i > 0 || *(szExt - 1) == '*') {
    do {
      frames++;
      fclose(f);
      sprintf(szNext, szFormat, cur + frames, szExt);
      f = fopen(szNext, "rb");
    } while (f != 0);
  } else {
    fclose(f);
    frames = 1;
  }

  img.resize(frames);

  for (i = 0; i < frames; i++) {
    if (frames > 1)
      sprintf(szNext, szFormat, cur + i, szExt);
    else
      strcpy(szNext, szImage);

    printf("loading %s (%d of %d)\n", szNext, i - first + 1, frames - first);

    if (load_image(szNext, &img[i]))
      return 1;

    img[i].delay_num = delay_num;
    img[i].delay_den = delay_den;

    sprintf(szNext, szFormat, cur + i, ".txt");
    f = fopen(szNext, "rt");
    if (f != 0) {
      char szStr[256];
      if (fgets(szStr, 256, f) != NULL) {
        int d1, d2;
        if (sscanf(szStr, "delay=%d/%d", &d1, &d2) == 2) {
          if (d1 != 0)
            img[i].delay_num = d1;
          if (d2 != 0)
            img[i].delay_den = d2;
        }
      }
      fclose(f);
    }

    if (img[0].w != img[i].w || img[0].h != img[i].h) {
      printf("Error at %s: different image size\n", szNext);
      return 1;
    }
  }

  *coltype = find_common_coltype(img);

  return 0;
}

/* fcTL stores delay_num/delay_den as 16-bit values. Long or merged delays
 * (e.g. ms delays of duplicate frames added together) can exceed that, which
 * used to silently wrap around. Scale the fraction down to fit instead. */
static void save_delay(unsigned char *buf, unsigned int num, unsigned int den) {
  if (den == 0)
    den = 100;
  unsigned long long n = num, d = den;
  if (n > 65535 || d > 65535) {
    /* try exact-ish common denominators first, then plain scaling */
    static const unsigned int dens[] = {1000, 100, 10, 1};
    int done = 0;
    for (int k = 0; k < 4 && !done; k++) {
      unsigned long long nn = (n * dens[k] + d / 2) / d;
      if (nn <= 65535 && (nn > 0 || n == 0)) {
        n = nn;
        d = dens[k];
        done = 1;
      }
    }
    if (!done) { /* longer than 65535 s - clamp */
      n = 65535;
      d = 1;
    }
  }
  png_save_uint_16(buf, (unsigned int)n);
  png_save_uint_16(buf + 2, (unsigned int)d);
}

int save_apng(char *szOut, std::vector<Image> &img, unsigned int loops,
              unsigned int first, int deflate_method, int iter) {
  unsigned char coltype = img[0].type;
  unsigned int has_tcolor = 0;
  unsigned int tcolor = 0;

  if (coltype == 0) {
    if (img[0].ts) {
      has_tcolor = 1;
      tcolor = img[0].tr[1];
    }
  } else if (coltype == 2) {
    if (img[0].ts) {
      has_tcolor = 1;
      tcolor = (((img[0].tr[5] << 8) + img[0].tr[3]) << 8) + img[0].tr[1];
    }
  } else if (coltype == 3) {
    for (int c = 0; c < img[0].ts; c++)
      if (img[0].tr[c] == 0) {
        has_tcolor = 1;
        tcolor = c;
        break;
      }
  } else
    has_tcolor = 1;

  FILE *f;
  if ((f = fopen(szOut, "wb")) == 0) {
    printf("Error: can't save to file '%s'\n", szOut);
    return 1;
  }

  unsigned char buf_IHDR[13];
  unsigned char buf_acTL[8];
  unsigned char buf_fcTL[26];

  unsigned int width = img[0].w;
  unsigned int height = img[0].h;
  unsigned int bpp = img[0].bpp;
  unsigned int rowbytes = width * bpp;
  unsigned int visible = (unsigned int)(img.size() - first);

  Image temp, over1, over2, over3, rest;
  temp.init(&img[0]);
  over1.init(&img[0]);
  over2.init(&img[0]);
  over3.init(&img[0]);
  rest.init(&img[0]);
  unsigned char *dest = new unsigned char[(rowbytes + 1) * height];

  png_save_uint_32(buf_IHDR, width);
  png_save_uint_32(buf_IHDR + 4, height);
  buf_IHDR[8] = 8;
  buf_IHDR[9] = coltype;
  buf_IHDR[10] = 0;
  buf_IHDR[11] = 0;
  buf_IHDR[12] = 0;

  png_save_uint_32(buf_acTL, visible);
  png_save_uint_32(buf_acTL + 4, loops);

  fwrite(png_sign, 1, 8, f);

  write_chunk(f, "IHDR", buf_IHDR, 13);

  if (img.size() > 1)
    write_chunk(f, "acTL", buf_acTL, 8);
  else
    first = 0;

  if (img[0].ps > 0 && coltype == 3)
    write_chunk(f, "PLTE", (unsigned char *)(&img[0].pl), img[0].ps * 3);

  /* tRNS is only allowed for gray, RGB and indexed images */
  if (img[0].ts > 0 && (coltype == 0 || coltype == 2 || coltype == 3))
    write_chunk(f, "tRNS", img[0].tr, img[0].ts);

  op_zstream1.data_type = Z_BINARY;
  op_zstream1.zalloc = Z_NULL;
  op_zstream1.zfree = Z_NULL;
  op_zstream1.opaque = Z_NULL;
  deflateInit2(&op_zstream1, Z_BEST_SPEED + 1, 8, 15, 8, Z_DEFAULT_STRATEGY);

  op_zstream2.data_type = Z_BINARY;
  op_zstream2.zalloc = Z_NULL;
  op_zstream2.zfree = Z_NULL;
  op_zstream2.opaque = Z_NULL;
  deflateInit2(&op_zstream2, Z_BEST_SPEED + 1, 8, 15, 8, Z_FILTERED);
#ifdef FEATURE_LIBDEFLATE
  if (deflate_method == 3)
    ld_compressor = libdeflate_alloc_compressor(zlib_level);
#endif

  unsigned int idat_size = (rowbytes + 1) * height;
  unsigned int zbuf_size =
      idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

  unsigned char *zbuf = new unsigned char[zbuf_size];
  op_zbuf1 = new unsigned char[zbuf_size];
  op_zbuf2 = new unsigned char[zbuf_size];
  row_buf = new unsigned char[rowbytes + 1];
  sub_row = new unsigned char[rowbytes + 1];
  up_row = new unsigned char[rowbytes + 1];
  avg_row = new unsigned char[rowbytes + 1];
  paeth_row = new unsigned char[rowbytes + 1];

  row_buf[0] = 0;
  sub_row[0] = 1;
  up_row[0] = 2;
  avg_row[0] = 3;
  paeth_row[0] = 4;

  unsigned int i, j, k;
  unsigned int zsize = 0;
  unsigned int x0 = 0;
  unsigned int y0 = 0;
  unsigned int w0 = width;
  unsigned int h0 = height;
  unsigned char bop = 0;
  unsigned char dop = 0;
  next_seq_num = 0;

  printf("saving %s (frame %d of %d)\n", szOut, 1 - first, visible);
  for (j = 0; j < 6; j++)
    op[j].valid = 0;
  deflate_rect_op(&img[0], x0, y0, w0, h0, bpp, zbuf_size, 0);
  deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size, 0);

  if (first) {
    write_IDATs(f, 0, zbuf, zsize, idat_size);

    printf("saving %s (frame %d of %d)\n", szOut, 1, visible);
    for (j = 0; j < 6; j++)
      op[j].valid = 0;
    deflate_rect_op(&img[1], x0, y0, w0, h0, bpp, zbuf_size, 0);
    deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size,
                     0);
  }

  for (i = first; i < img.size() - 1; i++) {
    unsigned int op_min;
    int op_best;

    printf("saving %s (frame %d of %d)\n", szOut, i - first + 2, visible);
    for (j = 0; j < 6; j++)
      op[j].valid = 0;

    /* dispose = none */
    get_rect(width, height, &img[i], &img[i + 1], &over1, bpp, zbuf_size,
             has_tcolor, tcolor, 0);

    /* dispose = background */
    if (has_tcolor) {
      for (j = 0; j < height; j++)
        memcpy(temp.rows[j], img[i].rows[j], rowbytes);
      if (coltype == 2)
        for (j = 0; j < h0; j++)
          for (k = 0; k < w0; k++)
            memcpy(temp.rows[j + y0] + (k + x0) * 3, &tcolor, 3);
      else
        for (j = 0; j < h0; j++)
          memset(temp.rows[j + y0] + x0 * bpp, tcolor, w0 * bpp);

      get_rect(width, height, &temp, &img[i + 1], &over2, bpp, zbuf_size,
               has_tcolor, tcolor, 1);
    }

    /* dispose = previous */
    if (i > first)
      get_rect(width, height, &rest, &img[i + 1], &over3, bpp, zbuf_size,
               has_tcolor, tcolor, 2);

    op_min = op[0].size;
    op_best = 0;
    for (j = 1; j < 6; j++)
      if (op[j].valid) {
        if (op[j].size < op_min) {
          op_min = op[j].size;
          op_best = j;
        }
      }

    dop = op_best >> 1;

    png_save_uint_32(buf_fcTL, next_seq_num++);
    png_save_uint_32(buf_fcTL + 4, w0);
    png_save_uint_32(buf_fcTL + 8, h0);
    png_save_uint_32(buf_fcTL + 12, x0);
    png_save_uint_32(buf_fcTL + 16, y0);
    save_delay(buf_fcTL + 20, img[i].delay_num, img[i].delay_den);
    buf_fcTL[24] = dop;
    buf_fcTL[25] = bop;
    write_chunk(f, "fcTL", buf_fcTL, 26);

    write_IDATs(f, i, zbuf, zsize, idat_size);

    /* process apng dispose - begin */
    if (dop != 2)
      for (j = 0; j < height; j++)
        memcpy(rest.rows[j], img[i].rows[j], rowbytes);

    if (dop == 1) {
      if (coltype == 2)
        for (j = 0; j < h0; j++)
          for (k = 0; k < w0; k++)
            memcpy(rest.rows[j + y0] + (k + x0) * 3, &tcolor, 3);
      else
        for (j = 0; j < h0; j++)
          memset(rest.rows[j + y0] + x0 * bpp, tcolor, w0 * bpp);
    }
    /* process apng dispose - end */

    x0 = op[op_best].x;
    y0 = op[op_best].y;
    w0 = op[op_best].w;
    h0 = op[op_best].h;
    bop = op_best & 1;

    deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size,
                     op_best);
  }

  if (img.size() > 1) {
    png_save_uint_32(buf_fcTL, next_seq_num++);
    png_save_uint_32(buf_fcTL + 4, w0);
    png_save_uint_32(buf_fcTL + 8, h0);
    png_save_uint_32(buf_fcTL + 12, x0);
    png_save_uint_32(buf_fcTL + 16, y0);
    save_delay(buf_fcTL + 20, img.back().delay_num, img.back().delay_den);
    buf_fcTL[24] = 0;
    buf_fcTL[25] = bop;
    write_chunk(f, "fcTL", buf_fcTL, 26);
  }

  write_IDATs(f, (unsigned int)(img.size() - 1), zbuf, zsize, idat_size);

  write_chunk(f, "tEXt", (unsigned char *)png_Software,
              (unsigned int)sizeof(png_Software) - 1);
  write_chunk(f, "IEND", 0, 0);
  fclose(f);

  delete[] zbuf;
  delete[] op_zbuf1;
  delete[] op_zbuf2;
  delete[] row_buf;
  delete[] sub_row;
  delete[] up_row;
  delete[] avg_row;
  delete[] paeth_row;

  deflateEnd(&op_zstream1);
  deflateEnd(&op_zstream2);
#ifdef FEATURE_LIBDEFLATE
  if (ld_compressor != NULL) {
    libdeflate_free_compressor(ld_compressor);
    ld_compressor = NULL;
  }
#endif

  temp.free();
  over1.free();
  over2.free();
  over3.free();
  rest.free();
  delete[] dest;

  return 0;
}

struct FrameItem {
  std::string path;
  int delay_num;
  int delay_den;
  int explicit_ms; /* delay was given with -dN */
};

static bool is_number_str(const char *s) {
  if (!s || !*s)
    return false;
  while (*s) {
    if (*s < '0' || *s > '9')
      return false;
    s++;
  }
  return true;
}

static void optim_fuzz(std::vector<Image> &img, unsigned int first, int fuzz) {
  if (fuzz <= 0 || img.size() <= first + 1)
    return;
  if (fuzz > 100)
    fuzz = 100;

  unsigned int w = img[first].w;
  unsigned int h = img[first].h;
  unsigned int bpp = img[first].bpp;
  unsigned int type = img[first].type;
  size_t npix = (size_t)w * h;
  size_t nbytes = npix * bpp;

  /* Pixel distances are integers, so "d <= t" is the same as "d <= floor(t)":
     compare in integer math instead of converting every pixel to double. */
  double thresh = (fuzz / 100.0) * 255.0;
  double t1d = thresh * thresh;
  long long t1 = (long long)t1d;
  long long t2 = (long long)(2.0 * t1d);
  long long t3 = (long long)(3.0 * t1d);
  long long t4 = (long long)(4.0 * t1d);

  std::vector<unsigned char> canvas(img[first].p, img[first].p + nbytes);

  for (size_t k = first + 1; k < img.size(); k++) {
    if (img[k].w != w || img[k].h != h || img[k].bpp != bpp || img[k].type != type)
      continue;
    unsigned char *cur = img[k].p;
    unsigned char *ref = canvas.data();

    if (type == 6 && bpp == 4) {
      for (size_t i = 0; i < nbytes; i += 4) {
        int a1 = cur[i + 3], a2 = ref[i + 3];
        if (a1 == 0 && a2 == 0) {
          cur[i] = ref[i];
          cur[i + 1] = ref[i + 1];
          cur[i + 2] = ref[i + 2];
          cur[i + 3] = 0;
          continue;
        }
        int da = a1 - a2;
        if ((long long)(da * da) <= t1) {
          int dr = (int)cur[i] - (int)ref[i];
          int dg = (int)cur[i + 1] - (int)ref[i + 1];
          int db = (int)cur[i + 2] - (int)ref[i + 2];
          if ((long long)(dr * dr + dg * dg + db * db + da * da) <= t4) {
            cur[i] = ref[i];
            cur[i + 1] = ref[i + 1];
            cur[i + 2] = ref[i + 2];
            cur[i + 3] = ref[i + 3];
            continue;
          }
        }
        ref[i] = cur[i];
        ref[i + 1] = cur[i + 1];
        ref[i + 2] = cur[i + 2];
        ref[i + 3] = cur[i + 3];
      }
    } else if (type == 2 && bpp == 3) {
      for (size_t i = 0; i < nbytes; i += 3) {
        int dr = (int)cur[i] - (int)ref[i];
        int dg = (int)cur[i + 1] - (int)ref[i + 1];
        int db = (int)cur[i + 2] - (int)ref[i + 2];
        if ((long long)(dr * dr + dg * dg + db * db) <= t3) {
          cur[i] = ref[i];
          cur[i + 1] = ref[i + 1];
          cur[i + 2] = ref[i + 2];
        } else {
          ref[i] = cur[i];
          ref[i + 1] = cur[i + 1];
          ref[i + 2] = cur[i + 2];
        }
      }
    } else if (type == 4 && bpp == 2) {
      for (size_t i = 0; i < nbytes; i += 2) {
        int a1 = cur[i + 1], a2 = ref[i + 1];
        if (a1 == 0 && a2 == 0) {
          cur[i] = ref[i];
          cur[i + 1] = 0;
          continue;
        }
        int da = a1 - a2;
        if ((long long)(da * da) <= t1) {
          int dg = (int)cur[i] - (int)ref[i];
          if ((long long)(dg * dg + da * da) <= t2) {
            cur[i] = ref[i];
            cur[i + 1] = ref[i + 1];
            continue;
          }
        }
        ref[i] = cur[i];
        ref[i + 1] = cur[i + 1];
      }
    } else if (type == 0 && bpp == 1) {
      for (size_t i = 0; i < nbytes; i++) {
        int dg = (int)cur[i] - (int)ref[i];
        if ((long long)(dg * dg) <= t1) {
          cur[i] = ref[i];
        } else {
          ref[i] = cur[i];
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  std::vector<Image> img;
  std::vector<FrameItem> frame_items;
  std::string szOutStr;
  unsigned int i;
  unsigned int first = 0;
  unsigned int loops = 0;
  int delay_num = -1;
  int delay_den = -1;
  int hs = 0;
  int vs = 0;
  int keep_palette = 0;
  int keep_coltype = 0;
  int deflate_method = 0;
  int iter = 15;
  int fuzz = 0;
  int res = 0;

  printf("\nAPNG Assembler " APNGASM_VERSION);

#ifdef FEATURE_ZOPFLI
  deflate_method = 2;
#endif
#ifdef FEATURE_7ZIP
  deflate_method = 1;
#endif

  if (argc <= 1) {
    printf("\n\nUsage   : apngasm output.png frame001.png [options]\n"
           "          apngasm output.png frame*.png   [options]\n"
           "          apngasm -o output.png frame1.png 50 frame2.png 50 "
           "[options]\n\n"
           "Options :\n"
           "1 10    : frame delay is 1/10 sec. (default)\n"
           "-o file : output file\n"
           "file ## : (multiple input files) delay of that frame in ms\n"
           "-d##    : delay of the preceding input file in ms\n"
           "-l2     : 2 loops (default is 0, forever)\n"
           "-f      : skip the first frame\n"
           "-hs##   : input is horizontal strip of ## frames (example: -hs12)\n"
           "-vs##   : input is vertical strip of ## frames   (example: -vs12)\n"
           "-kp     : keep palette\n"
           "-kc     : keep color type\n"
           "--fuzz N: lossy inter-frame optimization, merge pixels that differ by "
           "up to N%% (0-100)\n"
           "-z0     : zlib compression\n"
           "-zl#    : compression level for -z0 (1-9, default 9) or -z3 "
           "(1-12, default 12)\n");
#ifdef FEATURE_7ZIP
    printf("-z1     : 7zip compression%s\n",
           (deflate_method == 1) ? " (default)" : "");
#endif
#ifdef FEATURE_ZOPFLI
    printf("-z2     : Zopfli compression%s\n",
           (deflate_method == 2) ? " (default)" : "");
#endif
#ifdef FEATURE_LIBDEFLATE
    printf("-z3     : libdeflate compression%s\n",
           (deflate_method == 3) ? " (default)" : "");
#endif
    if (deflate_method)
      printf("-i##    : number of iterations (default -i%d)\n", iter);
    return 1;
  }

  for (int c = 1; c < argc; c++) {
    char *arg = argv[c];

    if (strcmp(arg, "-o") == 0 && c + 1 < argc) {
      szOutStr = argv[++c];
    } else if (strncmp(arg, "-o", 2) == 0 && strlen(arg) > 2) {
      szOutStr = arg + 2;
    } else if ((strcmp(arg, "-l") == 0 || strcmp(arg, "-loop") == 0) &&
               c + 1 < argc) {
      loops = atoi(argv[++c]);
    } else if (strncmp(arg, "-l", 2) == 0 && isdigit((unsigned char)arg[2])) {
      loops = atoi(arg + 2);
    } else if (strncmp(arg, "-f", 2) == 0 || strncmp(arg, "-F", 2) == 0) {
      first = 1;
    } else if (strncmp(arg, "-kp", 3) == 0 || strncmp(arg, "-KP", 3) == 0) {
      keep_palette = 1;
    } else if (strncmp(arg, "-kc", 3) == 0 || strncmp(arg, "-KC", 3) == 0) {
      keep_coltype = 1;
    } else if ((strncmp(arg, "-zl", 3) == 0 || strncmp(arg, "-ZL", 3) == 0) &&
               isdigit((unsigned char)arg[3])) {
      zlib_level = atoi(arg + 3);
      if (zlib_level < 1)
        zlib_level = 1;
      if (zlib_level > 12)
        zlib_level = 12;
    } else if (strncmp(arg, "-z", 2) == 0 || strncmp(arg, "-Z", 2) == 0) {
      deflate_method = atoi(arg + 2);
    } else if (strncmp(arg, "-i", 2) == 0 || strncmp(arg, "-I", 2) == 0) {
      iter = atoi(arg + 2);
      if (iter < 1)
        iter = 1;
    } else if (strncmp(arg, "-hs", 3) == 0 || strncmp(arg, "-HS", 3) == 0) {
      hs = atoi(arg + 3);
      if (hs < 1)
        hs = 1;
    } else if (strncmp(arg, "-vs", 3) == 0 || strncmp(arg, "-VS", 3) == 0) {
      vs = atoi(arg + 3);
      if (vs < 1)
        vs = 1;
    } else if (strcmp(arg, "--fuzz") == 0 && c + 1 < argc) {
      fuzz = atoi(argv[++c]);
      if (fuzz < 0) fuzz = 0;
      if (fuzz > 100) fuzz = 100;
    } else if (strncmp(arg, "--fuzz=", 7) == 0) {
      fuzz = atoi(arg + 7);
      if (fuzz < 0) fuzz = 0;
      if (fuzz > 100) fuzz = 100;
    } else if (strncmp(arg, "-d", 2) == 0 && strlen(arg) > 2) {
      int d = atoi(arg + 2);
      if (!frame_items.empty()) {
        frame_items.back().delay_num = d;
        frame_items.back().delay_den = 1000;
        frame_items.back().explicit_ms = 1;
      }
    } else if (is_number_str(arg)) {
      int val = atoi(arg);
      if (!frame_items.empty() && frame_items.back().delay_num == -1) {
        frame_items.back().delay_num = val;
        frame_items.back().delay_den = 1000;
      } else if (delay_num == -1) {
        delay_num = val;
      } else if (delay_den == -1) {
        delay_den = val;
      }
    } else {
      if (szOutStr.empty()) {
        szOutStr = arg;
      } else {
        FrameItem item;
        item.path = arg;
        item.delay_num = -1;
        item.delay_den = -1;
        item.explicit_ms = 0;
        frame_items.push_back(item);
      }
    }
  }

  if (szOutStr.empty() || frame_items.empty()) {
    printf("\nError: Missing output file or input image specification\n");
    return 1;
  }

  /* Original syntax: "apngasm out.png frame01.png 3 4" means 3/4 s for every
     frame of the frame01.png, frame02.png, ... sequence. With a single input the
     numbers after it are that global delay, not a per-frame delay in ms. */
  if (frame_items.size() == 1 && frame_items[0].delay_num != -1 &&
      !frame_items[0].explicit_ms) {
    delay_den = delay_num;
    delay_num = frame_items[0].delay_num;
    frame_items[0].delay_num = frame_items[0].delay_den = -1;
  }

  if (delay_num <= 0)
    delay_num = 1;
  if (delay_den <= 0)
    delay_den = 10;

  if (deflate_method == 3) {
    if (zlib_level < 1)
      zlib_level = 12;
    if (zlib_level > 12)
      zlib_level = 12;
  } else {
    if (zlib_level < 1)
      zlib_level = Z_BEST_COMPRESSION;
    if (zlib_level > 9)
      zlib_level = 9;
  }

  if (deflate_method == 0)
    printf(" using ZLIB level %d\n\n", zlib_level);
  else if (deflate_method == 1)
    printf(" using 7ZIP with %d iterations\n\n", iter);
  else if (deflate_method == 2)
    printf(" using ZOPFLI with %d iterations\n\n", iter);
#ifdef FEATURE_LIBDEFLATE
  else if (deflate_method == 3)
    printf(" using LIBDEFLATE level %d\n\n", zlib_level);
#endif

  unsigned char coltype = 6;

  if (frame_items.size() > 1) {
    img.resize(frame_items.size());
    for (size_t k = 0; k < frame_items.size(); k++) {
      printf("loading %s (%zu of %zu)\n", frame_items[k].path.c_str(), k + 1,
             frame_items.size());
      if (load_image((char *)frame_items[k].path.c_str(), &img[k])) {
        printf("Error: can't open image '%s'\n", frame_items[k].path.c_str());
        return 1;
      }
      img[k].delay_num =
          (frame_items[k].delay_num > 0) ? frame_items[k].delay_num : delay_num;
      img[k].delay_den =
          (frame_items[k].delay_den > 0) ? frame_items[k].delay_den : delay_den;
      /* all frames must share the first frame's size, otherwise the frame
         diffing code reads/writes past the end of the smaller buffers */
      if (img[k].w != img[0].w || img[k].h != img[0].h) {
        printf("Error: '%s' is %ux%u, expected %ux%u (all frames must have "
               "the same size)\n",
               frame_items[k].path.c_str(), img[k].w, img[k].h, img[0].w,
               img[0].h);
        return 1;
      }
    }
    coltype = find_common_coltype(img);
  } else if (frame_items.size() == 1) {
    char *szImage = (char *)frame_items[0].path.c_str();
    if (vs > 1)
      res = load_from_vertical_strip(szImage, vs, img, delay_num, delay_den,
                                     &coltype);
    else if (hs > 1)
      res = load_from_horizontal_strip(szImage, hs, img, delay_num, delay_den,
                                       &coltype);
    else
      res = load_image_sequence(szImage, first, img, delay_num, delay_den,
                                &coltype);

    if (res)
      return 1;
  }

  if (fuzz > 0 && coltype == 3 && !keep_coltype) {
    coltype = 6;
  }

  for (i = 0; i < img.size(); i++) {
    if (img[i].type != coltype)
      optim_upconvert(&img[i], coltype);
  }

  if (coltype == 6 || coltype == 4) {
    for (i = 0; i < img.size(); i++)
      optim_dirty_transp(&img[i]);
  }

  if (fuzz > 0) {
    optim_fuzz(img, first, fuzz);
  }

  optim_duplicates(img, first);

  if (!keep_coltype)
    optim_downconvert(img);

  coltype = img[0].type;

  if (coltype == 3 && !keep_palette)
    optim_palette(img);

  if (coltype == 2 || coltype == 0)
    optim_add_transp(img);

  res = save_apng((char *)szOutStr.c_str(), img, loops, first, deflate_method,
                  iter);

  for (i = 0; i < img.size(); i++)
    img[i].free();

  if (res)
    return 1;

  printf("all done\n");

  return 0;
}
