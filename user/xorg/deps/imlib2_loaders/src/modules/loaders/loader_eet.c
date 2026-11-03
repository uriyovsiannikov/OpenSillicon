#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "loader_common.h"

#include <Eet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>

static int
permissions(char *file)
{
   struct stat         st;

   if (stat(file, &st) < 0)
      return 0;
   return st.st_mode;
}

static int
exists(char *file)
{
   struct stat         st;

   if (stat(file, &st) < 0)
      return 0;
   return 1;
}

static int
can_read(char *file)
{
   if (!(permissions(file) & (S_IRUSR | S_IRGRP | S_IROTH)))
      return 0;
   return (1 + access(file, R_OK));
}

static int
can_write(char *file)
{
   if (!(permissions(file) & (S_IWUSR | S_IWGRP | S_IWOTH)))
      return 0;
   return (1 + access(file, W_OK));
}

char
load(ImlibImage * im, ImlibProgressFunction progress,
     char progress_granularity, char immediate_load)
{
   int                 w, h, alpha, compression, size;
   Eet_File           *ef;
   char                file[4096], key[4096];
   uint32_t           *ret;
   uint32_t           *body;

   if (!im->key)
      return 0;

   strcpy(file, im->real_file);
   strcpy(key, im->key);
   if (!can_read(file))
      return 0;
   ef = eet_open(file, EET_FILE_MODE_READ);
   if (!ef)
      return 0;
   ret = eet_read(ef, key, &size);
   if (!ret)
     {
        eet_close(ef);
        return 0;
     }
   /* header */
   {
      uint32_t            header[8];

      if (size < 32)
        {
           free(ret);
           eet_close(ef);
           return 0;
        }
      memcpy(header, ret, 32);
#ifdef WORDS_BIGENDIAN
      {
         int                 i;

         for (i = 0; i < 8; i++)
            SWAP32(header[i]);
      }
#endif
      if (header[0] != 0xac1dfeed)
        {
           free(ret);
           eet_close(ef);
           return 0;
        }
      w = header[1];
      h = header[2];
      alpha = header[3];
      compression = header[4];
      if ((w > 8192) || (h > 8192))
        {
           free(ret);
           eet_close(ef);
           return 0;
        }
      if ((compression == 0) && (size < ((w * h * 4) + 32)))
        {
           free(ret);
           eet_close(ef);
           return 0;
        }
      im->w = w;
      im->h = h;
      IM_FLAG_UPDATE(im, F_HAS_ALPHA, alpha);
   }
   if (((!im->data) && (im->loader)) || (immediate_load) || (progress))
     {
        uint32_t           *ptr;
        int                 y, pl = 0;
        char                pper = 0;

        body = &(ret[8]);
        /* must set the im->data member before callign progress function */
        if (!compression)
          {
             if (progress)
               {
                  char                per;
                  int                 l;

                  ptr = im->data = malloc(w * h * sizeof(uint32_t));
                  if (!im->data)
                    {
                       free(ret);
                       eet_close(ef);
                       return 0;
                    }
                  for (y = 0; y < h; y++)
                    {
#ifdef WORDS_BIGENDIAN
                       {
                          int                 x;

                          memcpy(ptr, &(body[y * w]), im->w * sizeof(uint32_t));
                          for (x = 0; x < im->w; x++)
                             SWAP32(ptr[x]);
                       }
#else
                       memcpy(ptr, &(body[y * w]), im->w * sizeof(uint32_t));
#endif
                       ptr += im->w;

                       per = (char)((100 * y) / im->h);
                       if (((per - pper) >= progress_granularity) ||
                           (y == (im->h - 1)))
                         {
                            l = y - pl;
                            if (!progress(im, per, 0, (y - l), im->w, l))
                              {
                                 free(ret);
                                 eet_close(ef);
                                 return 2;
                              }
                            pper = per;
                            pl = y;
                         }
                    }
               }
             else
               {
                  ptr = im->data = malloc(w * h * sizeof(uint32_t));
                  if (!im->data)
                    {
                       free(ret);
                       eet_close(ef);
                       return 0;
                    }
#ifdef WORDS_BIGENDIAN
                  {
                     int                 x;

                     memcpy(ptr, body, im->w * im->h * sizeof(uint32_t));
                     for (x = 0; x < (im->w * im->h); x++)
                        SWAP32(ptr[x]);
                  }
#else
                  memcpy(ptr, body, im->w * im->h * sizeof(uint32_t));
#endif
               }
          }
        else
          {
             uLongf              dlen;

             dlen = w * h * sizeof(uint32_t);
             im->data = malloc(w * h * sizeof(uint32_t));
             if (!im->data)
               {
                  free(ret);
                  eet_close(ef);
                  return 0;
               }
             uncompress((Bytef *) im->data, &dlen, (Bytef *) body,
                        (uLongf) (size - 32));
#ifdef WORDS_BIGENDIAN
             {
                int                 x;

                for (x = 0; x < (im->w * im->h); x++)
                   SWAP32(im->data[x]);
             }
#endif
             if (progress)
                progress(im, 100, 0, 0, im->w, im->h);
          }
     }
   free(ret);
   eet_close(ef);
   return 1;
}

