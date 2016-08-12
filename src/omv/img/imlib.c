/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Image library.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <arm_math.h>
#include <mp.h>
#include "font.h"
#include "array.h"
#include "ff_wrapper.h"
#include "fb_alloc.h"
#include "xalloc.h"
#include "imlib.h"
#include "mdefs.h"

// Gamma uncompress
extern const float xyz_table[256];

// RGB565 to YUV conversion
extern const int8_t yuv_table[196608];

// USE THE LUT FOR RGB->LAB CONVERSION - NOT THIS FUNCTION!
void imlib_rgb_to_lab(simple_color_t *rgb, simple_color_t *lab)
{
    // https://en.wikipedia.org/wiki/SRGB -> Specification of the transformation
    // https://en.wikipedia.org/wiki/Lab_color_space -> CIELAB-CIEXYZ conversions

    float r_lin = xyz_table[rgb->red];
    float g_lin = xyz_table[rgb->green];
    float b_lin = xyz_table[rgb->blue];

    float x = ((r_lin * 0.4124f) + (g_lin * 0.3576f) + (b_lin * 0.1805f)) / 095.047f;
    float y = ((r_lin * 0.2126f) + (g_lin * 0.7152f) + (b_lin * 0.0722f)) / 100.000f;
    float z = ((r_lin * 0.0193f) + (g_lin * 0.1192f) + (b_lin * 0.9505f)) / 108.883f;

    x = (x>0.008856f) ? fast_cbrtf(x) : ((x * 7.787037f) + 0.137931f);
    y = (y>0.008856f) ? fast_cbrtf(y) : ((y * 7.787037f) + 0.137931f);
    z = (z>0.008856f) ? fast_cbrtf(z) : ((z * 7.787037f) + 0.137931f);

    lab->L = ((int8_t) fast_roundf(116 * y)) - 16;
    lab->A = ((int8_t) fast_roundf(500 * (x-y)));
    lab->B = ((int8_t) fast_roundf(200 * (y-z)));
}

void imlib_lab_to_rgb(simple_color_t *lab, simple_color_t *rgb)
{
    // https://en.wikipedia.org/wiki/Lab_color_space -> CIELAB-CIEXYZ conversions
    // https://en.wikipedia.org/wiki/SRGB -> Specification of the transformation

    float x = ((lab->L + 16) * 0.008621f) + (lab->A * 0.002f);
    float y = ((lab->L + 16) * 0.008621f);
    float z = ((lab->L + 16) * 0.008621f) - (lab->B * 0.005f);

    x = ((x>0.206897f) ? (x*x*x) : ((0.128419f * x) - 0.017713f)) * 095.047f;
    y = ((y>0.206897f) ? (y*y*y) : ((0.128419f * y) - 0.017713f)) * 100.000f;
    z = ((z>0.206897f) ? (z*z*z) : ((0.128419f * z) - 0.017713f)) * 108.883f;

    float r_lin = ((x * +3.2406f) + (y * -1.5372f) + (z * -0.4986f)) / 100.0f;
    float g_lin = ((x * -0.9689f) + (y * +1.8758f) + (z * +0.0415f)) / 100.0f;
    float b_lin = ((x * +0.0557f) + (y * -0.2040f) + (z * +1.0570f)) / 100.0f;

    r_lin = (r_lin>0.0031308f) ? ((1.055f*powf(r_lin, 0.416666f))-0.055f) : (r_lin*12.92f);
    g_lin = (g_lin>0.0031308f) ? ((1.055f*powf(g_lin, 0.416666f))-0.055f) : (g_lin*12.92f);
    b_lin = (b_lin>0.0031308f) ? ((1.055f*powf(b_lin, 0.416666f))-0.055f) : (b_lin*12.92f);

    rgb->red   = IM_MAX(IM_MIN(fast_roundf(r_lin * 255), 255), 0);
    rgb->green = IM_MAX(IM_MIN(fast_roundf(g_lin * 255), 255), 0);
    rgb->blue  = IM_MAX(IM_MIN(fast_roundf(b_lin * 255), 255), 0);
}

void imlib_rgb_to_grayscale(simple_color_t *rgb, simple_color_t *grayscale)
{
    float r_lin = xyz_table[rgb->red];
    float g_lin = xyz_table[rgb->green];
    float b_lin = xyz_table[rgb->blue];
    float y = ((r_lin * 0.2126f) + (g_lin * 0.7152f) + (b_lin * 0.0722f)) / 100.0f;
    y = (y>0.0031308f) ? ((1.055f*powf(y, 0.416666f))-0.055f) : (y*12.92f);
    grayscale->G = IM_MAX(IM_MIN(fast_roundf(y * 255), 255), 0);
}

