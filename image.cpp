/* Image library
 *
 * Copyright (c) 2016 Max Stepin
 * maxst at users.sourceforge.net
 *
 * Modified 2026 by Madars (ezgif.com): performance improvements and bug
 * fixes, see README.md. This is an altered version of the original.
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
#include <stdio.h>
#include <stdlib.h>
#include "image.h"
#include "png.h"
#include "zlib.h"

/* Small open-addressing hash: packed color -> palette index. Replaces the
 * linear search over up to 256 palette entries that was done for every pixel
 * of every frame during color type reduction. Holds at most 257 entries. */
struct ColorMap
{
  enum { SIZE = 1024, MASK = SIZE - 1 };
  unsigned int key[SIZE];
  short val[SIZE];
  ColorMap() { memset(val, -1, sizeof(val)); }
  static unsigned int slot(unsigned int k) { return (k * 2654435761u) >> 22; }
  int find(unsigned int k) const
  {
    for (unsigned int s = slot(k); val[s] >= 0; s = (s + 1) & MASK)
      if (key[s] == k)
        return val[s];
    return -1;
  }
  void insert(unsigned int k, int v)
  {
    unsigned int s = slot(k);
    while (val[s] >= 0 && key[s] != k)
      s = (s + 1) & MASK;
    key[s] = k;
    val[s] = (short)v;
  }
};

static inline unsigned int pack_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  return (unsigned int)r | ((unsigned int)g << 8) | ((unsigned int)b << 16) | ((unsigned int)a << 24);
}


int load_png(char * szName, Image * image)
{
  FILE * f;
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop  info_ptr = png_create_info_struct(png_ptr);

  if (!png_ptr || !info_ptr)
    return 1;

  if ((f = fopen(szName, "rb")) == 0)
  {
    printf("Error: can't open '%s'\n", szName);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return 1;
  }

  if (setjmp(png_jmpbuf(png_ptr)))
  {
    printf("Error: can't load '%s'\n", szName);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(f);
    return 1;
  }

  png_init_io(png_ptr, f);
  png_read_info(png_ptr, info_ptr);
  unsigned int depth = png_get_bit_depth(png_ptr, info_ptr);
  if (depth < 8)
  {
    if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_PALETTE)
      png_set_packing(png_ptr);
    else
      png_set_expand(png_ptr);
  }
  else
  if (depth > 8)
  {
    png_set_expand(png_ptr);
    png_set_strip_16(png_ptr);
  }
  (void)png_set_interlace_handling(png_ptr);
  png_read_update_info(png_ptr, info_ptr);
  unsigned int w = png_get_image_width(png_ptr, info_ptr);
  unsigned int h = png_get_image_height(png_ptr, info_ptr);
  unsigned int bpp = png_get_channels(png_ptr, info_ptr);
  unsigned int type = png_get_color_type(png_ptr, info_ptr);
  image->init(w, h, bpp, type);
  if (!image->p)
  {
    /* init() refuses zero/oversized dimensions - bail out instead of letting
       png_read_image() write through a NULL row table */
    printf("Error: unsupported image size in '%s'\n", szName);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(f);
    return 1;
  }

  png_colorp palette;
  png_color_16p trans_color;
  png_bytep trans_alpha;

  /* RGB(A) files may carry a "suggested palette" PLTE; only keep a real one */
  if (type == PNG_COLOR_TYPE_PALETTE && png_get_PLTE(png_ptr, info_ptr, &palette, &image->ps))
    memcpy(image->pl, palette, image->ps * 3);

  if (png_get_tRNS(png_ptr, info_ptr, &trans_alpha, &image->ts, &trans_color) && image->ts > 0)
  {
    if (type == PNG_COLOR_TYPE_GRAY)
    {
      image->tr[0] = 0;
      image->tr[1] = trans_color->gray & 0xFF;
      image->ts = 2;
    }
    else
    if (type == PNG_COLOR_TYPE_RGB)
    {
      image->tr[0] = 0;
      image->tr[1] = trans_color->red & 0xFF;
      image->tr[2] = 0;
      image->tr[3] = trans_color->green & 0xFF;
      image->tr[4] = 0;
      image->tr[5] = trans_color->blue & 0xFF;
      image->ts = 6;
    }
    else
    if (type == PNG_COLOR_TYPE_PALETTE)
      memcpy(image->tr, trans_alpha, image->ts);
  }

  png_read_image(png_ptr, image->rows);
  png_read_end(png_ptr, info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  fclose(f);
  return 0;
}

