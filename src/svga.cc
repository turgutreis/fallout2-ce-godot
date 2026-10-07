#include "svga.h"

#include <limits.h>
#include <string.h>
#include <vector>

#include <SDL.h>

#include "config.h"
#include "display_monitor.h"
#include "draw.h"
#include "game_sound.h"
#include "interface.h"
#include "memory.h"
#include "mouse.h"
#include "win32.h"
#include "window_manager.h"
#include "window_manager_private.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect _scr_size;

// 0x6ACA18
void (*_scr_blit)(unsigned char* src, int src_pitch, int a3, int src_x, int src_y, int src_width, int src_height, int dest_x, int dest_y) = _GNW95_ShowRect;

// 0x6ACA1C
void (*_zero_mem)() = nullptr;

SDL_Window* gSdlWindow = nullptr;
SDL_Surface* gSdlSurface = nullptr;
SDL_Renderer* gSdlRenderer = nullptr;
SDL_Texture* gSdlTexture = nullptr;
SDL_Surface* gSdlTextureSurface = nullptr;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CAD08
int _init_mode_320_200()
{
    return _GNW95_init_mode_ex(320, 200, 8);
}

// 0x4CAD40
int _init_mode_320_400()
{
    return _GNW95_init_mode_ex(320, 400, 8);
}

// 0x4CAD5C
int _init_mode_640_480_16()
{
    return -1;
}

// 0x4CAD64
int _init_mode_640_480()
{
    return _init_vesa_mode(640, 480);
}

// 0x4CAD94
int _init_mode_640_400()
{
    return _init_vesa_mode(640, 400);
}

// 0x4CADA8
int _init_mode_800_600()
{
    return _init_vesa_mode(800, 600);
}

// 0x4CADBC
int _init_mode_1024_768()
{
    return _init_vesa_mode(1024, 768);
}

// 0x4CADD0
int _init_mode_1280_1024()
{
    return _init_vesa_mode(1280, 1024);
}

// 0x4CADF8
void _get_start_mode_()
{
}

// 0x4CADFC
void _zero_vid_mem()
{
    if (_zero_mem) {
        _zero_mem();
    }
}

// 0x4CAE1C
int _GNW95_init_mode_ex(int width, int height, int bpp)
{
    bool fullscreen = true;
    int scale = 1;

    Config resolutionConfig;
    if (configInit(&resolutionConfig)) {
        if (configRead(&resolutionConfig, "f2_res.ini", false)) {
            int screenWidth;
            if (configGetInt(&resolutionConfig, "MAIN", "SCR_WIDTH", &screenWidth)) {
                width = screenWidth;
            }

            int screenHeight;
            if (configGetInt(&resolutionConfig, "MAIN", "SCR_HEIGHT", &screenHeight)) {
                height = screenHeight;
            }

            bool windowed;
            if (configGetBool(&resolutionConfig, "MAIN", "WINDOWED", &windowed)) {
                fullscreen = !windowed;
            }

            int scaleValue;
            if (configGetInt(&resolutionConfig, "MAIN", "SCALE_2X", &scaleValue)) {
                scale = scaleValue + 1; // 0 = 1x, 1 = 2x
                // Only allow scaling if resulting game resolution is >= 640x480
                if ((width / scale) < 640 || (height / scale) < 480) {
                    scale = 1;
                } else {
                    width /= scale;
                    height /= scale;
                }
            }

            configGetBool(&resolutionConfig, "IFACE", "IFACE_BAR_MODE", &gInterfaceBarMode);
            configGetInt(&resolutionConfig, "IFACE", "IFACE_BAR_WIDTH", &gInterfaceBarWidth);
            configGetInt(&resolutionConfig, "IFACE", "IFACE_BAR_SIDE_ART", &gInterfaceSidePanelsImageId);
            configGetBool(&resolutionConfig, "IFACE", "IFACE_BAR_SIDES_ORI", &gInterfaceSidePanelsExtendFromScreenEdge);

            int crtFilterValue;
            if (configGetInt(&resolutionConfig, "MAIN", "CRT_FILTER", &crtFilterValue)) {
                setCrtFilterMode(crtFilterValue);
            }
        }
        configFree(&resolutionConfig);
    }

    if (_GNW95_init_window(width, height, fullscreen, scale) == -1) {
        return -1;
    }

    if (directDrawInit(width, height, bpp) == -1) {
        return -1;
    }

    _scr_size.left = 0;
    _scr_size.top = 0;
    _scr_size.right = width - 1;
    _scr_size.bottom = height - 1;

    _mouse_blit_trans = nullptr;
    _scr_blit = _GNW95_ShowRect;
    _zero_mem = _GNW95_zero_vid_mem;
    _mouse_blit = _GNW95_ShowRect;

    return 0;
}

