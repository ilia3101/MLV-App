#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"

/* Nothing to see here */
char * filmprofile_fj = "3 1 7 3 -8.32176290957420738970e-01 1.0835954635952433"
    "9110e+00 -1.53596291045552013621e+00 3.67384514989079313807e+00 -5.6287459"
    "6479495181711e+00 3.33493552026486006490e-01 -7.12665460749794998918e+00 9"
    ".52623022802043806223e-01 4.98567373146714931664e+00 -3.190878325195455555"
    "98e+00 6.40049169520185312621e+00 1.48307139386843722662e+00 -2.2101335796"
    "2759204867e+00 1.33574963765940140092e+01 -4.34871285918206063048e-01 -1.9"
    "5293014567889300359e+00 2.09644613739775698136e+00 6.48035843659867882849e"
    "+00 -2.39748195290617882591e+00 -3.16130675196507793245e+00 -1.11688161646"
    "700034879e+00 4.23098912462331999684e-01 -8.20734848356459067986e+00 2.577"
    "97338617953331052e+00 6.25356006420329535445e+00 3.45135112364741791779e+0"
    "0 -1.80124851238127337005e+00 5.52922961362061293755e+00 1.145045799659712"
    "31559e+01 3.02092623576809993224e+00 -5.82868432257680790798e+00 -4.767736"
    "95678160169820e+00 1.43527519841506236986e+01 2.54331162851854086782e+00 -"
    "2.70380976583335863594e+00 1.60820646684338841581e+00 -5.02616726969326244"
    "046e+00 1.97955756024031015450e+00 -5.94141534554111672151e+00 -2.10259087"
    "120419296824e+00 -4.79761248096838510691e-01 -2.15286380744306921065e-01 -"
    "4.08106451251237789535e+00 -1.98932654029564948139e-01 5.81507384499231338"
    "992e+00 8.98602754784793233966e+00 -1.57889637430522600248e+00 -9.82598541"
    "026572375179e-02 -8.64513642871019283298e-01 -6.58914154017710140820e-01 -"
    "1.78457442827277001918e+00 2.58094544332886099980e+00";


filterObject_t * initFilterObject()
{
    filterObject_t * filter = malloc(sizeof(filterObject_t));

    /* 
     * """""""""Diclaimer"""""""""
     * These networks are *definitely* *not* imitating any well known plugin 
     * that claims to convert anything to film or anything like that.
     */

    /* FJ preset */
    FILE * fj_preset = fmemopen(filmprofile_fj, strlen(filmprofile_fj), "rb");
    filter->net_fj = genann_read(fj_preset);
    fclose(fj_preset);


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
    else if (filter->filter_option == 1) /* Not avalible yet */
        net = genann_copy(filter->net_kd);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        pixel[0] = ((double)pix[0])/65535.0;
        pixel[1] = ((double)pix[1])/65535.0;
        pixel[2] = ((double)pix[2])/65535.0;
        const double * filtered = genann_run(net, pixel);
        pix[0] = filter->processed[(size_t)(filtered[0]*65535.0)] + filter->original[pix[0]];
        pix[1] = filter->processed[(size_t)(filtered[1]*65535.0)] + filter->original[pix[1]];
        pix[2] = filter->processed[(size_t)(filtered[2]*65535.0)] + filter->original[pix[2]];
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
        filter->processed[i] = (uint16_t)(strength * (double)i);
        filter->original[i] = (uint16_t)(istrength * (double)i);
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