int load_tga(char * szName, Image * image)
{
  FILE * f;
  unsigned int i, j, k, n;
  unsigned int w, h;
  unsigned int compressed, top_bottom;
  unsigned char c;
  unsigned char col[4];
  unsigned char header[18];

  if ((f = fopen(szName, "rb")) == 0)
  {
    printf("Error: can't open '%s'\n", szName);
    return 1;
  }

  if (fread(&header, 1, 18, f) != 18)
    goto fail;

  w = header[12] + header[13]*256;
  h = header[14] + header[15]*256;
  if (w == 0 || h == 0 || w > 65535 || h > 65535)
    goto fail;
  compressed = header[2] & 8;
  top_bottom = header[17] & 0x20;

  if ((header[2] & 7) == 1 && header[16] == 8 && header[1] == 1 && header[7] == 24)
    image->init(w, h, 1, 3);
  else
  if ((header[2] & 7) == 3 && header[16] == 8)
    image->init(w, h, 1, 0);
  else
  if ((header[2] & 7) == 2 && header[16] == 24)
    image->init(w, h, 3, 2);
  else
  if ((header[2] & 7) == 2 && header[16] == 32)
    image->init(w, h, 4, 6);
  else
    goto fail;

  if (!image->p)
    goto fail;

  if (header[0] != 0)
    fseek(f, header[0], SEEK_CUR);

  if (header[1] == 1)
  {
    unsigned int start = header[3] + header[4]*256;
    unsigned int size  = header[5] + header[6]*256;
    for (i=start; i<start+size && i<256; i++)
    {
      if (fread(&col, 1, 3, f) != 3)
        goto fail;
      image->pl[i].r = col[2];
      image->pl[i].g = col[1];
      image->pl[i].b = col[0];
    }
    image->ps = i;
    if (start+size > 256)
      fseek(f, (start+size-256)*3, SEEK_CUR);
  }

  for (j=0; j<h; j++)
  {
    unsigned char * row = image->rows[(top_bottom) ? j : h-1-j];
    if (compressed == 0)
    {
      if (image->bpp >= 3)
      {
        for (i=0; i<w; i++)
        {
          if (fread(&col, 1, image->bpp, f) != image->bpp)
            goto fail;
          *row++ = col[2];
          *row++ = col[1];
          *row++ = col[0];
          if (image->bpp == 4)
            *row++ = col[3];
        }
      }
      else
      {
        if (fread(row, 1, w, f) != w)
          goto fail;
      }
    }
    else
    {
      i = 0;
      while (i < w)
      {
        if (fread(&c, 1, 1, f) != 1)
          goto fail;
        n = (c & 0x7F)+1;
        if (i + n > w)
          goto fail;

        if ((c & 0x80) != 0)
        {
          if (image->bpp >= 3)
          {
            if (fread(&col, 1, image->bpp, f) != image->bpp)
              goto fail;
            for (k=0; k<n; k++)
            {
              *row++ = col[2];
              *row++ = col[1];
              *row++ = col[0];
              if (image->bpp == 4)
                *row++ = col[3];
            }
          }
          else
          {
            if (fread(&col, 1, 1, f) != 1)
              goto fail;
            memset(row, col[0], n);
            row += n;
          }
        }
        else
        {
          if (image->bpp >= 3)
          {
            for (k=0; k<n; k++)
            {
              if (fread(&col, 1, image->bpp, f) != image->bpp)
                goto fail;
              *row++ = col[2];
              *row++ = col[1];
              *row++ = col[0];
              if (image->bpp == 4)
                *row++ = col[3];
            }
          }
          else
          {
            if (fread(row, 1, n, f) != n)
              goto fail;
            row += n;
          }
        }
        i+=n;
      }
    }
  }
  fclose(f);
  return 0;
fail:
  printf("Error: can't load '%s'\n", szName);
  fclose(f);
  return 1;
}

