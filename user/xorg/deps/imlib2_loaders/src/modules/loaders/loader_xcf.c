/*
  -----------------------------[ XCF Loader ]-----------------------------

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  ------------------------------------------------------------------------

  There's a quick overview of the XCF file structure in Gimp's source
  tree, in docs/xcf.doc. However it's brief, so here's a more verbose
  overview based on my understanding of XCF.  All image characteristics
  are defined in "properties". In the data stream properties are defined
  through a 4 bit index number (see enum below), followed by a 4 byte
  length. The property data directly follows this information. A list of
  properties ends when PROP_END is encountered. There is a properties
  block at the beginning of the file, as well as at the beginning of each
  layer and channel. Layers and channels are read at offsets in the file,
  the list of layers (resp. channels) is exhausted when the next offset
  read in is zero.

  The actual image data is stored in tiles, which are by default 64x64 in
  size, likely with smaller ones on the right and bottom edges.

  The position of the tiles on the image is left->right, row by row. The
  actual uint8_t* data is contained in a "level" which is contained in a
  "hierarchy". I've not really understood the purpose of the hierarchy, as
  it seems to always contain only one level anyway.
  
  Layer masks are stored as channels (basically grayscale layers with a
  single color definition. For the purpose of this loader I replaced the
  concept of a channel with a layer, since it doesn't really matter.

  Ok, hope this helps with understanding XCF.                 -- cK.
*/
#include <netinet/in.h>

#include "Imlib2_Loader.h"
#include "loader_xcf.h"

#define XCF_DBG 0
#if XCF_DBG
#define D(fmt...) printf("Imlib2 XCF loader: " fmt)
#else
#define D(fmt...)
#endif

static const char  *const _formats[] = { "xcf" };

#define TILE_WIDTH   64
#define TILE_HEIGHT  64

/* ------------ typedefs ------------ */

typedef struct _Layer Layer;
typedef struct _Tile Tile;

/* ------------ enums ------------ */

/* These are all the properties that a layer or channel can have.
   Only some of them are actually used. */
typedef enum {
   PROP_END = 0,
   PROP_COLORMAP = 1,
   PROP_ACTIVE_LAYER = 2,
   PROP_ACTIVE_CHANNEL = 3,
   PROP_SELECTION = 4,
   PROP_FLOATING_SELECTION = 5,
   PROP_OPACITY = 6,
   PROP_MODE = 7,
   PROP_VISIBLE = 8,
   PROP_LINKED = 9,
   PROP_PRESERVE_TRANSPARENCY = 10,
   PROP_APPLY_MASK = 11,
   PROP_EDIT_MASK = 12,
   PROP_SHOW_MASK = 13,
   PROP_SHOW_MASKED = 14,
   PROP_OFFSETS = 15,
   PROP_COLOR = 16,
   PROP_COMPRESSION = 17,
   PROP_GUIDES = 18,
   PROP_RESOLUTION = 19,
   PROP_TATTOO = 20,
   PROP_PARASITES = 21,
   PROP_UNIT = 22,
   PROP_PATHS = 23,
   PROP_USER_UNIT = 24
} PropType;

/* The tiles can be stored in an encrypted fashion, defined as follows: */
typedef enum {
   COMPRESS_NONE = 0,
   COMPRESS_RLE = 1,
   COMPRESS_ZLIB = 2,
   COMPRESS_FRACTAL = 3         /* Unused. */
} CompressionType;

/* Layer modes (*SIGH*) */
typedef enum {
   NORMAL_MODE,
   DISSOLVE_MODE,
   BEHIND_MODE,
   MULTIPLY_MODE,
   SCREEN_MODE,
   OVERLAY_MODE,
   DIFFERENCE_MODE,
   ADDITION_MODE,
   SUBTRACT_MODE,
   DARKEN_ONLY_MODE,
   LIGHTEN_ONLY_MODE,
   HUE_MODE,
   SATURATION_MODE,
   COLOR_MODE,
   VALUE_MODE,
   DIVIDE_MODE,
   ERASE_MODE,                  /*< skip > */
   REPLACE_MODE,                /*< skip > */
   ANTI_ERASE_MODE              /*< skip > */
} LayerModeEffects;

/* Base image types */
typedef enum {
   RGB,
   GRAY,
   INDEXED
} GimpImageBaseType;

/* Image types */
typedef enum {
   RGB_GIMAGE,
   RGBA_GIMAGE,
   GRAY_GIMAGE,
   GRAYA_GIMAGE,
   INDEXED_GIMAGE,
   INDEXEDA_GIMAGE
} GimpImageType;

/* ------------ structs ------------ */

/* Ok, this is what's left of Gimp's layer abstraction. I kicked out
   all the stuff that's unnecessary and added the necessary stuff
   from the Gimp drawable superclass. This one also serves as a
   Channel, e.g. for use as a layer mask. 
                                            --cK.
*/
struct _Layer {
   int                 visible; /* controls visibility            */
   int                 width, height;   /* size of drawable               */
   int                 bpp;     /* depth                          */
   int                 offset_x, offset_y;      /* offset of layer in image       */

