#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "filter.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)


/* Nothing to see here */
#include "film.h"
char * filmprofile_fj = FILM_FJ;
char * filmprofile_vis3 = FILM_VIS3;
char * filmprofile_p400 = FILM_P400;
char * filmprofile_toyc = FILM_TOYC;
char * filmprofile_sepia = FILM_SEPIA;

/* Cuz there will be many */
FILE * open_filter(char * text)
{
    FILE * file;

    file = fopen("__filter_temp__", "wb");
    fputs(text, file);
    fclose(file);
    file = fopen("__filter_temp__", "rb");

    return file;
}

#define close_filter(filter) { fclose(filter); remove("__filter_temp__"); }

filterObject_t * initFilterObject()
{
    filterObject_t * filter = malloc(sizeof(filterObject_t));

    /* 
     * """""""""Diclaimer"""""""""
     * These networks are *definitely* *not* imitating any well known plugin 
     * that claims to convert anything to film or anything like that.
     */

    /* FJ preset */
    FILE * fj_preset = open_filter(filmprofile_fj);
    filter->net_fj = genann_read(fj_preset);
    close_filter(fj_preset);
    
    /* Kodak Vision 3 preset */
    FILE * vis3_preset = open_filter(filmprofile_vis3);
    filter->net_vis3 = genann_read(vis3_preset);
    close_filter(vis3_preset);

    /* Kodak Portra 400 preset */
    FILE * p400_preset = open_filter(filmprofile_p400);
    filter->net_p400 = genann_read(p400_preset);
    close_filter(p400_preset);

    /* Toy Camera */
    FILE * toy_cam = open_filter(filmprofile_toyc);
    filter->net_toyc = genann_read(toy_cam);
    close_filter(toy_cam);

    /* Sepia Tone */
    FILE * sepia = open_filter(filmprofile_sepia);
    filter->net_sepia = genann_read(sepia);
    close_filter(sepia);

    filterObjectSetFilterStrength(filter, 1.0);

    return filter;
}

void applyFilterObject( filterObject_t * filter,
                        int width, int height,
                        uint16_t * image )
{
    if (filter->strength < 0.01) return;

    double pixel[3];

    uint16_t * end = image + (width * height * 3);

    /* We need a copy or it gets messed up on many threads */
    genann * net;
    if (filter->filter_option == 0)
        net = genann_copy(filter->net_fj);
    else if (filter->filter_option == 1)
        net = genann_copy(filter->net_vis3);
    else if (filter->filter_option == 2)
        net = genann_copy(filter->net_p400);
    else if (filter->filter_option == 3)
        net = genann_copy(filter->net_toyc);
    else if (filter->filter_option == 4)
        net = genann_copy(filter->net_sepia);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        pixel[0] = ((double)pix[0])/65535.0;
        pixel[1] = ((double)pix[1])/65535.0;
        pixel[2] = ((double)pix[2])/65535.0;
        const double * filtered = genann_run(net, pixel);
        pix[0] = LIMIT16(filter->processed[(size_t)(filtered[0]*65535.0)] + filter->original[pix[0]]);
        pix[1] = LIMIT16(filter->processed[(size_t)(filtered[1]*65535.0)] + filter->original[pix[1]]);
        pix[2] = LIMIT16(filter->processed[(size_t)(filtered[2]*65535.0)] + filter->original[pix[2]]);
    }

    genann_free(net);
}

/* Set effect strength, 0.0-1.0 */
void filterObjectSetFilterStrength(filterObject_t * filter, double strength)
{
    filter->strength = strength;
    double istrength = 1.0 - strength;
    for (int i = 0; i < 65536; ++i)
    {
        filter->processed[i] = (int32_t)(strength * (double)i);
        filter->original[i] = (int32_t)(istrength * (double)i);
    }
}

void freeFilterObject(filterObject_t * filter)
{
    genann_free(filter->net_fj);
    free(filter);
}

void filterObjectSetFilter(filterObject_t * filter, int filterID)
{
    filter->filter_option = filterID;
}