int load_image(char * szName, Image * image)
{
  FILE * f;

  if ((f = fopen(szName, "rb")) != 0)
  {
    unsigned int sign;
    size_t res = fread(&sign, sizeof(sign), 1, f);
    fclose(f);

    if (res == 1)
    {
      if (sign == 0x474E5089)
        return load_png(szName, image);
      else
        return load_tga(szName, image);
    }
  }

  printf("Error: can't load '%s'\n", szName);
  return 1;
}

unsigned char find_common_coltype(std::vector<Image>& img)
{
  unsigned char coltype = img[0].type;

  for (size_t i=1; i<img.size(); i++)
  {
    if (img[0].ps != img[i].ps || memcmp(img[0].pl, img[i].pl, img[0].ps*3) != 0)
      coltype = 6;
    else
    if (img[0].ts != img[i].ts || memcmp(img[0].tr, img[i].tr, img[0].ts) != 0)
      coltype = 6;
    else
    if (img[i].type != 3)
    {
      if (coltype != 3)
        coltype |= img[i].type;
      else
        coltype = 6;
    }
    else
      if (coltype != 3)
        coltype = 6;
  }
  return coltype;
}

void up0to6(Image * image)
{
  image->type = 6;
  image->bpp = 4;
  unsigned int x, y;
  unsigned char g, a;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      g = *sp++;
      a = (image->ts > 0 && image->tr[1] == g) ? 0 : 255;
      *dp++ = g;
      *dp++ = g;
      *dp++ = g;
      *dp++ = a;
    }
  }
  delete[] image->p;
  image->p = dst;
}

void up2to6(Image * image)
{
  image->type = 6;
  image->bpp = 4;
  unsigned int x, y;
  unsigned char r, g, b, a;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      r = *sp++;
      g = *sp++;
      b = *sp++;
      a = (image->ts > 0 && image->tr[1] == r && image->tr[3] == g && image->tr[5] == b) ? 0 : 255;
      *dp++ = r;
      *dp++ = g;
      *dp++ = b;
      *dp++ = a;
    }
  }
  delete[] image->p;
  image->p = dst;
}

void up3to6(Image * image)
{
  image->type = 6;
  image->bpp = 4;
  unsigned int x, y;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      *dp++ = image->pl[*sp].r;
      *dp++ = image->pl[*sp].g;
      *dp++ = image->pl[*sp].b;
      *dp++ = image->tr[*sp++];
    }
  }
  delete[] image->p;
  image->p = dst;
}

void up4to6(Image * image)
{
  image->type = 6;
  image->bpp = 4;
  unsigned int x, y;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      *dp++ = *sp;
      *dp++ = *sp;
      *dp++ = *sp++;
      *dp++ = *sp++;
    }
  }
  delete[] image->p;
  image->p = dst;
}

void up0to4(Image * image)
{
  image->type = 4;
  image->bpp = 2;
  unsigned int x, y;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      unsigned char g = *sp++;
      *dp++ = g;
      *dp++ = (image->ts > 0 && image->tr[1] == g) ? 0 : 255;
    }
  }
  delete[] image->p;
  image->p = dst;
}

void up0to2(Image * image)
{
  image->type = 2;
  image->bpp = 3;
  unsigned int x, y;
  unsigned char * sp, * dp;
  unsigned int rowbytes = image->w *image->bpp;
  unsigned char * dst = new unsigned char[image->h * rowbytes];

  for (y=0; y<image->h; y++)
  {
    sp = image->rows[y];
    image->rows[y] = (y == 0) ? dst : image->rows[y-1] + rowbytes;
    dp = image->rows[y];
    for (x=0; x<image->w; x++)
    {
      *dp++ = *sp;
      *dp++ = *sp;
      *dp++ = *sp++;
    }
  }
  delete[] image->p;
  image->p = dst;

  /* gray tRNS (2 bytes) -> RGB tRNS (6 bytes) */
  if (image->ts > 0)
  {
    image->tr[0] = image->tr[2] = image->tr[4] = 0;
    image->tr[3] = image->tr[5] = image->tr[1];
    image->ts = 6;
  }
}