   int                 ID;      /* provides a unique ID           */
   GimpImageType       type;    /* type of drawable               */
   char                has_alpha;       /* drawable has alpha             */

   int                 preserve_trans;  /*  preserve transparency         */

   Layer              *mask;    /*  possible layer mask           */

   int                 opacity; /*  layer opacity                 */
   LayerModeEffects    mode;    /*  layer combination mode        */

   /* XCF stores the actual image data as tiles. A Layer is covered with
    * tiles, the tiles on the right and bottom will (usually) be a bit
    * smaller. num_rows and num_cols gives the number of tile rows and
    * columns.
    */

   Tile               *tiles;   /* tiles for drawable data        */
   int                 num_rows;
   int                 num_cols;

   /* After the tiles are read in, they're serialized int an array
    * of uint8_t's, that will always contain 4 bpp data. Not optimal,
    * I know, but makes life easier
    */

   uint32_t           *data;

   /* Layers are stored as a linked list. */
   struct _Layer      *next;
   struct _Layer      *prev;
};

/* The tile structure:
*/
struct _Tile {
   unsigned char       bpp;     /* the bytes per pixel (1, 2, 3 or 4) */
   unsigned short      ewidth;  /* the effective width of the tile */
   unsigned short      eheight; /* the effective height of the tile */

   /* a tile's effective width and height may be smaller
    * (but not larger) than TILE_WIDTH and TILE_HEIGHT.
    * This is to handle edge tiles of a drawable.
    */

   uint8_t            *data;
};

/* This struct simply contains everything that didn't fit elsewhere,
   based on GimpImage :)
*/
struct _GimpImage {
   FILE               *fp;
   int                 cp;      /*  file stream pointer          */
   int                 compression;     /*  file compression mode        */
   int                 file_version;

   int                 width, height;   /*  width and height attributes  */
   GimpImageBaseType   base_type;       /*  base gimp_image type         */

   uint32_t            floating_sel_offset;

   uint8_t            *cmap;    /*  colormap--for indexed        */
   uint32_t            num_cols;        /*  number of colors in map      */

   /* If a layer number was passed to the loader, it goes here: */
   int                 single_layer_index;

   /* Tadaa -- the final image data. Layers get pasted
    * onto this one, bottom-up.
    */
   uint32_t           *data;

   Layer              *layers;
   Layer              *last_layer;
   Layer              *floating_sel;
} _image;

/* ------------ globals ------------ */

/* This makes using the Gimp sources easier */
struct _GimpImage  *image = &_image;

/* ------------ prototypes ------------ */

/* new stuff :) */

static void
free_tiles(Tile * tiles, int num_tiles)
{
   int                 i;

   D("%s: tiles=%p num=%d\n", __func__, tiles, num_tiles);

   for (i = 0; i < num_tiles; i++)
     {
        if (tiles[i].data)
          {
             free(tiles[i].data);
             tiles[i].data = NULL;
          }
     }
   free(tiles);
}

static void
init_tile(Tile * tile, int width, int height, int bpp)
{
   D("%s: tile=%p\n", __func__, tile);

   if (!tile)
      return;

   tile->bpp = bpp;
   tile->ewidth = width;
   tile->eheight = height;
   tile->data = malloc(sizeof(uint8_t) * width * height * bpp);
   if (!tile->data)
     {
        D("Couldn't allocate tile.\n");
     }
}

static Tile        *
allocate_tiles(int width, int height, int bpp, int *num_rows, int *num_cols)
{
   Tile               *tiles;
   int                 i, j, k, right_tile, bottom_tile;
   int                 tile_width, tile_height;

   (*num_rows) = (height + TILE_HEIGHT - 1) / TILE_HEIGHT;
   (*num_cols) = (width + TILE_WIDTH - 1) / TILE_WIDTH;

   tiles = calloc((*num_rows) * (*num_cols), sizeof(Tile));
   if (!tiles)
     {
        D("Couldn't allocate tiles.\n");
        return NULL;
     }

   D("%s: tiles=%p %dx%d\n", __func__, tiles, *num_cols, *num_rows);

   right_tile = width - (((*num_cols) - 1) * TILE_WIDTH);
   bottom_tile = height - (((*num_rows) - 1) * TILE_HEIGHT);

   for (i = 0, k = 0; i < (*num_rows); i++)
     {
        for (j = 0; j < (*num_cols); j++, k++)
          {
             tile_width = ((j == (*num_cols) - 1) ? right_tile : TILE_WIDTH);
             tile_height = ((i == (*num_rows) - 1) ? bottom_tile : TILE_HEIGHT);
             init_tile(&(tiles[k]), tile_width, tile_height, bpp);
          }
     }

   D("Allocated %ix%i tiles.\n", (*num_cols), (*num_rows));

   return tiles;
}

static Layer       *
new_layer(int width, int height, GimpImageType type, int opacity,
          LayerModeEffects mode)
{
   Layer              *layer;

   layer = calloc(1, sizeof(Layer));
   if (!layer)
     {
        D("Couldn't allocate layer.\n");
        return NULL;
     }

   D("%s: layer=%p\n", __func__, layer);

   layer->width = width;
   layer->height = height;
   layer->type = type;
   layer->opacity = opacity;
   layer->mode = mode;

   return layer;
}