// 0x4CAECC
int _init_vesa_mode(int width, int height)
{
    return _GNW95_init_mode_ex(width, height, 8);
}

// 0x4CAEDC
int _GNW95_init_window(int width, int height, bool fullscreen, int scale)
{
    if (gSdlWindow == nullptr) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

        Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

        if (fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }

        gSdlWindow = SDL_CreateWindow(gProgramWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width * scale, height * scale, windowFlags);
        if (gSdlWindow == nullptr) {
            return -1;
        }

        if (!createRenderer(width, height)) {
            destroyRenderer();

            SDL_DestroyWindow(gSdlWindow);
            gSdlWindow = nullptr;

            return -1;
        }
    }

    return 0;
}

// 0x4CAF9C
int directDrawInit(int width, int height, int bpp)
{
    if (gSdlSurface != nullptr) {
        unsigned char* palette = directDrawGetPalette();
        directDrawFree();

        if (directDrawInit(width, height, bpp) == -1) {
            return -1;
        }

        directDrawSetPalette(palette);

        return 0;
    }

    gSdlSurface = SDL_CreateRGBSurface(0, width, height, bpp, 0, 0, 0, 0);

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    return 0;
}

// 0x4CB1B0
void directDrawFree()
{
    if (gSdlSurface != nullptr) {
        SDL_FreeSurface(gSdlSurface);
        gSdlSurface = nullptr;
    }
}

// 0x4CB310
void directDrawSetPaletteInRange(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB568
void directDrawSetPalette(unsigned char* palette)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB68C
unsigned char* directDrawGetPalette()
{
    // 0x6ACA24
    static unsigned char palette[768];

    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color* colors = gSdlSurface->format->palette->colors;

        for (int index = 0; index < 256; index++) {
            SDL_Color* color = &(colors[index]);
            palette[index * 3] = color->r >> 2;
            palette[index * 3 + 1] = color->g >> 2;
            palette[index * 3 + 2] = color->b >> 2;
        }
    }

    return palette;
}

// 0x4CB850
void _GNW95_ShowRect(unsigned char* src, int srcPitch, int a3, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY)
{
    blitBufferToBuffer(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
}

// Clears drawing surface.
//
// 0x4CBBC8
void _GNW95_zero_vid_mem()
{
    if (!gProgramIsActive) {
        return;
    }

    unsigned char* surface = (unsigned char*)gSdlSurface->pixels;
    for (int y = 0; y < gSdlSurface->h; y++) {
        memset(surface, 0, gSdlSurface->w);
        surface += gSdlSurface->pitch;
    }

    SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&_scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&_scr_size);
}

int screenGetVisibleHeight()
{
    int windowBottomMargin = 0;

    if (!gInterfaceBarMode) {
        windowBottomMargin = INTERFACE_BAR_HEIGHT;
    }
    return screenGetHeight() - windowBottomMargin;
}

static SDL_Texture* gCrtOverlayTexture = nullptr;
static int gCrtFilterMode = CRT_FILTER_OFF;