void optim_upconvert(Image * image, unsigned char coltype)
{
  if (image->type == 0 && coltype == 6)
    up0to6(image);
  else
  if (image->type == 2 && coltype == 6)
    up2to6(image);
  else
  if (image->type == 3 && coltype == 6)
    up3to6(image);
  else
  if (image->type == 4 && coltype == 6)
    up4to6(image);
  else
  if (image->type == 0 && coltype == 4)
    up0to4(image);
  else
  if (image->type == 0 && coltype == 2)
    up0to2(image);

  /* transparency is now in the alpha channel; drop stale PLTE/tRNS data so
     they don't end up as (invalid) chunks in a gray+alpha / RGBA file */
  if (image->type == 4 || image->type == 6)
  {
    image->ps = 0;
    image->ts = 0;
  }
}

void optim_dirty_transp(Image * image)
{
  unsigned int x, y;
  if (image->type == 6)
  {
    for (y=0; y<image->h; y++)
    {
      unsigned char * sp = image->rows[y];
      for (x=0; x<image->w; x++, sp+=4)
      {
        if (sp[3] == 0)
           sp[0] = sp[1] = sp[2] = 0;
      }
    }
  }
  else
  if (image->type == 4)
  {
    for (y=0; y<image->h; y++)
    {
      unsigned char * sp = image->rows[y];
      for (x=0; x<image->w; x++, sp+=2)
      {
        if (sp[1] == 0)
          sp[0] = 0;
      }
    }
  }
}

int different(Image * image1, Image * image2)
{
  return memcmp(image1->p, image2->p, (size_t)image1->w * image1->h * image1->bpp);
}

void optim_duplicates(std::vector<Image>& img, unsigned int first)
{
  unsigned int i = first;

  while (++i < img.size())
  {
    if (different(&img[i-1], &img[i]))
      continue;

    i--;
    unsigned int num = img[i].delay_num;
    unsigned int den = img[i].delay_den;
      
    img[i].free();
    img.erase(img.begin() + i);

    if (img[i].delay_den == den)
      img[i].delay_num += num;
    else
    {
      /* 64-bit: num*den products of 16-bit-ish values overflow 32 bits */
      unsigned long long n = (unsigned long long)num*img[i].delay_den + (unsigned long long)den*img[i].delay_num;
      unsigned long long d = (unsigned long long)den*img[i].delay_den;
      unsigned long long a = n, b = d;
      while (b)
      {
        unsigned long long t = a % b;
        a = b;
        b = t;
      }
      if (a == 0)
        a = 1;
      n /= a;
      d /= a;
      /* keep it representable; save_apng() scales it to 16 bits */
      while (n > 0xFFFFFFFFull || d > 0xFFFFFFFFull)
      {
        n = (n + 1) / 2;
        d = (d + 1) / 2;
      }
      img[i].delay_num = (unsigned int)n;
      img[i].delay_den = d ? (unsigned int)d : 1;
    }
  }
}

typedef struct { unsigned int num; unsigned char r, g, b, a; } COLORS;

/* Count a color in col[]/colors with the same result as the original linear
 * search: stop tracking once more than 256 distinct colors were seen.
 * last_key/last_idx cache the previous pixel (runs of equal pixels are the
 * common case). */
static inline void count_color(ColorMap & map, COLORS * col, unsigned int & colors,
                               unsigned int & last_key, int & last_idx,
                               unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  if (colors > 256)
    return;
  unsigned int key = pack_rgba(r, g, b, a);
  int k = (last_idx >= 0 && key == last_key) ? last_idx : map.find(key);
  if (k >= 0)
  {
    col[k].num++;
    last_key = key;
    last_idx = k;
    return;
  }
  if (colors < 256)
  {
    col[colors].num++;
    col[colors].r = r;
    col[colors].g = g;
    col[colors].b = b;
    col[colors].a = a;
    map.insert(key, colors);
    last_key = key;
    last_idx = colors;
  }
  colors++;
}

/* index of each (sorted) palette entry, for the pixel remapping pass */
static void build_index_map(ColorMap & map, COLORS * col, unsigned int colors)
{
  /* insert in reverse so the first matching entry wins, like the linear search */
  for (int k = (int)colors - 1; k >= 0; k--)
    map.insert(pack_rgba(col[k].r, col[k].g, col[k].b, col[k].a), k);
}