static void
free_layer(Layer * layer)
{
   D("%s: layer=%p\n", __func__, layer);

   if (!layer)
      return;

   if (layer->tiles)
     {
        free_tiles(layer->tiles, layer->num_rows * layer->num_cols);
        layer->tiles = NULL;
     }

   if (layer->mask)
     {
        free_layer(layer->mask);
        layer->mask = NULL;
     }

   if (layer->data)
      free(layer->data);

   free(layer);
}

static void
add_layer_to_image(Layer * layer)
{
   if (!layer)
      return;

   if (image->last_layer)
     {
        image->last_layer->next = layer;
        layer->prev = image->last_layer;
     }
   else
     {
        image->layers = layer;
        layer->prev = NULL;
     }
   layer->next = NULL;
   image->last_layer = layer;
}

static void
set_layer_opacity(Layer * layer)
{
   int                 i;
   uint8_t            *ptr;

   if (!layer)
      return;

   if (layer->opacity != 255)
     {
        for (i = 0, ptr = (uint8_t *) layer->data;
             i < layer->width * layer->height; i++, ptr += 4)
          {
             *(ptr + 3) = (*(ptr + 3) * layer->opacity) >> 8;
          }
     }
}

static void
apply_layer_mask(Layer * layer)
{
   uint8_t            *ptr1;
   uint8_t            *ptr2;
   int                 i, tmp;

   D("Applying layer mask.\n");

   if (!layer)
      return;

   if (!layer->mask)
      return;

   ptr1 = (uint8_t *) layer->data;
   ptr2 = (uint8_t *) layer->mask->data;

   for (i = 0; i < layer->width * layer->height; i++)
     {
        tmp = (*(ptr1 + 3) * *(ptr2)) / 256;
        if (tmp > 255)
           tmp = 255;

        *(ptr1 + 3) = (uint8_t) tmp;
        ptr1 += 4;
        ptr2 += 4;
     }
}