// Just copy settings back.
void imlib_grayscale_to_rgb(simple_color_t *grayscale, simple_color_t *rgb)
{
    rgb->red   = grayscale->G;
    rgb->green = grayscale->G;
    rgb->blue  = grayscale->G;
}

ALWAYS_INLINE uint16_t imlib_yuv_to_rgb(uint8_t y, int8_t u, int8_t v)
{
    uint32_t r = IM_MAX(IM_MIN(y + ((91881*v)>>16), 255), 0);
    uint32_t g = IM_MAX(IM_MIN(y - (((22554*u)+(46802*v))>>16), 255), 0);
    uint32_t b = IM_MAX(IM_MIN(y + ((116130*u)>>16), 255), 0);
    return IM_RGB565(IM_R825(r), IM_G826(g), IM_B825(b));
}

////////////////////////////////////////////////////////////////////////////////

static save_image_format_t imblib_parse_extension(image_t *img, const char *path)
{
    size_t l = strlen(path);
    const char *p = path + l;
    if (l >= 5) {
               if (((p[-1] == 'g') || (p[-1] == 'G'))
               &&  ((p[-2] == 'e') || (p[-2] == 'E'))
               &&  ((p[-3] == 'p') || (p[-3] == 'P'))
               &&  ((p[-4] == 'j') || (p[-4] == 'J'))
               &&  ((p[-5] == '.') || (p[-5] == '.'))) {
                    // Will convert to JPG if not.
                    return FORMAT_JPG;
        }
    }
    if (l >= 4) {
               if (((p[-1] == 'g') || (p[-1] == 'G'))
               &&  ((p[-2] == 'p') || (p[-2] == 'P'))
               &&  ((p[-3] == 'j') || (p[-3] == 'J'))
               &&  ((p[-4] == '.') || (p[-4] == '.'))) {
                    // Will convert to JPG if not.
                    return FORMAT_JPG;
        } else if (((p[-1] == 'p') || (p[-1] == 'P'))
               &&  ((p[-2] == 'm') || (p[-2] == 'M'))
               &&  ((p[-3] == 'b') || (p[-3] == 'B'))
               &&  ((p[-4] == '.') || (p[-4] == '.'))) {
                    if (IM_IS_JPEG(img)) {
                        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError,
                        "Image is not BMP!"));
                    }
                    return FORMAT_BMP;
        } else if (((p[-1] == 'm') || (p[-1] == 'M'))
               &&  ((p[-2] == 'p') || (p[-2] == 'P'))
               &&  ((p[-3] == 'p') || (p[-3] == 'P'))
               &&  ((p[-4] == '.') || (p[-4] == '.'))) {
                    if (!IM_IS_RGB565(img)) {
                        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError,
                        "Image is not PPM!"));
                    }
                    return FORMAT_PNM;
        } else if (((p[-1] == 'm') || (p[-1] == 'M'))
               &&  ((p[-2] == 'g') || (p[-2] == 'G'))
               &&  ((p[-3] == 'p') || (p[-3] == 'P'))
               &&  ((p[-4] == '.') || (p[-4] == '.'))) {
                    if (!IM_IS_GS(img)) {
                        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError,
                        "Image is not PGM!"));
                    }
                    return FORMAT_PNM;
        }
    }
    return FORMAT_DONT_CARE;
}

static bool imlib_read_geometry(FIL *fp, image_t *img, const char *path, img_read_settings_t *rs)
{
    file_read_open(fp, path);
    char magic[2];
    read_data(fp, &magic, 2);
    file_close(fp);

    bool vflipped = false;
    if ((magic[0]=='P')
    && ((magic[1]=='2') || (magic[1]=='3')
    ||  (magic[1]=='5') || (magic[1]=='6'))) { // PPM
        rs->format = FORMAT_PNM;
        file_read_open(fp, path);
        file_buffer_on(fp); // REMEMBER TO TURN THIS OFF LATER!
        ppm_read_geometry(fp, img, path, &rs->ppm_rs);
    } else if ((magic[0]=='B') && (magic[1]=='M')) { // BMP
        rs->format = FORMAT_BMP;
        file_read_open(fp, path);
        file_buffer_on(fp); // REMEMBER TO TURN THIS OFF LATER!
        vflipped = bmp_read_geometry(fp, img, path, &rs->bmp_rs);
    } else {
        ff_unsupported_format(NULL);
    }
    imblib_parse_extension(img, path); // Enforce extension!
    return vflipped;
}

