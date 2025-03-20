/* The standard MLV App image profiles, more can be added here. */
static image_profile_t default_image_profiles[] =
{
    { /* PROFILE_STANDARD */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_None,
        .gamma_power = 3.15,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "pow(x, 1/3.15)"
    },
    { /* PROFILE_TONEMAPPED */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_Reinhard,
        .gamma_power = 3.15,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "(x < 0.0) ? 0 : pow(x / (1.0 + x), 1/3.15)"
    },
    { /* PROFILE_FILM */
        .allow_creative_adjustments = 1,
        .tonemap_function = TONEMAP_Tangent,
        .gamma_power = 3.465,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "pow(atan(x) / atan(8.0), 1/3.465)"
    },
    { /* PROFILE_ALEXA_LOG */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_AlexaLogC,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_AlexaWideGamutRGB,
        .transfer_function = "(x > 0.010591) ? (0.247190 * log10(5.555556 * x + 0.052272) + 0.385537) : (5.367655 * x + 0.092809)"
    },
    { /* PROFILE_CINEON_LOG */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_CineonLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_AlexaWideGamutRGB,
        .transfer_function = "((log10(x * (1.0 - 0.0108) + 0.0108)) * 300.0 + 685.0) / 1023.0"
    },
    { /* PROFILE_SONY_LOG_3 */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_SonySLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_SonySGamut3,
        .transfer_function = "(x >= 0.01125000) ? (420.0 + log10((x + 0.01) / (0.18 + 0.01)) * 261.5) / 1023.0 : (x * (171.2102946929 - 95.0) / 0.01125000 + 95.0) / 1023.0"
    },
    { /* PROFILE_LINEAR */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_None,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "x"
    },
    { /* PROFILE_SRGB */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_sRGB,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "x < 0.0031308 ? x * 12.92 : (1.055 * pow(x, 1.0 / 2.4)) - 0.055"
    },
    { /* PROFILE_REC709 */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_Rec709,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec709,
        .transfer_function = "(x <= 0.018) ? (x * 4.5) : 1.099 * pow( x, (0.45) ) - 0.099"
    },
    { /* Davinci Wide Gamut Intermediate */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_DavinciIntermediate,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_DavinciWideGamut,
        .transfer_function = "(x <= 0.00262409) ? (x * 10.44426855) : (log10(x + 0.0075) / log10(2) + 7.0) * 0.07329248"
    },
    { /* PROFILE_FUJI_FLOG */
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_None,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Rec2020,
        .transfer_function = "(x < 0.00089) ? (8.735631 * x + 0.092864) : (0.344676 * log10(0.555556 * x + 0.009468) + 0.790453)"
    },
    { /* PROFILE_CANON_LOG (source: WhitePaper_Clog_optoelectronic.pdf by Canon)*/
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_CanonLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_Canon_Cinema,
        .transfer_function = "(0.529136 * (log10 ( 10.1596 * x + 1 ))) + 0.0730597"
    },
    { /* PROFILE_CANON_LOG (source: VARICAM_V-Log_V-Gamut.pdf)*/
        .allow_creative_adjustments = 0,
        .tonemap_function = TONEMAP_PanasonicVLog,
        .gamma_power = 1.0,
        .colour_gamut = GAMUT_PanasonivV,
        .transfer_function = "(x >= 0.01) ? (0.241514 * log10(x + 0.00873) + 0.598206) : (5.6 * x + 0.125)"
    }
};