static void
flatten_image(void)
{
   Layer              *l = image->last_layer;
   Layer              *lp;
   int                 layer_index;

   image->data = calloc(image->width * image->height, sizeof(uint32_t));
   layer_index = 0;

   while (l)
     {
        /* Ok, paste each layer on top of the image, using the mode's merging type.
         * We're moving upward through the layer stack.
         * --cK.
         */
        if (image->single_layer_index < 0
            || layer_index == image->single_layer_index)
          {
             switch (l->mode)
               {
               case MULTIPLY_MODE:
                  D("MULTIPLY\n");
                  combine_pixels_mult(l->data, l->width, l->height,
                                      image->data, image->width, image->height,
                                      l->offset_x, l->offset_y);
                  break;
               case DIVIDE_MODE:
                  D("DIVIDE\n");
                  combine_pixels_div(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case SCREEN_MODE:
                  D("SCREEN\n");
                  combine_pixels_screen(l->data, l->width, l->height,
                                        image->data, image->width,
                                        image->height, l->offset_x,
                                        l->offset_y);
                  break;
               case OVERLAY_MODE:
                  D("OVERLAY\n");
                  combine_pixels_overlay(l->data, l->width, l->height,
                                         image->data, image->width,
                                         image->height, l->offset_x,
                                         l->offset_y);
                  break;
               case DIFFERENCE_MODE:
                  D("DIFF\n");
                  combine_pixels_diff(l->data, l->width, l->height,
                                      image->data, image->width, image->height,
                                      l->offset_x, l->offset_y);
                  break;
               case ADDITION_MODE:
                  D("ADD\n");
                  combine_pixels_add(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case SUBTRACT_MODE:
                  D("SUB\n");
                  combine_pixels_sub(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case DARKEN_ONLY_MODE:
                  D("DARKEN\n");
                  combine_pixels_darken(l->data, l->width, l->height,
                                        image->data, image->width,
                                        image->height, l->offset_x,
                                        l->offset_y);
                  break;
               case LIGHTEN_ONLY_MODE:
                  D("LIGHTEN\n");
                  combine_pixels_lighten(l->data, l->width, l->height,
                                         image->data, image->width,
                                         image->height, l->offset_x,
                                         l->offset_y);
                  break;

               case HUE_MODE:
                  D("HUE\n");
                  combine_pixels_hue(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case SATURATION_MODE:
                  D("SATURATION\n");
                  combine_pixels_sat(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case VALUE_MODE:
                  D("VALUE\n");
                  combine_pixels_val(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case COLOR_MODE:
                  D("COLOR\n");
                  combine_pixels_col(l->data, l->width, l->height,
                                     image->data, image->width, image->height,
                                     l->offset_x, l->offset_y);
                  break;
               case DISSOLVE_MODE:
                  D("DISSOLVE\n");
                  combine_pixels_diss(l->data, l->width, l->height,
                                      image->data, image->width, image->height,
                                      l->offset_x, l->offset_y);
                  break;

                  /* None of the following is actually valid for layer blending, fall through: */
               case BEHIND_MODE:
               case REPLACE_MODE:
               case ERASE_MODE:
               case ANTI_ERASE_MODE:
                  D("EEEEEK -- this mode shouldn't be here\n");
                  /* FALLTHROUGH */

               case NORMAL_MODE:
                  D("NORMAL\n");
                  combine_pixels_normal(l->data, l->width, l->height,
                                        image->data, image->width,
                                        image->height, l->offset_x,
                                        l->offset_y);
                  break;

               default:
                  D("Unknown layer mode: %i. Skipping.\n", l->mode);
                  break;
               }
          }

        lp = l->prev;
        /* free the layer now, since it's not needed anymore */
        free_layer(l);

        l = lp;
        layer_index++;
     }

   /* We've used up all the layers now, so set them to NULL in the image: */
   image->layers = NULL;
   image->last_layer = NULL;
}

/* ------------ code ------------ */

/* stuff that was adapted from xcf.c */

static void
xcf_seek_pos(int pos)
{
   if (image->cp != pos)
     {
        image->cp = pos;
        fseek(image->fp, image->cp, SEEK_SET);
     }
}

static int
xcf_read_int8(FILE * fp, uint8_t * data, int count)
{
   return fread(data, 1, count, fp);
}

static int
xcf_read_int32(FILE * fp, uint32_t * data, int count)
{
   int                 i, nr;

   nr = fread(data, 4, count, fp);
   if (nr != count)
      return 0;
   for (i = 0; i < count; i++)
     {
        *data = (uint32_t) ntohl(*data);
        data++;
     }

   return 4 * nr;
}

static int
xcf_read_string(FILE * fp, char **data, int count)
{
   uint32_t            tmp;
   int                 total;
   int                 i;

   total = 0;
   for (i = 0; i < count; i++)
     {
        total += xcf_read_int32(fp, &tmp, 1);
        if (tmp > 0)
          {
             data[i] = malloc(sizeof(uint8_t) * tmp);
             total += xcf_read_int8(fp, (uint8_t *) data[i], tmp);
          }
        else
          {
             data[i] = NULL;
          }
     }

   return total;
}

static int
xcf_load_prop(PropType * prop_type, uint32_t * prop_size)
{
   int                 nr;

   nr = xcf_read_int32(image->fp, (uint32_t *) prop_type, 1);
   if (nr != 4)
      return 0;
   nr = xcf_read_int32(image->fp, (uint32_t *) prop_size, 1);
   if (nr != 4)
      return 0;

   image->cp += 8;

   return 1;
}

static int
xcf_load_image_props(void)
{
   PropType            prop_type;
   uint32_t            prop_size;
   uint8_t             compression;

   while (1)
     {
        if (!xcf_load_prop(&prop_type, &prop_size))
           return 0;

        switch (prop_type)
          {
          case PROP_END:
             D("Finished reading image properties.\n");
             return 1;

          case PROP_COLORMAP:
             if (image->file_version == 0)
               {
                  unsigned int        i;

                  fprintf(stderr,
                          "XCF warning: version 0 of XCF file format\n"
                          "did not save indexed colormaps correctly.\n"
                          "Substituting grayscale map.\n");
                  image->cp += xcf_read_int32(image->fp, &image->num_cols, 1);
                  image->cmap = malloc(sizeof(uint8_t) * image->num_cols * 3);
                  xcf_seek_pos(image->cp + image->num_cols);
                  for (i = 0; i < image->num_cols; i++)
                    {
                       image->cmap[i * 3 + 0] = i;
                       image->cmap[i * 3 + 1] = i;
                       image->cmap[i * 3 + 2] = i;
                    }
               }
             else
               {
                  D("Loading colormap.\n");
                  image->cp += xcf_read_int32(image->fp, &image->num_cols, 1);
                  image->cmap = malloc(sizeof(uint8_t) * image->num_cols * 3);
                  image->cp +=
                     xcf_read_int8(image->fp, (uint8_t *) image->cmap,
                                   image->num_cols * 3);
               }
             break;

          case PROP_COMPRESSION:
             image->cp += xcf_read_int8(image->fp, &compression, 1);

             if ((compression != COMPRESS_NONE) &&
                 (compression != COMPRESS_RLE) &&
                 (compression != COMPRESS_ZLIB) &&
                 (compression != COMPRESS_FRACTAL))
               {
                  fprintf(stderr, "unknown xcf compression type: %d\n",
                          (int)compression);
                  return 0;
               }

             D("Image compression type: %i\n", compression);

             image->compression = compression;
             break;

             /* I threw out all of the following: --cK */
          case PROP_TATTOO:
          case PROP_PARASITES:
          case PROP_UNIT:
          case PROP_PATHS:
          case PROP_USER_UNIT:
          case PROP_GUIDES:
          case PROP_RESOLUTION:
          default:
             D("Skipping %s property: %d size %d\n", "image",
               prop_type, prop_size);

             while (prop_size > 0)
               {
                  uint8_t             buf[16];
                  int                 amount;

                  amount = (16 < prop_size ? 16 : prop_size);
                  image->cp += xcf_read_int8(image->fp, buf, amount);
                  prop_size -= (16 < amount ? 16 : amount);
               }
             break;
          }
     }

   return 0;
}

static int
xcf_load_layer_props(Layer * layer)
{
   PropType            prop_type;
   uint32_t            prop_size;

   while (1)
     {
        if (!xcf_load_prop(&prop_type, &prop_size))
           return 0;

        switch (prop_type)
          {
          case PROP_END:
             D("Finished reading layer properties.\n");
             return 1;
          case PROP_FLOATING_SELECTION:
             D("Loading floating selection.\n");
             image->floating_sel = layer;
             image->cp +=
                xcf_read_int32(image->fp,
                               (uint32_t *) & image->floating_sel_offset, 1);
             break;
          case PROP_OPACITY:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->opacity, 1);
             break;
          case PROP_VISIBLE:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->visible, 1);
             break;
          case PROP_PRESERVE_TRANSPARENCY:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->preserve_trans,
                               1);
             break;
          case PROP_OFFSETS:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->offset_x, 1);
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->offset_y, 1);
             break;
          case PROP_MODE:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->mode, 1);
             break;

             /* I threw out all of the following: --cK */
          case PROP_LINKED:
          case PROP_ACTIVE_LAYER:
          case PROP_TATTOO:
          case PROP_APPLY_MASK:
          case PROP_EDIT_MASK:
          case PROP_SHOW_MASK:
          case PROP_PARASITES:
          default:
             D("Skipping %s property: %d size %d\n", "layer",
               prop_type, prop_size);

             while (prop_size > 0)
               {
                  uint8_t             buf[16];
                  int                 amount;

                  amount = (16 < prop_size ? 16 : prop_size);
                  image->cp += xcf_read_int8(image->fp, buf, amount);
                  prop_size -= (16 < amount ? 16 : amount);
               }
             break;
          }
     }

   return 0;
}