static void imlib_read_pixels(FIL *fp, image_t *img, int line_start, int line_end, img_read_settings_t *rs)
{
    switch (rs->format) {
        case FORMAT_DONT_CARE: // won't happen
            break;
        case FORMAT_BMP:
            bmp_read_pixels(fp, img, line_start, line_end, &rs->bmp_rs);
            break;
        case FORMAT_PNM:
            ppm_read_pixels(fp, img, line_start, line_end, &rs->ppm_rs);
            break;
        case FORMAT_JPG: // won't happen
            break;
    }
}

void imlib_image_operation(image_t *img, const char *path, image_t *other, line_op_t op)
{
    if (path) {
        uint32_t size = fb_avail() / 2;
        void *alloc = fb_alloc(size); // We have to do this before the read.
        // This code reads a window of an image in at a time and then executes
        // the line operation on each line in that window before moving to the
        // next window. The vflipped part is here because BMP files can be saved
        // vertically flipped resulting in us reading the image backwards.
        FIL fp;
        image_t temp;
        img_read_settings_t rs;
        bool vflipped = imlib_read_geometry(&fp, &temp, path, &rs);
        if (!IM_EQUAL(img, &temp)) {
            ff_not_equal(&fp);
        }
        // When processing vertically flipped images the read function will fill
        // the window up from the bottom. The read function assumes that the
        // window is equal to an image in size. However, since this is not the
        // case we shrink the window size to how many lines we're buffering.
        temp.pixels = alloc;
        temp.h = (size / (temp.w * temp.bpp)); // round down
        // This should never happen unless someone forgot to free.
        if ((!temp.pixels) || (!temp.h)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_MemoryError,
                                               "Not enough memory available!"));
        }
        for (int i=0; i<img->h; i+=temp.h) { // goes past end
            int can_do = IM_MIN(temp.h, img->h-i);
            imlib_read_pixels(&fp, &temp, 0, can_do, &rs);
            for (int j=0; j<can_do; j++) {
                if (!vflipped) {
                    op(img, i+j, temp.pixels+(temp.w*temp.bpp*j));
                } else {
                    op(img, (img->h-i-can_do)+j, temp.pixels+(temp.w*temp.bpp*j));
                }
            }
        }
        file_buffer_off(&fp);
        file_close(&fp);
        fb_free();
    } else {
        if (!IM_EQUAL(img, other)) {
            ff_not_equal(NULL);
        }
        for (int i=0; i<img->h; i++) {
            op(img, i, other->pixels + (img->w * img->bpp * i));
        }
    }
}

void imlib_load_image(image_t *img, const char *path)
{
    FIL fp;
    file_read_open(&fp, path);
    char magic[2];
    read_data(&fp, &magic, 2);
    file_close(&fp);

    if ((magic[0]=='P')
    && ((magic[1]=='2') || (magic[1]=='3')
    ||  (magic[1]=='5') || (magic[1]=='6'))) { // PPM
        ppm_read(img, path);
    } else if ((magic[0]=='B') && (magic[1]=='M')) { // BMP
        bmp_read(img, path);
    } else if ((magic[0]==0xFF) && (magic[1]==0xD8)) { // JPEG
        jpeg_read(img, path);
    } else {
        ff_unsupported_format(NULL);
    }
    imblib_parse_extension(img, path); // Enforce extension!
}

void imlib_save_image(image_t *img, const char *path, rectangle_t *roi, int quality)
{
    switch (imblib_parse_extension(img, path)) {
        case FORMAT_DONT_CARE:
            if (IM_IS_JPEG(img)) {
                char *new_path = strcat(strcpy(fb_alloc(strlen(path)+5), path), ".jpg");
                jpeg_write(img, new_path, quality);
                fb_free();
            } else {
                char *new_path = strcat(strcpy(fb_alloc(strlen(path)+5), path), ".bmp");
                bmp_write_subimg(img, new_path, roi);
                fb_free();
            }
            break;
        case FORMAT_BMP:
            bmp_write_subimg(img, path, roi);
            break;
        case FORMAT_PNM:
            ppm_write_subimg(img, path, roi);
            break;
        case FORMAT_JPG:
            jpeg_write(img, path, quality);
            break;
    }
}