static inline unsigned char lookup_index(const ColorMap & map, unsigned int colors,
                                         unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  int k = map.find(pack_rgba(r, g, b, a));
  return (unsigned char)(k >= 0 ? k : colors);
}

int cmp_colors(const void *arg1, const void *arg2)
{
  if ( ((COLORS*)arg1)->a != ((COLORS*)arg2)->a )
    return (int)(((COLORS*)arg1)->a) - (int)(((COLORS*)arg2)->a);

  if ( ((COLORS*)arg1)->num != ((COLORS*)arg2)->num )
    return (int)(((COLORS*)arg2)->num) - (int)(((COLORS*)arg1)->num);

  if ( ((COLORS*)arg1)->r != ((COLORS*)arg2)->r )
    return (int)(((COLORS*)arg1)->r) - (int)(((COLORS*)arg2)->r);

  if ( ((COLORS*)arg1)->g != ((COLORS*)arg2)->g )
    return (int)(((COLORS*)arg1)->g) - (int)(((COLORS*)arg2)->g);

  return (int)(((COLORS*)arg1)->b) - (int)(((COLORS*)arg2)->b);
}

void down6(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char * dp;
  unsigned char r, g, b, a;
  int simple_transp = 1;
  int full_transp = 0;
  int grayscale = 1;
  unsigned char cube[4096];
  unsigned char gray[256];
  COLORS col[256];
  unsigned int colors = 0;
  ColorMap cmap;
  unsigned int last_key = 0;
  int last_idx = -1;
  Image * image = &img[0];

  memset(&cube, 0, sizeof(cube));
  memset(&gray, 0, sizeof(gray));

  for (i=0; i<256; i++)
  {
    col[i].num = 0;
    col[i].r = col[i].g = col[i].b = i;
    col[i].a = image->tr[i] = 255;
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;
        a = *sp++;

        if (a != 0)
        {
          if (a != 255)
            simple_transp = 0;
          else
            if (((r | g | b) & 15) == 0)
              cube[(r<<4) + g + (b>>4)] = 1;

          if (r != g || g != b)
            grayscale = 0;
          else
            gray[r] = 1;
        }
        else
          full_transp = 1;

        count_color(cmap, col, colors, last_key, last_idx, r, g, b, a);
      }
    }
  }

  if (colors <= 256)
  {
    if (grayscale && simple_transp && colors > 128) /* 6 -> 0 */
    {
      image->type = 0;
      image->bpp = 1;
      unsigned char t = 0;

      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        image->tr[0] = 0;
        image->tr[1] = t = i;
        image->ts = 2;
        break;
      }

      for (i=0; i<img.size(); i++)
      {
        for (y=0; y<image->h; y++)
        {
          sp = dp = img[i].rows[y];
          for (x=0; x<image->w; x++, sp+=4)
          {
            *dp++ = (sp[3] == 0) ? t : sp[0];
          }
        }
      }
    }
    else /* 6 -> 3 */
    {
      image->type = 3;
      image->bpp = 1;

      if (full_transp == 0 && colors < 256)
        col[colors++].a = 0;

      qsort(&col[0], colors, sizeof(COLORS), cmp_colors);
      ColorMap imap;
      build_index_map(imap, col, colors);

      for (i=0; i<img.size(); i++)
      {
        for (y=0; y<image->h; y++)
        {
          sp = dp = img[i].rows[y];
          for (x=0; x<image->w; x++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            a = *sp++;
            *dp++ = lookup_index(imap, colors, r, g, b, a);
          }
        }
      }

      image->ps = colors;
      image->ts = 0; /* recomputed below; drop any stale RGB tRNS length */
      for (i=0; i<colors; i++)
      {
        image->pl[i].r = col[i].r;
        image->pl[i].g = col[i].g;
        image->pl[i].b = col[i].b;
        image->tr[i]   = col[i].a;
        if (image->tr[i] != 255) 
          image->ts = i+1;
      }
    }
  }
  else
  if (grayscale)     /* 6 -> 4 */
  {
    image->type = 4;
    image->bpp = 2;
    for (i=0; i<img.size(); i++)
    {
      for (y=0; y<image->h; y++)
      {
        sp = dp = img[i].rows[y];
        for (x=0; x<image->w; x++, sp+=4)
        {
          *dp++ = sp[2];
          *dp++ = sp[3];
        }
      }
    }
  }
  else
  if (simple_transp)  /* 6 -> 2 */
  {
    for (i=0; i<4096; i++)
    if (cube[i] == 0)
    {
      image->tr[0] = 0;
      image->tr[1] = (i>>4)&0xF0;
      image->tr[2] = 0;
      image->tr[3] = i&0xF0;
      image->tr[4] = 0;
      image->tr[5] = (i<<4)&0xF0;
      image->ts = 6;
      break;
    }
    if (image->ts != 0)
    {
      image->type = 2;
      image->bpp = 3;
      for (i=0; i<img.size(); i++)
      {
        for (y=0; y<image->h; y++)
        {
          sp = dp = img[i].rows[y];
          for (x=0; x<image->w; x++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            a = *sp++;
            if (a == 0)
            {
              *dp++ = image->tr[1];
              *dp++ = image->tr[3];
              *dp++ = image->tr[5];
            }
            else
            {
              *dp++ = r;
              *dp++ = g;
              *dp++ = b;
            }
          }
        }
      }
    }
  }
}