static int
read_tiles_into_data(Tile * tiles, int num_cols, int width,
                     int height, int bpp, uint32_t ** data_p, int use_cmap)
{
   int                 tile_x, tile_y, x, y, offset_x, offset_y;
   uint32_t           *data;
   uint8_t            *ptr2;
   unsigned char       a, r, g, b;
   Tile               *t;
   int                 warned = 0;

   if (!tiles)
      return 0;

   if (*data_p)
      free(*data_p);

   /* Always allocate the data as 4 bytes per pixel */
   data = malloc(sizeof(uint32_t) * width * height);
   *data_p = data;

   a = r = g = b = 0xff;

   for (y = 0; y < height; y++)
     {
        for (x = 0; x < width; x++)
          {
             tile_x = x / TILE_WIDTH;
             tile_y = y / TILE_HEIGHT;
             offset_x = x % TILE_WIDTH;
             offset_y = y % TILE_HEIGHT;

             t = &tiles[tile_y * num_cols + tile_x];
             ptr2 = &(t->data[offset_y * t->ewidth * bpp + offset_x * bpp]);

             switch (bpp)
               {
               case 1:
                  /* use colormap if the image has one */
                  if (image->cmap && use_cmap)
                    {
                       r = image->cmap[*(ptr2) * 3];
                       g = image->cmap[*(ptr2) * 3 + 1];
                       b = image->cmap[*(ptr2) * 3 + 2];
                       a = 255;
                    }
                  /* else use colors themselves */
                  else
                    {
                       r = *(ptr2);
                       g = *(ptr2);
                       b = *(ptr2);
                       a = 255;
                    }
                  break;
               case 2:
                  /* use colormap if the image has one */
                  if (image->cmap && use_cmap)
                    {
                       r = image->cmap[*(ptr2) * 3];
                       g = image->cmap[*(ptr2) * 3 + 1];
                       b = image->cmap[*(ptr2) * 3 + 2];
                       a = *(ptr2 + 1);
                    }
                  /* else use colors themselves */
                  else if (warned == 0)
                    {
                       warned++;
                       fprintf(stderr,
                               "There's nothing to see here. 2 bpp without colormap not implemented yet.\n");
                    }
                  break;
               case 3:
                  if (image->cmap)
                    {
                       if (warned == 0)
                         {
                            warned++;
                            fprintf(stderr,
                                    "There's nothing to see here. 3 bpp with colormap not implemented yet.\n");
                         }
                    }
                  else
                    {
                       r = *(ptr2);
                       g = *(ptr2 + 1);
                       b = *(ptr2 + 2);
                       a = 255;
                    }
                  break;
               default:
                  r = *(ptr2);
                  g = *(ptr2 + 1);
                  b = *(ptr2 + 2);
                  a = *(ptr2 + 3);
                  break;
               }

             *data++ = PIXEL_ARGB(a, r, g, b);
          }
     }

   return warned;
}

static int
xcf_load_tile(Tile * tile)
{
   image->cp +=
      xcf_read_int8(image->fp, tile->data,
                    tile->ewidth * tile->eheight * tile->bpp);
   return 1;
}