void imlib_copy_image(image_t *dst, image_t *src, rectangle_t *roi)
{
    if (IM_IS_JPEG(src)) {
        dst->w = src->w;
        dst->h = src->h;
        dst->bpp = src->bpp;
        dst->pixels = xalloc(src->bpp);
        memcpy(dst->pixels, src->pixels, src->bpp);
    } else {
        rectangle_t rect;
        if (!rectangle_subimg(src, roi, &rect)) ff_no_intersection(NULL);
        dst->w = rect.w;
        dst->h = rect.h;
        dst->bpp = src->bpp;
        dst->pixels = xalloc(rect.w * rect.h * src->bpp);
        uint8_t *dst_pointer = dst->pixels;
        for (int i = rect.y; i < (rect.y + rect.h); i++) {
            int length = rect.w * src->bpp;
            memcpy(dst_pointer,
                   src->pixels + (rect.x * src->bpp) + (i * src->w * src->bpp),
                   length);
            dst_pointer += length;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// Get pixel (handles boundary check and image type check).
int imlib_get_pixel(image_t *img, int x, int y)
{
    return (IM_X_INSIDE(img, x) && IM_Y_INSIDE(img, y)) ?
        ( IM_IS_GS(img)
        ? IM_GET_GS_PIXEL(img, x, y)
        : IM_GET_RGB565_PIXEL(img, x, y) )
    : 0;
}

// Set pixel (handles boundary check and image type check).
void imlib_set_pixel(image_t *img, int x, int y, int p)
{
    if (IM_X_INSIDE(img, x) && IM_Y_INSIDE(img, y)) {
        if (IM_IS_GS(img)) {
            IM_SET_GS_PIXEL(img, x, y, p);
        } else {
            IM_SET_RGB565_PIXEL(img, x, y, p);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void imlib_draw_line(image_t *img, int x0, int y0, int x1, int y1, int c)
{
    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    int sx = x0<x1 ? 1 : -1;
    int sy = y0<y1 ? 1 : -1;
    int err = (dx>dy ? dx : -dy)/2;
    for (;;) {
        imlib_set_pixel(img, x0, y0, c);
        if (x0==x1 && y0==y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

void imlib_draw_rectangle(image_t *img, int rx, int ry, int rw, int rh, int c)
{
    if (rw<=0 || rh<=0) {
        return;
    }
    for (int i=rx, j=rx+rw, k=ry+rh-1; i<j; i++) {
        imlib_set_pixel(img, i, ry, c);
        imlib_set_pixel(img, i, k, c);
    }
    for (int i=ry+1, j=ry+rh-1, k=rx+rw-1; i<j; i++) {
        imlib_set_pixel(img, rx, i, c);
        imlib_set_pixel(img, k, i, c);
    }
}

void imlib_draw_circle(image_t *img, int cx, int cy, int r, int c)
{
    int x = r, y = 0, radiusError = 1-x;
    while (x>=y) {
        imlib_set_pixel(img,  x + cx,  y + cy, c);
        imlib_set_pixel(img,  y + cx,  x + cy, c);
        imlib_set_pixel(img, -x + cx,  y + cy, c);
        imlib_set_pixel(img, -y + cx,  x + cy, c);
        imlib_set_pixel(img, -x + cx, -y + cy, c);
        imlib_set_pixel(img, -y + cx, -x + cy, c);
        imlib_set_pixel(img,  x + cx, -y + cy, c);
        imlib_set_pixel(img,  y + cx, -x + cy, c);
        y++;
        if (radiusError<0) {
            radiusError += 2 * y + 1;
        } else {
            x--;
            radiusError += 2 * (y - x + 1);
        }
    }
}

void imlib_draw_string(image_t *img, int x_off, int y_off, const char *str, int c)
{
    const int anchor = x_off;
    for(char ch, last='\0'; (ch=*str); str++, last=ch) {
        if (last=='\r' && ch=='\n') { // handle "\r\n" strings
            continue;
        }
        if (ch=='\n' || ch=='\r') { // handle '\n' or '\r' strings
            x_off = anchor;
            y_off += font[0].h; // newline height == space height
            continue;
        }
        if (ch<' ' || ch>'~') {
            imlib_draw_rectangle(img,(x_off+1),(y_off+1),font[0].w-2,font[0].h-2,c);
            continue;
        }
        const glyph_t *g = &font[ch-' '];
        for (int y=0; y<g->h; y++) {
            for (int x=0; x<g->w; x++) {
                if (g->data[y] & (1<<(g->w-x))) {
                    imlib_set_pixel(img, (x_off+x), (y_off+y), c);
                }
            }
        }
        x_off += g->w;
    }
}

////////////////////////////////////////////////////////////////////////////////

void imlib_binary(image_t *img,
                  int num_thresholds, simple_color_t *l_thresholds, simple_color_t *h_thresholds,
                  bool invert)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            bool in = false;
            for (int k=0; k<num_thresholds; k++) {
                in |= invert ^
                      ((l_thresholds[k].G <= pixels[i])
                   && (pixels[i] <= h_thresholds[k].G));
            }
            pixels[i] = in ? 0xFF : 0;
        }
    } else {
        uint16_t *pixels = (uint16_t *) img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            const int pixel = pixels[i];
            const int lab_l = IM_RGB5652L(pixel);
            const int lab_a = IM_RGB5652A(pixel);
            const int lab_b = IM_RGB5652B(pixel);
            bool in = false;
            for (int k=0; k<num_thresholds; k++) {
                in |= invert ^
                     (((l_thresholds[k].L <= lab_l)
                   && (lab_l <= h_thresholds[k].L))
                   && ((l_thresholds[k].A <= lab_a)
                   && (lab_a <= h_thresholds[k].A))
                   && ((l_thresholds[k].B <= lab_b)
                   && (lab_b <= h_thresholds[k].B)));
            }
            pixels[i] = in ? 0xFFFF : 0;
        }
    }
}

void imlib_invert(image_t *img)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            pixels[i] = ~pixels[i];
        }
    } else {
        uint16_t *pixels = (uint16_t *) img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            pixels[i] = ~pixels[i];
        }
    }
}

static void imlib_and_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] &= other[i];
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] &= ((uint16_t *) other)[i];
        }
    }
}