void down2(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char * dp;
  unsigned char r, g, b, a;
  int full_transp = 0;
  int grayscale = 1;
  unsigned char gray[256];
  COLORS col[256];
  unsigned int colors = 0;
  ColorMap cmap;
  unsigned int last_key = 0;
  int last_idx = -1;
  Image * image = &img[0];

  memset(&gray, 0, sizeof(gray));

  for (i=0; i<256; i++)
  {
    col[i].num = 0;
    col[i].r = col[i].g = col[i].b = i;
    col[i].a =  255;
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;
        a = (image->ts > 0 && image->tr[1] == r && image->tr[3] == g && image->tr[5] == b) ? 0 : 255;

        if (a != 0)
        {
          if (r != g || g != b)
            grayscale = 0;
          else
            gray[r] = 1;
        }
        else
          full_transp = 1;

        count_color(cmap, col, colors, last_key, last_idx, r, g, b, a);
      }
    }
  }

  if (colors <= 256)
  {
    if (grayscale && colors > 128) /* 2 -> 0 */
    {
      image->type = 0;
      image->bpp = 1;
      unsigned char t = 0;
      int ts = 0;

      for (i=0; i<256; i++)
      {
        if (gray[i] == 0)
        {
          t = i;
          ts = 2;
          break;
        }
      }
      for (i=0; i<img.size(); i++)
      {
        for (y=0; y<image->h; y++)
        {
          sp = dp = img[i].rows[y];
          for (x=0; x<image->w; x++, sp+=3)
          {
            *dp++ = (image->ts > 0 && image->tr[1] == sp[0] && image->tr[3] == sp[1] && image->tr[5] == sp[2]) ? t : sp[0];
          }
        }
      }
      /* replace the old RGB tRNS; if no free gray level was found the image
         must not keep the 6-byte RGB tRNS (invalid for grayscale) */
      image->ts = 0;
      if (ts > 0)
      {
        image->tr[0] = 0;
        image->tr[1] = t;
        image->ts = ts;
      }
    }
    else  /* 2 -> 3 */
    {
      image->type = 3;
      image->bpp = 1;

      if (full_transp == 0 && colors < 256)
        col[colors++].a = 0;

      qsort(&col[0], colors, sizeof(COLORS), cmp_colors);
      ColorMap imap;
      build_index_map(imap, col, colors);

      for (i=0; i<img.size(); i++)
      {
        for (y=0; y<image->h; y++)
        {
          sp = dp = img[i].rows[y];
          for (x=0; x<image->w; x++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            a = (image->ts > 0 && image->tr[1] == r && image->tr[3] == g && image->tr[5] == b) ? 0 : 255;
            *dp++ = lookup_index(imap, colors, r, g, b, a);
          }
        }
      }

      image->ps = colors;
      image->ts = 0; /* recomputed below; drop any stale RGB tRNS length */
      for (i=0; i<colors; i++)
      {
        image->pl[i].r = col[i].r;
        image->pl[i].g = col[i].g;
        image->pl[i].b = col[i].b;
        image->tr[i]   = col[i].a;
        if (image->tr[i] != 255) 
          image->ts = i+1;
      }
    }
  }
}