static int
xcf_load_tile_rle(Tile * tile, int data_length)
{
   uint8_t            *data;
   uint8_t             val;
   int                 size;
   int                 length;
   int                 bpp;
   int                 i, j;
   int                 nmemb_read_successfully;
   uint8_t            *xcfdata, *xcfodata, *xcfdatalimit;

   data = tile->data;
   bpp = tile->bpp;

   /*printf ("Reading encrypted tile %ix%ix%i, data_length %i\n", tile->ewidth, tile->eheight, tile->bpp, data_length); */

   xcfdata = xcfodata = malloc(sizeof(uint8_t) * data_length);

   /* we have to use fread instead of xcf_read_* because we may be
    * reading past the end of the file here */
   nmemb_read_successfully = fread(xcfdata, 1, data_length, image->fp);
   image->cp += nmemb_read_successfully;

   xcfdatalimit = &xcfodata[nmemb_read_successfully - 1];

   for (i = 0; i < bpp; i++)
     {
        data = (tile->data) + i;
        size = tile->ewidth * tile->eheight;

        while (size > 0)
          {
             if (xcfdata > xcfdatalimit)
               {
                  goto bogus_rle;
               }

             val = *xcfdata++;

             length = val;
             if (length >= 128)
               {
                  length = 255 - (length - 1);
                  if (length == 128)
                    {
                       if (xcfdata >= xcfdatalimit)
                         {
                            goto bogus_rle;
                         }

                       length = (*xcfdata << 8) + xcfdata[1];
                       xcfdata += 2;
                    }

                  size -= length;

                  if (size < 0)
                    {
                       goto bogus_rle;
                    }

                  if (&xcfdata[length - 1] > xcfdatalimit)
                    {
                       goto bogus_rle;
                    }

                  while (length-- > 0)
                    {
                       *data = *xcfdata++;
                       data += bpp;
                    }
               }
             else
               {
                  length += 1;
                  if (length == 128)
                    {
                       if (xcfdata >= xcfdatalimit)
                         {
                            goto bogus_rle;
                         }

                       length = (*xcfdata << 8) + xcfdata[1];
                       xcfdata += 2;
                    }

                  size -= length;

                  if (size < 0)
                    {
                       goto bogus_rle;
                    }

                  if (xcfdata > xcfdatalimit)
                    {
                       goto bogus_rle;
                    }

                  val = *xcfdata++;

                  for (j = 0; j < length; j++)
                    {
                       *data = val;
                       data += bpp;
                    }
               }
          }
     }
   free(xcfodata);
   return 1;

 bogus_rle:
   fprintf(stderr,
           "WHOOOOOP -- bogus rle? Highly unlikely, blame cK for this one :) \n");
   free(xcfodata);
   return 0;
}

static int
xcf_load_level(Tile ** tiles_p, int hierarchy_width, int hierarchy_height,
               int bpp, int *num_rows, int *num_cols)
{
   uint32_t            saved_pos;
   uint32_t            offset, offset2;
   int                 ntiles;
   int                 width;
   int                 height;
   int                 i;
   int                 fail;

   Tile               *tiles;
   Tile               *current_tile;

   image->cp += xcf_read_int32(image->fp, (uint32_t *) & width, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & height, 1);

   if ((width != hierarchy_width) || (height != hierarchy_height))
      return 0;

   D("Loading level of size %ix%i.\n", width, height);
   (*tiles_p) = allocate_tiles(width, height, bpp, num_rows, num_cols);
   tiles = (*tiles_p);

   image->cp += xcf_read_int32(image->fp, &offset, 1);
   if (offset == 0)
      return 1;

   ntiles = (*num_rows) * (*num_cols);
   for (i = 0; i < ntiles; i++)
     {
        current_tile = &(tiles[i]);
        fail = 0;

        if (offset == 0)
          {
             D("Not enough tiles found in level\n");
             return 0;
          }

        /* save the current position as it is where the
         *  next tile offset is stored.
         */
        saved_pos = image->cp;

        /* read in the offset of the next tile so we can calculate the amount
         * of data needed for this tile */
        image->cp += xcf_read_int32(image->fp, &offset2, 1);

        /* if the offset is 0 then we need to read in the maximum possible
         * allowing for negative compression */
        if (offset2 == 0)
           offset2 = offset + TILE_WIDTH * TILE_WIDTH * 4 * 1.5;        /* 1.5 is probably more
                                                                         * than we need to allow */

        /* seek to the tile offset */
        xcf_seek_pos(offset);

        /* read in the current_tile */
        switch (image->compression)
          {
          case COMPRESS_NONE:
             if (!xcf_load_tile(current_tile))
                fail = 1;
             break;
          case COMPRESS_RLE:
             if (!xcf_load_tile_rle(current_tile, offset2 - offset))
                fail = 1;
             break;
          case COMPRESS_ZLIB:
             fprintf(stderr, "xcf: zlib compression unimplemented\n");
             fail = 1;
             break;
          case COMPRESS_FRACTAL:
             fprintf(stderr, "xcf: fractal compression unimplemented\n");
             fail = 1;
             break;
          }

        if (fail)
          {
             D("Couldn't load tiles.\n");
             return 0;
          }

        /* restore the saved position so we'll be ready to
         *  read the next offset.
         */
        xcf_seek_pos(saved_pos);

        /* read in the offset of the next tile */
        image->cp += xcf_read_int32(image->fp, &offset, 1);
     }

   if (offset != 0)
     {
        D("encountered garbage after reading level: %d\n", offset);
        return 0;
     }

   D("Loaded level successfully.\n");

   return 1;
}

