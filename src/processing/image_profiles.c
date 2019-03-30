/* The standard MLV App image profiles, more can be added here. */
static image_profile_t default_image_profiles[] =
{
    { /* PROFILE_STANDARD */
        .allow_creative_adjustments = 1,
        .tone_mapping_function = NULL,
        .gamma_power = STANDARD_GAMMA,
        .processing_gamut = GAMUT_ProPhotoRGB
    },
    { /* PROFILE_TONEMAPPED */
        .allow_creative_adjustments = 1,
        .tone_mapping_function = &ReinhardTonemap,
        .gamma_power = STANDARD_GAMMA,
        .processing_gamut = GAMUT_ProPhotoRGB
    },
    { /* PROFILE_FILM */
        .allow_creative_adjustments = 1,
        .tone_mapping_function = &TangentTonemap,
        .gamma_power = STANDARD_GAMMA*1.1,
        .processing_gamut = GAMUT_ProPhotoRGB
    },
    { /* PROFILE_ALEXA_LOG */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &AlexaLogCTonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_CINEON_LOG */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &CineonLogTonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_SONY_LOG_3 */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &SonySLogTonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_SonySGamut3
    },
    { /* PROFILE_LINEAR */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = NULL,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_SRGB */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &sRGBTonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_REC709 */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &Rec709Tonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* BMDFilm */
        .allow_creative_adjustments = 0,
        .tone_mapping_function = &BmdFilmTonemap,
        .gamma_power = 1.0,
        .processing_gamut = GAMUT_AlexaWideGamutRGB
    }
};

/* Matrices XYZ -> RGB */
static double colour_gamuts[][9] = {
    { /* GAMUT_Rec709 */
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252
    },
    { /* GAMUT_Rec2020 */
         1.72466, -0.36222, -0.25442,
        -0.66941,  1.62275,  0.01240,
         0.01826, -0.04444,  0.94329
    },
    { /* GAMUT_ACES_AP0 */
        1.0498110175, 0.0000000000, -0.0000974845,
        -0.4959030231, 1.3733130458, 0.0982400361,
        0.0000000000, 0.0000000000, 0.9912520182
    },
    { /* GAMUT_AdobeRGB */
         2.0413690, -0.5649464, -0.3446944,
        -0.9692660,  1.8760108,  0.0415560,
         0.0134474, -0.1183897,  1.0154096
    },
    { /* GAMUT_ProPhotoRGB */
         1.3459433, -0.2556075, -0.0511118,
        -0.5445989,  1.5081673,  0.0205351,
         0.0000000,  0.0000000,  1.2118128
    },
    { /* GAMUT_XYZ */
         1, 0, 0, 0, 1, 0, 0, 0, 1
    },
    { /* GAMUT_AlexaWideGamutRGB */
         1.789066, -0.482534, -0.200076,
        -0.639849,  1.396400,  0.194432,
        -0.041532,  0.082335,  0.878868
    },
    { /* GAMUT_SonySGamut3 */
         1.8467789693, -0.5259861230, -0.2105452114,
        -0.4441532629,  1.2594429028,  0.1493999729,
         0.0408554212,  0.0156408893,  0.8682072487
    }
};

char * gamutnames[] = {

 "GAMUT_Rec709", /* Rec709/sRGB */
 "GAMUT_Rec2020",
 "GAMUT_ACES_AP0",
 "GAMUT_AdobeRGB",
 "GAMUT_ProPhotoRGB",
 "GAMUT_XYZ",
 "GAMUT_AlexaWideGamutRGB",
 "GAMUT_SonySGamut3"
};

double * get_matrix_xyz_to_rgb(int gamut)
{
    return colour_gamuts[gamut];
}

void get_matrix_rgb_to_xyz(int gamut, double * out)
{
    puts(gamutnames[gamut]);
    invertMatrix(colour_gamuts[gamut], out);
}