void imlib_and(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_and_line_op);
}

static void imlib_nand_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] & other[i]);
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] & ((uint16_t *) other)[i]);
        }
    }
}

void imlib_nand(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_nand_line_op);
}

static void imlib_or_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] |= other[i];
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] |= ((uint16_t *) other)[i];
        }
    }
}

void imlib_or(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_or_line_op);
}

static void imlib_nor_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] | other[i]);
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] | ((uint16_t *) other)[i]);
        }
    }
}

void imlib_nor(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_nor_line_op);
}

static void imlib_xor_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] ^= other[i];
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] ^= ((uint16_t *) other)[i];
        }
    }
}

void imlib_xor(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_xor_line_op);
}

static void imlib_xnor_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] ^ other[i]);
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = ~(pixels[i] ^ ((uint16_t *) other)[i]);
        }
    }
}

void imlib_xnor(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_xnor_line_op);
}

static void imlib_erode_dilate(image_t *img, int ksize, int threshold, int e_or_d)
{
    int brows = ksize + 1;
    uint8_t *buffer = fb_alloc(img->w * brows * img->bpp);
    if (IM_IS_GS(img)) {
        for (int y=0; y<img->h; y++) {
            for (int x=0; x<img->w; x++) {
                // We're writing into the buffer like if it were a window.
                int buffer_idx = ((y%brows)*img->w)+x;
                buffer[buffer_idx] = IM_GET_GS_PIXEL(img, x, y);
                if ((!!buffer[buffer_idx]) == e_or_d) {
                    continue; // short circuit (makes this very fast - usually)
                }
                int acc = e_or_d ? 0 : -1; // don't count center pixel...
                for (int j=-ksize; j<=ksize; j++) {
                    for (int k=-ksize; k<=ksize; k++) {
                        if (IM_X_INSIDE(img, x+k) && IM_Y_INSIDE(img, y+j)) {
                            acc += !!IM_GET_GS_PIXEL(img, x+k, y+j);
                        } else { // outer pixels should not affect result.
                            acc += e_or_d ? 0 : 1;
                            // 1 for erode prevents acc from being lower.
                            // 0 for dilate prevents acc from being higher.
                        }
                    }
                }
                if (!e_or_d) {
                    // Preserve original pixel value...
                    if (acc < threshold) buffer[buffer_idx] = 0; // clear
                } else {
                    // Preserve original pixel value...
                    if (acc > threshold) buffer[buffer_idx] = -1; // set
                }
            }
            if (y>=ksize) {
                memcpy(img->pixels+((y-ksize)*img->w),
                       buffer+(((y-ksize)%brows)*img->w),
                       img->w * sizeof(uint8_t));
            }
        }
        for (int y=img->h-ksize; y<img->h; y++) {
            memcpy(img->pixels+(y*img->w),
                   buffer+((y%brows)*img->w),
                   img->w * sizeof(uint8_t));
        }
    } else {
        for (int y=0; y<img->h; y++) {
            for (int x=0; x<img->w; x++) {
                // We're writing into the buffer like if it were a window.
                int buffer_idx = ((y%brows)*img->w)+x;
                ((uint16_t *) buffer)[buffer_idx] = IM_GET_RGB565_PIXEL(img, x, y);
                if ((!!((uint16_t *) buffer)[buffer_idx]) == e_or_d) {
                    continue; // short circuit (makes this very fast - usually)
                }
                int acc = e_or_d ? 0 : -1; // don't count center pixel...
                for (int j=-ksize; j<=ksize; j++) {
                    for (int k=-ksize; k<=ksize; k++) {
                        if (IM_X_INSIDE(img, x+k) && IM_Y_INSIDE(img, y+j)) {
                            acc += !!IM_GET_RGB565_PIXEL(img, x+k, y+j);
                        } else { // outer pixels should not affect result.
                            acc += e_or_d ? 0 : 1;
                            // 1 for erode prevents acc from being lower.
                            // 0 for dilate prevents acc from being higher.
                        }
                    }
                }
                if (!e_or_d) {
                    // Preserve original pixel value...
                    if (acc < threshold) ((uint16_t *) buffer)[buffer_idx] = 0; // clear
                } else {
                    // Preserve original pixel value...
                    if (acc > threshold) ((uint16_t *) buffer)[buffer_idx] = -1; // set
                }
            }
            if (y>=ksize) {
                memcpy(((uint16_t *) img->pixels)+((y-ksize)*img->w),
                       ((uint16_t *) buffer)+(((y-ksize)%brows)*img->w),
                       img->w * sizeof(uint16_t));
            }
        }
        for (int y=img->h-ksize; y<img->h; y++) {
            memcpy(((uint16_t *) img->pixels)+(y*img->w),
                   ((uint16_t *) buffer)+((y%brows)*img->w),
                   img->w * sizeof(uint16_t));
        }
    }
    fb_free();
}

