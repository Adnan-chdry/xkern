#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

/*
 *  config.h - hardcoded boot screen configuration.
 *  13 values; will be auto-generated in future releases.
 *  xkern 26.0.8
 */

/* 1. logo display width (pixels, scaled from source) */
#define BOOT_LOGO_DISP_W    180

/* 2. logo display height (pixels) */
#define BOOT_LOGO_DISP_H    180

/* 3. spinner animation time (ms for full rotation) */
#define BOOT_SPINNER_TIME   800

/* 4. spinner arc length (degrees) */
#define BOOT_SPINNER_ARC    60

/* 5. spinner diameter (pixels) */
#define BOOT_SPINNER_DIA    48

/* 6. background color  (0xRRGGBB) */
#define BOOT_BG_COLOR       0x000000

/* 7. spinner color     (0xRRGGBB) */
#define BOOT_SPINNER_COLOR  0xFFFFFF

/* 8. boot screen minimum duration (ms) before allowing exit */
#define BOOT_MIN_DURATION   1800

/* 9. version display flag (1 = show, 0 = hide) */
#define BOOT_SHOW_VERSION   1

/* 10. fade-in time (ms, 0 = instant) */
#define BOOT_FADE_IN_MS     400

/* 11. y-offset of logo from screen centre (positive = below) */
#define BOOT_LOGO_Y_OFS     -40

/* 12. y-offset of spinner from logo bottom (positive = below) */
#define BOOT_SPINNER_Y_OFS  30

/* 13. plymouth master toggle: 1 = show splash, 0 = skip it (kernel testing /
 *     full debug log on screen from the very start). */
#define BOOT_ENABLE_PLYMOUTH 0

#endif /* BOOT_CONFIG_H */