void down4(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char * dp;
  unsigned char g, a;
  int simple_transp = 1;
  int full_transp = 0;
  unsigned char gray[256];
  COLORS col[256];
  unsigned int colors = 0;
  ColorMap cmap;
  unsigned int last_key = 0;
  int last_idx = -1;
  Image * image = &img[0];

  memset(&gray, 0, sizeof(gray));

  for (i=0; i<256; i++)
  {
    col[i].num = 0;
    col[i].r = col[i].g = col[i].b = i;
    col[i].a = image->tr[i] = 255;
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
      {
        g = *sp++;
        a = *sp++;

        if (a != 0)
        {
          if (a != 255)
            simple_transp = 0;
          else
            gray[g] = 1;
        }
        else
          full_transp = 1;

        count_color(cmap, col, colors, last_key, last_idx, g, g, g, a);
      }
    }
  }

  if (simple_transp && colors <= 256) /* 4 -> 0 */
  {
    image->type = 0;
    image->bpp = 1;
    unsigned char t = 0;

    for (i=0; i<256; i++)
    if (gray[i] == 0)
    {
      image->tr[0] = 0;
      image->tr[1] = t = i;
      image->ts = 2;
      break;
    }

    for (i=0; i<img.size(); i++)
    {
      for (y=0; y<image->h; y++)
      {
        sp = dp = img[i].rows[y];
        for (x=0; x<image->w; x++, sp+=2)
        {
          *dp++ = (sp[1] == 0) ? t : sp[0];
        }
      }
    }
  }
  else
  if (colors <= 256)   /* 4 -> 3 */
  {
    image->type = 3;
    image->bpp = 1;

    if (full_transp == 0 && colors < 256)
      col[colors++].a = 0;

    qsort(&col[0], colors, sizeof(COLORS), cmp_colors);
      ColorMap imap;
      build_index_map(imap, col, colors);

    for (i=0; i<img.size(); i++)
    {
      for (y=0; y<image->h; y++)
      {
        sp = dp = img[i].rows[y];
        for (x=0; x<image->w; x++)
        {
          g = *sp++;
          a = *sp++;
          *dp++ = lookup_index(imap, colors, g, g, g, a);
        }
      }
    }

    image->ps = colors;
      image->ts = 0; /* recomputed below; drop any stale RGB tRNS length */
    for (i=0; i<colors; i++)
    {
      image->pl[i].r = col[i].r;
      image->pl[i].g = col[i].g;
      image->pl[i].b = col[i].b;
      image->tr[i]   = col[i].a;
      if (image->tr[i] != 255) 
        image->ts = i+1;
    }
  }
}

void down3(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char * dp;
  int c;
  int simple_transp = 1;
  int grayscale = 1;
  unsigned char gray[256];
  COLORS col[256];
  Image * image = &img[0];

  memset(&gray, 0, sizeof(gray));

  for (c=0; c<256; c++)
  {
    col[c].num = 0;
    if (c < image->ps)
    {
      col[c].r = image->pl[c].r;
      col[c].g = image->pl[c].g;
      col[c].b = image->pl[c].b;
      col[c].a = image->tr[c];
    }
    else
    {
      col[c].r = col[c].g = col[c].b = c;
      col[c].a = 255;
    }
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
        col[*sp++].num++;
    }
  }

  for (i=0; i<256; i++)
  if (col[i].num != 0)
  {
    if (col[i].a != 0)
    {
      if (col[i].a != 255)
        simple_transp = 0;
      else
      if (col[i].r != col[i].g || col[i].g != col[i].b)
        grayscale = 0;
      else
        gray[col[i].g] = 1;
    }
  }

  if (grayscale && simple_transp) /* 3 -> 0 */
  {
    image->type = 0;
    image->bpp = 1;
    unsigned char t = 0;
    int ts = 0;

    for (i=0; i<256; i++)
    {
      if (gray[i] == 0)
      {
        t = i;
        ts = 2;
        break;
      }
    }

    for (i=0; i<img.size(); i++)
    {
      for (y=0; y<image->h; y++)
      {
        dp = img[i].rows[y];
        for (x=0; x<image->w; x++, dp++)
        {
          *dp = (col[*dp].a == 0) ? t : image->pl[*dp].g;
        }
      }
    }
    image->ps = 0;
    image->ts = 0;
    if (ts > 0)
    {
      image->tr[0] = 0;
      image->tr[1] = t;
      image->ts = ts;
    }
  }
}