void imlib_erode(image_t *img, int ksize, int threshold)
{
    // Threshold should be equal to ((ksize*2)+1)*((ksize*2)+1)-1
    // for normal operation. E.g. for ksize==3 -> threshold==8
    // Basically you're adjusting the number of pixels that
    // must be set in the kernel (besides the center) for the output to be 1.
    // Erode normally requires all pixels to be 1.
    imlib_erode_dilate(img, ksize, threshold, 0);
}

void imlib_dilate(image_t *img, int ksize, int threshold)
{
    // Threshold should be equal to 0
    // for normal operation. E.g. for ksize==3 -> threshold==0
    // Basically you're adjusting the number of pixels that
    // must be set in the kernel (besides the center) for the output to be 1.
    // Dilate normally requires one pixel to be 1.
    imlib_erode_dilate(img, ksize, threshold, 1);
}

////////////////////////////////////////////////////////////////////////////////

void imlib_negate(image_t *img)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            pixels[i] = IM_MAX_GS - pixels[i];
        }
    } else {
        uint16_t *pixels = (uint16_t *) img->pixels;
        for (int i=0, j=img->w*img->h; i<j; i++) {
            const int pixel = pixels[i];
            const int r = IM_MAX_R5 - IM_R565(pixel);
            const int g = IM_MAX_G6 - IM_G565(pixel);
            const int b = IM_MAX_B5 - IM_B565(pixel);
            pixels[i] = IM_RGB565(r, g, b);
        }
    }
}

static void imlib_difference_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = abs(pixels[i] - other[i]);
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            const int pixel = pixels[i], other_pixel = ((uint16_t *) other)[i];
            const int r = abs(IM_R565(pixel) - IM_R565(other_pixel));
            const int g = abs(IM_G565(pixel) - IM_G565(other_pixel));
            const int b = abs(IM_B565(pixel) - IM_B565(other_pixel));
            pixels[i] = IM_RGB565(r, g, b);
        }
    }
}

