#include <stdio.h>
#include <stdlib.h>
#include <libimagequant.h>
#include <lcdfgif/gif.h>
#include <gifsicle.h>
#include <emscripten.h>

// #include <time.h>
// clock_t c;
// #define TIMESTAMP(name)                                                       \
//   printf("  %s %f\n", name, ((double)(clock() - c) / CLOCKS_PER_SEC * 1000)); \
//   c = clock();

Gt_OutputData active_output_data;
int mode = 0;
int nested_mode = 0;
Clp_Parser *clp = 0;
Gif_CompressInfo gif_write_info = {.flags = 0, .loss = 20};

typedef struct Encoder
{
  int width;
  int height;
  Gif_Stream *stream;
} Encoder;

inline Gif_Colormap *create_colormap_from_palette(const liq_palette *palette)
{
  Gif_Colormap *colormap = Gif_NewFullColormap(palette->count, palette->count);

  for (int i = 0; i < palette->count; i++)
  {
    liq_color color = palette->entries[i];
    colormap->col[i].pixel = 256;
    colormap->col[i].gfc_red = color.r;
    colormap->col[i].gfc_green = color.g;
    colormap->col[i].gfc_blue = color.b;
    colormap->col[i].haspixel = 1;
  }

  return colormap;
}

EMSCRIPTEN_KEEPALIVE
void quantize_image(int width, int height, void *rgba, void (*cb)(void *, int, void *))
{
  liq_attr *attr = liq_attr_create();
  liq_image *raw_image = liq_image_create_rgba(attr, rgba, width, height, 0);
  liq_result *res = liq_quantize_image(attr, raw_image);
  liq_attr_destroy(attr);

  const liq_palette *palette = liq_get_palette(res);
  size_t size = width * height;
  unsigned char *img = malloc(size);
  liq_write_remapped_image(res, raw_image, img, size);

  cb((void *)palette, sizeof(liq_palette), img);

  liq_result_destroy(res);
  liq_image_destroy(raw_image);
}

EMSCRIPTEN_KEEPALIVE
Encoder *encoder_new(int width, int height)
{
  Gif_Stream *stream = Gif_NewStream();
  stream->screen_width = width;
  stream->screen_height = height;
  stream->loopcount = 0;

  Encoder *result = malloc(sizeof(Encoder));
  result->width = width;
  result->height = height;
  result->stream = stream;

  return result;
}

EMSCRIPTEN_KEEPALIVE
void encoder_add_frame(Encoder *encoder, void *data, int delay)
{
  liq_palette *palette = data;
  unsigned char *img = data + sizeof(liq_palette);

  Gif_Image *image = Gif_NewImage();
  image->width = encoder->width;
  image->height = encoder->height;
  image->delay = delay;
  image->local = create_colormap_from_palette(palette);
  Gif_SetUncompressedImage(image, img, 0, 0);

  Gif_AddImage(encoder->stream, image);
}

EMSCRIPTEN_KEEPALIVE
void encoder_finish(Encoder *encoder, void (*cb)(void *, int))
{
  optimize_fragments(encoder->stream, 1, 0);

  Gif_Writer *writer = Gif_NewMemoryWriter(&gif_write_info);
  Gif_WriteGif(writer, encoder->stream);

  cb(writer->v, writer->pos);

  Gif_DeleteMemoryWriter(writer);
  Gif_DeleteStream(encoder->stream);
  free(encoder);
}