void optim_downconvert(std::vector<Image>& img)
{
  if (img[0].type == 6)
    down6(img);
  else
  if (img[0].type == 2)
    down2(img);
  else
  if (img[0].type == 4)
    down4(img);
  else
  if (img[0].type == 3)
    down3(img);
}

void optim_palette(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char r, g, b, a;
  int c;
  int full_transp = 0;
  COLORS col[256];
  Image * image = &img[0];

  for (c=0; c<256; c++)
  {
    col[c].num = 0;
    if (c < image->ps)
    {
      col[c].r = image->pl[c].r;
      col[c].g = image->pl[c].g;
      col[c].b = image->pl[c].b;
      col[c].a = image->tr[c];
    }
    else
    {
      col[c].r = col[c].g = col[c].b = c;
      col[c].a = 255;
    }
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
        col[*sp++].num++;
    }
  }

  for (i=0; i<256; i++)
  {
    if (col[i].num != 0 && col[i].a == 0)
    {
      full_transp = 1;
      break;
    }
  }

  for (i=0; i<256; i++)
  {
    if (col[i].num == 0)
    {
      col[i].a = 255;
      if (full_transp == 0)
      {
        col[i].a = 0;
        full_transp = 1;
      }
    }
  }

  qsort(&col[0], 256, sizeof(COLORS), cmp_colors);

  unsigned char remap[256];
  for (c = 0; c < 256; c++)
  {
    r = image->pl[c].r;
    g = image->pl[c].g;
    b = image->pl[c].b;
    a = image->tr[c];
    int k;
    for (k = 0; k < 256; k++)
      if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
        break;
    remap[c] = (unsigned char)k;
  }

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
      {
        *sp = remap[*sp];
        sp++;
      }
    }
  }

  for (i=0; i<256; i++)
  {
    image->pl[i].r = col[i].r;
    image->pl[i].g = col[i].g;
    image->pl[i].b = col[i].b;
    image->tr[i]   = col[i].a;
    if (col[i].num != 0)
      image->ps = i+1;
    if (image->tr[i] != 255) 
      image->ts = i+1;
  }
}

void add_transp2(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char r, g, b;
  unsigned char cube[4096];
  Image * image = &img[0];

  memset(&cube, 0, sizeof(cube));

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;
        if (((r | g | b) & 15) == 0)
          cube[(r<<4) + g + (b>>4)] = 1;
      }
    }
  }

  for (i=0; i<4096; i++)
  if (cube[i] == 0)
  {
    image->tr[0] = 0;
    image->tr[1] = (i>>4)&0xF0;
    image->tr[2] = 0;
    image->tr[3] = i&0xF0;
    image->tr[4] = 0;
    image->tr[5] = (i<<4)&0xF0;
    image->ts = 6;
    break;
  }
}

void add_transp0(std::vector<Image>& img)
{
  unsigned int i, x, y;
  unsigned char * sp;
  unsigned char gray[256];
  Image * image = &img[0];

  memset(&gray, 0, sizeof(gray));

  for (i=0; i<img.size(); i++)
  {
    for (y=0; y<image->h; y++)
    {
      sp = img[i].rows[y];
      for (x=0; x<image->w; x++)
        gray[*sp++] = 1;
    }
  }

  for (i=0; i<256; i++)
  if (gray[i] == 0)
  {
    image->tr[0] = 0;
    image->tr[1] = i;
    image->ts = 2;
    break;
  }
}

void optim_add_transp(std::vector<Image>& img)
{
  if (img[0].ts == 0)
  {
    if (img[0].type == 2)
      add_transp2(img);
    else
    if (img[0].type == 0)
      add_transp0(img);
  }
}