void imlib_difference(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_difference_line_op);
}

static void imlib_replace_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        memcpy(pixels, other, img->w * sizeof(uint8_t));
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        memcpy(pixels, other, img->w * sizeof(uint16_t));
    }
}

void imlib_replace(image_t *img, const char *path, image_t *other)
{
    imlib_image_operation(img, path, other, imlib_replace_line_op);
}

static uint32_t alpha_temp;

static void imlib_blend_line_op(image_t *img, int line, uint8_t *other)
{
    if (IM_IS_GS(img)) {
        uint8_t *pixels = img->pixels + (img->w * line);
        for (int i=0; i<img->w; i++) {
            pixels[i] = __SMUAD(alpha_temp,__PKHBT(pixels[i],other[i],16))>>8;
        }
    } else {
        uint16_t *pixels = ((uint16_t *) img->pixels) + (img->w * line);
        for (int i=0; i<img->w; i++) {
            const int pixel = pixels[i], other_pixel = ((uint16_t *) other)[i];
            uint32_t vr = __PKHBT(IM_R565(pixel), IM_R565(other_pixel), 16);
            uint32_t vg = __PKHBT(IM_G565(pixel), IM_G565(other_pixel), 16);
            uint32_t vb = __PKHBT(IM_B565(pixel), IM_B565(other_pixel), 16);
            uint32_t r = __SMUAD(alpha_temp, vr)>>8;
            uint32_t g = __SMUAD(alpha_temp, vg)>>8;
            uint32_t b = __SMUAD(alpha_temp, vb)>>8;
            pixels[i] = IM_RGB565(r, g, b);
        }
    }
}

void imlib_blend(image_t *img, const char *path, image_t *other, int alpha)
{
    alpha_temp = __PKHBT((256-alpha), alpha, 16);
    imlib_image_operation(img, path, other, imlib_blend_line_op);
}

////////////////////////////////////////////////////////////////////////////////

void imlib_histeq(image_t *img)
{
    int a = img->w * img->h;
    float s = IM_MAX_GS / ((float)a);
    uint32_t *hist = fb_alloc0(IM_G_HIST_SIZE * sizeof(uint32_t));

    if (IM_IS_GS(img)) {

        /* compute image histogram */
        for (int i=0; i<a; i++) {
            hist[img->pixels[i]] += 1;
        }

        /* compute the CDF */
        for (int i=0, sum=0; i<IM_G_HIST_SIZE; i++) {
            sum += hist[i];
            hist[i] = sum;
        }

        for (int i=0; i<a; i++) {
            img->pixels[i] =  s * hist[img->pixels[i]];
        }

    } else {

        uint16_t *pixels = (uint16_t *) img->pixels;

        /* compute image histogram */
        for (int i=0; i<a; i++) {
            hist[yuv_table[pixels[i]*3]+128] += 1;
        }

        /* compute the CDF */
        for (int i=0, sum=0; i<IM_G_HIST_SIZE; i++) {
            sum += hist[i];
            hist[i] = sum;
        }

        for (int i=0; i<a; i++) {
            uint8_t y = s * hist[yuv_table[pixels[i]*3]+128];
            int8_t u = yuv_table[(pixels[i]*3)+1];
            int8_t v = yuv_table[(pixels[i]*3)+2];
            pixels[i] = imlib_yuv_to_rgb(y, u, v);
        }

    }

    fb_free();
}

////////////////////////////////////////////////////////////////////////////////

int imlib_image_mean(image_t *src)
{
    int s=0;
    int n=src->w*src->h;

    for (int i=0; i<n; i++) {
        s += src->pixels[i];
    }

    /* mean */
    return s/n;
}

// One pass standard deviation.
int imlib_image_std(image_t *src)
{
    int w=src->w;
    int h=src->h;
    int n=w*h;
    uint8_t *data=src->pixels;

    uint32_t s=0, sq=0;
    for (int i=0; i<n; i+=2) {
        s += data[i+0]+data[i+1];
        uint32_t tmp = __PKHBT(data[i+0], data[i+1], 16);
        sq = __SMLAD(tmp, tmp, sq);
    }

    if (n%2) {
        s += data[n-1];
        sq += data[n-1]*data[n-1];
    }

    /* mean */
    int m = s/n;

    /* variance */
    uint32_t v = sq/n-(m*m);

    /* std */
    return fast_sqrtf(v);
}