static int
xcf_load_hierarchy(Tile ** tiles, int *num_rows, int *num_cols, int *bpp)
{
   uint32_t            saved_pos;
   uint32_t            offset;
   uint32_t            junk;
   int                 width;
   int                 height;

   image->cp += xcf_read_int32(image->fp, (uint32_t *) & width, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & height, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) bpp, 1);

   image->cp += xcf_read_int32(image->fp, &offset, 1);  /* top level */

   D("Loading hierarchy: width %i, height %i,  bpp %i\n", width, height, *bpp);

   /* discard offsets for layers below first, if any.
    */
   do
     {
        image->cp += xcf_read_int32(image->fp, &junk, 1);
     }
   while (junk != 0);

   /* save the current position as it is where the
    *  next level offset is stored.
    */
   saved_pos = image->cp;

   /* seek to the level offset */
   xcf_seek_pos(offset);

   /* read in the level */
   if (!xcf_load_level(tiles, width, height, *bpp, num_rows, num_cols))
      return 0;

   /* restore the saved position so we'll be ready to
    *  read the next offset.
    */
   xcf_seek_pos(saved_pos);

   D("Loaded hierarchy successfully.\n");

   return 1;
}

static int
xcf_load_channel_props(Layer * layer)
{
   PropType            prop_type;
   uint32_t            prop_size;

   while (1)
     {
        if (!xcf_load_prop(&prop_type, &prop_size))
           return 0;

        switch (prop_type)
          {
          case PROP_END:
             D("Finished loading channel props.\n");
             return 1;
          case PROP_OPACITY:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->opacity, 1);
             break;
          case PROP_VISIBLE:
             image->cp +=
                xcf_read_int32(image->fp, (uint32_t *) & layer->visible, 1);
             break;
          case PROP_ACTIVE_CHANNEL:
          case PROP_SHOW_MASKED:
          case PROP_SELECTION:
          case PROP_COLOR:
          case PROP_TATTOO:
          case PROP_PARASITES:
          default:
             D("Skipping %s property: %d size %d\n", "channel",
               prop_type, prop_size);

             while (prop_size > 0)
               {
                  uint8_t             buf[16];
                  int                 amount;

                  amount = (16 < prop_size ? 16 : prop_size);
                  image->cp += xcf_read_int8(image->fp, buf, amount);
                  prop_size -= (16 < amount ? 16 : amount);
               }
             break;
          }
     }

   return 0;
}

static Layer       *
xcf_load_channel(void)
{
   Layer              *layer;
   uint32_t            hierarchy_offset;
   int                 err, width, height;
   char               *name;

   D("Loading channel ...\n");

   /* read in the layer width, height and name */
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & width, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & height, 1);
   image->cp += xcf_read_string(image->fp, &name, 1);

   /* Yeah, still ugly :) */
   free(name);

   /* create a new channel */
   layer = new_layer(width, height, RGBA_GIMAGE, 255, NORMAL_MODE);
   if (!layer)
      return NULL;

   /* read in the channel properties */
   if (!xcf_load_channel_props(layer))
      goto error;

   /* read the hierarchy and layer mask offsets */
   image->cp += xcf_read_int32(image->fp, &hierarchy_offset, 1);

   /* read in the hierarchy */
   xcf_seek_pos(hierarchy_offset);
   if (!xcf_load_hierarchy
       (&(layer->tiles), &(layer->num_rows), &(layer->num_cols), &(layer->bpp)))
      goto error;

   err = read_tiles_into_data(layer->tiles, layer->num_cols, layer->width,
                              layer->height, layer->bpp, &(layer->data), 0);
   free_tiles(layer->tiles, layer->num_rows * layer->num_cols);
   layer->tiles = NULL;
   if (err)
      goto error;

   D("Channel loaded successfully.\n");

   return layer;

 error:
   free_layer(layer);
   return NULL;
}

static Layer       *
xcf_load_layer(void)
{
   Layer              *layer;
   Layer              *layer_mask;
   uint32_t            hierarchy_offset;
   uint32_t            layer_mask_offset;
   int                 err, width, height, type;
   char               *name;

   D("Loading one layer ...\n");

   /* read in the layer width, height and type */
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & width, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & height, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & type, 1);
   image->cp += xcf_read_string(image->fp, &name, 1);

   /* ugly, I know */
   free(name);

   /* create a new layer */
   layer = new_layer(width, height, type, 255, NORMAL_MODE);
   if (!layer)
      return NULL;

   /* read in the layer properties */
   if (!xcf_load_layer_props(layer))
      goto error;

   D("Loading opacity: %i \n", layer->opacity);

   if (!layer->visible)
      return layer;

   /* read the hierarchy and layer mask offsets */
   image->cp += xcf_read_int32(image->fp, &hierarchy_offset, 1);
   image->cp += xcf_read_int32(image->fp, &layer_mask_offset, 1);

   /* read in the hierarchy */
   xcf_seek_pos(hierarchy_offset);
   if (!xcf_load_hierarchy
       (&(layer->tiles), &(layer->num_rows), &(layer->num_cols), &(layer->bpp)))
      goto error;

   /* read in the layer mask */
   if (layer_mask_offset != 0)
     {
        D("Loading layer mask.\n");
        xcf_seek_pos(layer_mask_offset);

        layer_mask = xcf_load_channel();
        if (!layer_mask)
           goto error;

        /* set the offsets of the layer_mask */
        layer_mask->offset_x = layer->offset_x;
        layer_mask->offset_y = layer->offset_y;
        layer->mask = layer_mask;
     }

   err = read_tiles_into_data(layer->tiles, layer->num_cols,
                              layer->width, layer->height,
                              layer->bpp, &(layer->data), 1);
   free_tiles(layer->tiles, layer->num_rows * layer->num_cols);
   layer->tiles = NULL;
   if (err)
      goto error;

   set_layer_opacity(layer);

   if (layer->mask)
      apply_layer_mask(layer);

   return layer;

 error:
   free_layer(layer);
   return NULL;
}