char
save(ImlibImage * im, ImlibProgressFunction progress, char progress_granularity)
{
   int                 alpha = 0;
   char                file[4096], key[4096], *tmp;
   uint32_t           *header;
   uint32_t           *buf;
   Eet_File           *ef;
   int                 compression = 0, size = 0;
   uint32_t           *ret;

   /* no image data? abort */
   if (!im->data)
      return 0;
   if (IM_FLAG_ISSET(im, F_HAS_ALPHA))
      alpha = 1;
   if ((!im->file) || (!im->real_file))
      return 0;
   strcpy(file, im->real_file);

   tmp = strrchr(file, ':');
   if (!tmp)
      return 0;
   *tmp++ = '\0';
   if (!*tmp)
      return 0;
   strcpy(key, tmp);

   if (exists(file))
     {
        if (!can_write(file))
           return 0;
        if (!can_read(file))
           return 0;
     }
   ef = eet_open(file, EET_FILE_MODE_WRITE);
   if (!ef)
      return 0;

   /* account for space for compression */
   buf = malloc((((im->w * im->h * 101) / 100) + 3 + 8) * sizeof(uint32_t));
   header = buf;
   header[0] = 0xac1dfeed;
   header[1] = im->w;
   header[2] = im->h;
   header[3] = alpha;
   {
      ImlibImageTag      *tag;

      tag = __imlib_GetTag(im, "compression");
      if (!tag)
         header[4] = 0;
      else
        {
           compression = tag->val;
           if (compression < 0)
              compression = 0;
           else if (compression > 9)
              compression = 9;
           header[4] = compression;
        }
   }
   if (compression > 0)
     {
        uint32_t           *compressed;
        int                 retr;
        uLongf              buflen;

        compressed = &(buf[8]);
        buflen = ((im->w * im->h * sizeof(uint32_t) * 101) / 100) + 12;
#ifdef WORDS_BIGENDIAN
        {
           int                 i;
           uint32_t           *buf2;

           for (i = 0; i < 8; i++)
              SWAP32(header[i]);

           buf2 =
              malloc((((im->w * im->h * 101) / 100) + 3) * sizeof(uint32_t));
           if (buf2)
             {
                int                 y;

                memcpy(buf2, im->data, im->w * im->h * sizeof(uint32_t));
                for (y = 0; y < (im->w * im->h) + 8; y++)
                   SWAP32(buf2[y]);
                retr = compress2((Bytef *) compressed, &buflen,
                                 (Bytef *) buf2,
                                 (uLong) (im->w * im->h * sizeof(uint32_t)),
                                 compression);
                free(buf2);
             }
           else
              retr = Z_MEM_ERROR;
        }
#else
        retr = compress2((Bytef *) compressed, &buflen,
                         (Bytef *) im->data,
                         (uLong) (im->w * im->h * sizeof(uint32_t)),
                         compression);
#endif
        if (retr != Z_OK)
           compressed = 0;
        else
          {
             if (buflen >= (im->w * im->h * sizeof(uint32_t)))
                compressed = 0;
             else
                size = (8 * sizeof(uint32_t)) + buflen;
          }
     }
   else
     {
        memcpy(&(buf[8]), im->data, im->w * im->h * sizeof(uint32_t));
        header[4] = compression;
#ifdef WORDS_BIGENDIAN
        {
           int                 y;

           for (y = 0; y < (im->w * im->h) + 8; y++)
              SWAP32(buf[y]);
        }
#endif
        size = ((im->w * im->h) + 8) * sizeof(uint32_t);
     }
   ret = buf;
   eet_write(ef, key, ret, size, 0);
   free(buf);
   if (progress)
      progress(im, 100, 0, 0, im->w, im->h);
   /* finish off */
   eet_close(ef);
   return 1;
}

void
formats(ImlibLoader * l)
{
   static const char  *const list_formats[] = { "ani" };
   __imlib_LoaderSetFormats(l, list_formats,
                            sizeof(list_formats) / sizeof(char *));
}
