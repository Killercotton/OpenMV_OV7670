/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Integral image.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <arm_math.h>
#include "imlib.h"
#include "fb_alloc.h"

void imlib_integral_image_alloc(i_image_t *sum, int w, int h)
{
    sum->w = w;
    sum->h = h;
    sum->data = fb_alloc(w * h * sizeof(*sum->data));
}

void imlib_integral_image_free(i_image_t *sum)
{
    // 1 allocation
    fb_free();
}

void imlib_integral_image(image_t *src, i_image_t *sum)
{
    typeof(*src->data) *img_data = src->data;
    typeof(*sum->data) *sum_data = sum->data;

    // Compute first column to avoid branching
    for (int s=0, x=0; x<src->w; x++) {
        /* sum of the current row (integer) */
        s += img_data[x];
        sum_data[x] = s;
    }

    for (int y=1; y<src->h; y++) {
        /* loop over the number of columns */
        for (int s=0, x=0; x<src->w; x++) {
            /* sum of the current row (integer) */
            s += img_data[y*src->w+x];
            sum_data[y*src->w+x] = s+sum_data[(y-1)*src->w+x];
        }
    }
}

void imlib_integral_image_scaled(image_t *src, i_image_t *sum)
{
    typeof(*src->data) *img_data = src->data;
    typeof(*sum->data) *sum_data = sum->data;

    int x_ratio = (int)((src->w<<16)/sum->w) +1;
    int y_ratio = (int)((src->h<<16)/sum->h) +1;

    // Compute first column to avoid branching
    for (int s=0, x=0; x<sum->w; x++) {
        int sx = (x*x_ratio)>>16;
        /* sum of the current row (integer) */
        s += img_data[sx];
        sum_data[x] = s;
    }

    for (int y=1; y<sum->h; y++) {
        int sy = (y*y_ratio)>>16;
        /* loop over the number of columns */
        for (int s=0, x=0; x<sum->w; x++) {
            int sx = (x*x_ratio)>>16;

            /* sum of the current row (integer) */
            s += img_data[sy*src->w+sx];
            sum_data[y*sum->w+x] = s+sum_data[(y-1)*sum->w+x];
        }
    }
}

void imlib_integral_image_sq(image_t *src, i_image_t *sum)
{
    typeof(*src->data) *img_data = src->data;
    typeof(*sum->data) *sum_data = sum->data;

    // Compute first column to avoid branching
    for (int s=0, x=0; x<src->w; x++) {
        /* sum of the current row (integer) */
        s += img_data[src->w+x] * img_data[src->w+x];
        sum_data[src->w+x] = s;
    }

    for (int y=1; y<src->h; y++) {
        /* loop over the number of columns */
        for (int s=0, x=0; x<src->w; x++) {
            /* sum of the current row (integer) */
            s += img_data[y*src->w+x] * img_data[y*src->w+x];
            sum_data[y*src->w+x] = s+sum_data[(y-1)*src->w+x];
        }
    }

}

uint32_t imlib_integral_lookup(i_image_t *sum, int x, int y, int w, int h)
{
#define PIXEL_AT(x,y)\
    (sum->data[sum->w*(y)+(x)])
    return PIXEL_AT(w+x, h+y) + PIXEL_AT(x, y) - PIXEL_AT(w+x, y) - PIXEL_AT(x, h+y);
#undef  PIXEL_AT
}