static int
xcf_load_image(void)
{
   Layer              *layer;
   uint32_t            saved_pos;
   uint32_t            offset;
   int                 width;
   int                 height;
   int                 image_type;
   int                 num_successful_elements = 0;

   /* read in the image width, height and type */
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & width, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & height, 1);
   image->cp += xcf_read_int32(image->fp, (uint32_t *) & image_type, 1);

   image->width = width;
   image->height = height;
   image->base_type = image_type;

   D("Loading %ix%i image.\n", width, height);

   /* read the image properties */
   if (!xcf_load_image_props())
      goto hard_error;

   while (1)
     {
        /* read in the offset of the next layer */
        image->cp += xcf_read_int32(image->fp, &offset, 1);

        /* if the offset is 0 then we are at the end
         *  of the layer list.
         */
        if (offset == 0)
           break;

        /* save the current position as it is where the
         *  next layer offset is stored.
         */
        saved_pos = image->cp;

        /* seek to the layer offset */
        xcf_seek_pos(offset);

        /* read in the layer */
        layer = xcf_load_layer();
        if (!layer)
           goto error;

        num_successful_elements++;

        /* add the layer to the image if it's visible */
        if (layer->visible)
           add_layer_to_image(layer);
        else
           free_layer(layer);

        /* restore the saved position so we'll be ready to
         *  read the next offset.
         */
        xcf_seek_pos(saved_pos);

     }

   /* If we were a Gimp we would now load the user-defined channels here ... */

   /* Flat-o-rama now :) */
   flatten_image();

   return 1;                    /* Success */

 error:
   if (num_successful_elements == 0)
      goto hard_error;

   fprintf(stderr,
           "XCF: This file is corrupt!  I have loaded as much of it as I can, but it is incomplete.\n");
   return 0;

 hard_error:
   fprintf(stderr,
           "XCF: This file is corrupt!  I could not even salvage any partial image data from it.\n");
   return 0;
}

static int
xcf_file_init(FILE * fp)
{
   char                success = 1;
   char                id[14];

   image->single_layer_index = -1;
#if 0
   if ((suffix = strchr(filename, ':')))
     {
        *suffix = '\0';
        image->single_layer_index = atoi(suffix + 1);
        D("Loading only XCF layer %i.\n", image->single_layer_index);
     }
#endif
   image->fp = fp;

   image->layers = NULL;
   image->last_layer = NULL;
   image->cmap = NULL;
   image->num_cols = 0;
   image->data = NULL;

   image->cp = 0;

   memset(id, 0, sizeof(id));
   image->cp += xcf_read_int8(image->fp, (uint8_t *) id, 14);
   if (strncmp(id, "gimp xcf ", 9) != 0)
     {
        success = 0;
     }
   else if (strcmp(id + 9, "file") == 0)
     {
        image->file_version = 0;
     }
   else if (id[9] == 'v')
     {
        image->file_version = atoi(id + 10);
     }
   else
     {
        success = 0;
     }

   return success;
}

static void
xcf_cleanup(void)
{
   Layer              *l;
   Layer              *lp;

   l = image->last_layer;
   while (l)
     {
        lp = l->prev;
        free_layer(l);
        l = lp;
     }

   if (image->cmap)
     {
        free(image->cmap);
        image->cmap = NULL;
     }
}

static void
xcf_to_imlib(ImlibImage * im)
{
   im->w = image->width;
   im->h = image->height;
   im->has_alpha = 1;

   im->data = image->data;
}

static int
_load(ImlibImage * im, int load_data)
{
   int                 rc = LOAD_FAIL;

   if (!im->fi->fp)
      return LOAD_FAIL;

   /* initialize */
   if (!xcf_file_init(im->fi->fp))
      return LOAD_FAIL;

   /* do it! */
   if (!xcf_load_image())
      goto quit;

   /* Now paste stuff into Imlib image */
   xcf_to_imlib(im);

   /* I want to ignore this for now, am I doing this right? --cK */
   if (im->lc)
      __imlib_LoadProgressRows(im, 0, im->h);

   rc = LOAD_SUCCESS;

 quit:
   /* cleanup */
   xcf_cleanup();

   return rc;
}

IMLIB_LOADER(_formats, _load, NULL);