static void updateCrtOverlayTexture(int width, int height)
{
    if (gCrtOverlayTexture != nullptr) {
        SDL_DestroyTexture(gCrtOverlayTexture);
        gCrtOverlayTexture = nullptr;
    }

    if (gCrtFilterMode == CRT_FILTER_OFF || gSdlRenderer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    gCrtOverlayTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (gCrtOverlayTexture == nullptr) {
        return;
    }

    SDL_SetTextureBlendMode(gCrtOverlayTexture, SDL_BLENDMODE_BLEND);

    std::vector<Uint32> pixels(width * height, 0);

    float centerX = width * 0.5f;
    float centerY = height * 0.5f;

    // Scanline-Abstand: Bei höheren Auflösungen (>= 600 z.B. 720p/800p) alle 3 Zeilen (240-266 sichtbare Scanlines)
    // Bei niedrigeren Auflösungen (480p) alle 2 Zeilen (240 sichtbare Scanlines)
    const int scanlinePeriod = (height >= 600) ? 3 : 2;

    for (int y = 0; y < height; ++y) {
        int rowInPeriod = y % scanlinePeriod;
        bool isScanlineGap = (rowInPeriod == 0);

        if (gCrtFilterMode == CRT_FILTER_SCANLINES) {
            // Mode 1: Deutliche, sofort sichtbare Scanlines
            // Die Abtastzeile hat tiefes Schwarz (~70% Abdunklung), der Strahlkern bleibt 100% klar
            Uint8 a = isScanlineGap ? 175 : 0;
            Uint32 pixelColor = ((Uint32)a << 24);
            for (int x = 0; x < width; ++x) {
                pixels[y * width + x] = pixelColor;
            }
        } else if (gCrtFilterMode == CRT_FILTER_RETRO_CRT) {
            // Mode 2: Voller Retro CRT Röhren-Look
            // - Kräftige Scanlines (alpha 195 -> ~76% Abdunklung)
            // - Echtes RGB-Phosphor-Gitter (Rot, Grün, Blau Subpixel-Streifen)
            // - Abgerundete Röhrenecken & Glas-Vignette
            float ny = (float)(y - centerY) / centerY;
            float ny2 = ny * ny;
            float ny4 = ny2 * ny2;

            for (int x = 0; x < width; ++x) {
                float nx = (float)(x - centerX) / centerX;
                float nx2 = nx * nx;
                float nx4 = nx2 * nx2;

                // Abgerundete Ecken (Superellipse / Curved Tube Glass)
                float corner = nx4 + ny4;
                if (corner > 1.50f) {
                    // Außerhalb der Röhre: massives Schwarz
                    pixels[y * width + x] = 0xFF000000;
                    continue;
                }

                int totalA = 0;
                Uint8 r = 0, g = 0, b = 0;

                if (isScanlineGap) {
                    // Dunkle Kathodenstrahl-Abtastzeile
                    totalA = 195;
                } else {
                    // Phosphor Mask: Rote, grüne und blaue Streifen
                    int subpixel = x % 3;
                    if (subpixel == 0) {
                        r = 255; g = 30; b = 30; totalA = 26;
                    } else if (subpixel == 1) {
                        r = 30; g = 255; b = 30; totalA = 26;
                    } else {
                        r = 30; g = 30; b = 255; totalA = 26;
                    }
                }

                // Vignette & Glaswölbung am Rand
                float distSq = nx2 + ny2;
                int vigAlpha = (int)(distSq * 28.0f);

                // Weicher Schatten an den abgerundeten Glasecken
                if (corner > 1.25f) {
                    vigAlpha += (int)((corner - 1.25f) * 600.0f);
                }

                totalA += vigAlpha;
                if (totalA > 255) totalA = 255;

                // ARGB8888 packen
                pixels[y * width + x] = ((Uint32)totalA << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
            }
        }
    }

    SDL_UpdateTexture(gCrtOverlayTexture, nullptr, pixels.data(), width * sizeof(Uint32));
}

void setCrtFilterMode(int mode)
{
    if (mode < 0 || mode >= CRT_FILTER_COUNT) {
        mode = CRT_FILTER_OFF;
    }
    gCrtFilterMode = mode;
    if (gSdlRenderer != nullptr) {
        updateCrtOverlayTexture(screenGetWidth(), screenGetHeight());
    }
}

int getCrtFilterMode()
{
    return gCrtFilterMode;
}

void cycleCrtFilterMode()
{
    int nextMode = (gCrtFilterMode + 1) % CRT_FILTER_COUNT;
    setCrtFilterMode(nextMode);

    soundPlayFile("toggle");

    const char* modeNames[CRT_FILTER_COUNT] = {
        "Aus",
        "Sanfte Scanlines",
        "Retro CRT-Monitor"
    };

    char msg[64];
    snprintf(msg, sizeof(msg), "CRT-Filter: %s", modeNames[gCrtFilterMode]);
    displayMonitorAddMessage(msg);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == nullptr) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == nullptr) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, nullptr, nullptr, nullptr) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == nullptr) {
        return false;
    }

    if (gCrtFilterMode != CRT_FILTER_OFF) {
        updateCrtOverlayTexture(width, height);
    }

    return true;
}

static void destroyRenderer()
{
    if (gCrtOverlayTexture != nullptr) {
        SDL_DestroyTexture(gCrtOverlayTexture);
        gCrtOverlayTexture = nullptr;
    }

    if (gSdlTextureSurface != nullptr) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = nullptr;
    }

    if (gSdlTexture != nullptr) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = nullptr;
    }

    if (gSdlRenderer != nullptr) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = nullptr;
    }
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

void renderPresent()
{
    SDL_UpdateTexture(gSdlTexture, nullptr, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, nullptr, nullptr);
    if (gCrtFilterMode != CRT_FILTER_OFF && gCrtOverlayTexture != nullptr) {
        SDL_RenderCopy(gSdlRenderer, gCrtOverlayTexture, nullptr, nullptr);
    }
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
