/* The standard MLV App image profiles, more can be added here. */
static image_profile_t default_image_profiles[] =
{
    { /* PROFILE_STANDARD */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_None,
        .gamma_power = 3.15,
        .colour_gamut = GAMUT_Rec709
    },
    { /* PROFILE_TONEMAPPED */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_Reinhard,
        .gamma_power = 3.15,
        .colour_gamut = GAMUT_Rec709
    },
    { /* PROFILE_FILM */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_Tangent,
        .gamma_power = 3.465,
        .colour_gamut = GAMUT_Rec709
    },
    { /* PROFILE_ALEXA_LOG */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_AlexaLogC,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_CINEON_LOG */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_CineonLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_AlexaWideGamutRGB
    },
    { /* PROFILE_SONY_LOG_3 */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_SonySLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_SonySGamut3
    },
    { /* PROFILE_LINEAR */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_None,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709
    },
    { /* PROFILE_SRGB */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_sRGB,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709
    },
    { /* PROFILE_REC709 */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_Rec709,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709
    },
    { /* BMDFilm */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_BMDFilm,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_BmdFilm
    }
};
