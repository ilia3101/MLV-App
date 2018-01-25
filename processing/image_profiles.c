/* The standard MLV App image profiles, more can be added here. */
static image_profile_t default_image_profiles[] = 
{
    { /* PROFILE_STANDARD */
        .disable_settings = {
            .saturation = 1,
            .curves = 1,
            .tonemapping = 0
        },
        .tone_mapping_function = NULL,
        .gamma_power = STANDARD_GAMMA,
        /* xy chromacity is unused right now so not filled in... */
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_TONEMAPPED */
        .disable_settings = {
            .saturation = 1,
            .curves = 1,
            .tonemapping = 1
        },
        .tone_mapping_function = &ReinhardTonemap,
        .gamma_power = STANDARD_GAMMA,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_FILM */
        .disable_settings = {
            .saturation = 1,
            .curves = 1,
            .tonemapping = 1
        },
        .tone_mapping_function = &TangentTonemap,
        .gamma_power = STANDARD_GAMMA*1.1,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_ALEXA_LOG */
        .disable_settings = {
            .saturation = 0,
            .curves = 0,
            .tonemapping = 1
        },
        .tone_mapping_function = &AlexaLogCTonemap,
        .gamma_power = 1.0,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_CINEON_LOG */
        .disable_settings = {
            .saturation = 0,
            .curves = 0,
            .tonemapping = 1
        },
        .tone_mapping_function = &CineonLogTonemap,
        .gamma_power = 1.0,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_SONY_LOG_3 */
        .disable_settings = {
            .saturation = 0,
            .curves = 0,
            .tonemapping = 1
        },
        .tone_mapping_function = &SonySLogTonemap,
        .gamma_power = 1.0,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    },
    { /* PROFILE_LINEAR */
        .disable_settings = {
            .saturation = 1,
            .curves = 1,
            .tonemapping = 0
        },
        .tone_mapping_function = NULL,
        .gamma_power = 1.0,
        .xy_chromaticity = {
            .red = {0,0},
            .green = {0,0},
            .blue = {0,0},
            .white = {0,0},
        }
    }
};
