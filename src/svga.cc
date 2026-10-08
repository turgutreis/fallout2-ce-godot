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

            int filterVal;
            if (configGetInt(&resolutionConfig, "MAIN", "UPSCALE_FILTER", &filterVal)) {
                setUpscaleFilterMode(filterVal);
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

static int gUpscaleFilterMode = UPSCALE_FILTER_NEAREST;
static SDL_Texture* gScale2xTexture = nullptr;
static std::vector<Uint32> gScale2xBuffer;

static void applyTextureScaleMode()
{
    if (gSdlTexture != nullptr) {
        if (gUpscaleFilterMode == UPSCALE_FILTER_LINEAR) {
            SDL_SetTextureScaleMode(gSdlTexture, SDL_ScaleModeLinear);
        } else {
            SDL_SetTextureScaleMode(gSdlTexture, SDL_ScaleModeNearest);
        }
    }
}

static void scale2x(const Uint32* src, int srcWidth, int srcHeight, int srcPitchPixels,
                    Uint32* dst, int dstPitchPixels)
{
    for (int y = 0; y < srcHeight; ++y) {
        int yPrev = (y > 0) ? (y - 1) : 0;
        int yNext = (y < srcHeight - 1) ? (y + 1) : (srcHeight - 1);

        const Uint32* rowP = src + y * srcPitchPixels;
        const Uint32* rowA = src + yPrev * srcPitchPixels;
        const Uint32* rowC = src + yNext * srcPitchPixels;

        Uint32* dstRow0 = dst + (y * 2) * dstPitchPixels;
        Uint32* dstRow1 = dst + (y * 2 + 1) * dstPitchPixels;

        for (int x = 0; x < srcWidth; ++x) {
            int xPrev = (x > 0) ? (x - 1) : 0;
            int xNext = (x < srcWidth - 1) ? (x + 1) : (srcWidth - 1);

            Uint32 P = rowP[x];
            Uint32 A = rowA[x];
            Uint32 B = rowP[xNext];
            Uint32 C = rowC[x];
            Uint32 D = rowP[xPrev];

            // Scale2x rules:
            // 1 2
            // 3 4
            Uint32 E0 = (D == A && C != A && D != B) ? D : P;
            Uint32 E1 = (A == B && A != C && B != D) ? B : P;
            Uint32 E2 = (D == C && D != A && C != B) ? D : P;
            Uint32 E3 = (B == C && A != C && B != D) ? B : P;

            dstRow0[x * 2]     = E0;
            dstRow0[x * 2 + 1] = E1;
            dstRow1[x * 2]     = E2;
            dstRow1[x * 2 + 1] = E3;
        }
    }
}

void setUpscaleFilterMode(int mode)
{
    if (mode < 0 || mode >= UPSCALE_FILTER_COUNT) {
        mode = UPSCALE_FILTER_NEAREST;
    }
    gUpscaleFilterMode = mode;
    applyTextureScaleMode();
}

int getUpscaleFilterMode()
{
    return gUpscaleFilterMode;
}

void cycleUpscaleFilterMode()
{
    int nextMode = (gUpscaleFilterMode + 1) % UPSCALE_FILTER_COUNT;
    setUpscaleFilterMode(nextMode);

    soundPlayFile("toggle");

    const char* modeNames[UPSCALE_FILTER_COUNT] = {
        "Knackige Pixel (Original)",
        "Weichzeichnung (Bilinear)",
        "Scale2x Kantenglaettung"
    };

    char msg[64];
    snprintf(msg, sizeof(msg), "Bildfilter: %s", modeNames[gUpscaleFilterMode]);
    displayMonitorAddMessage(msg);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (gSdlRenderer == nullptr) {
        gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    }
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

    gScale2xTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width * 2, height * 2);
    if (gScale2xTexture != nullptr) {
        SDL_SetTextureScaleMode(gScale2xTexture, SDL_ScaleModeNearest);
        gScale2xBuffer.resize(width * 2 * height * 2);
    }

    applyTextureScaleMode();

    return true;
}

static void destroyRenderer()
{
    if (gScale2xTexture != nullptr) {
        SDL_DestroyTexture(gScale2xTexture);
        gScale2xTexture = nullptr;
    }
    gScale2xBuffer.clear();

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
    if (gUpscaleFilterMode == UPSCALE_FILTER_SCALE2X && gScale2xTexture != nullptr && gSdlTextureSurface != nullptr) {
        int w = screenGetWidth();
        int h = screenGetHeight();
        scale2x(
            (const Uint32*)gSdlTextureSurface->pixels,
            w, h, gSdlTextureSurface->pitch / sizeof(Uint32),
            gScale2xBuffer.data(), w * 2
        );
        SDL_UpdateTexture(gScale2xTexture, nullptr, gScale2xBuffer.data(), w * 2 * sizeof(Uint32));
        SDL_RenderClear(gSdlRenderer);
        SDL_RenderCopy(gSdlRenderer, gScale2xTexture, nullptr, nullptr);
    } else {
        SDL_UpdateTexture(gSdlTexture, nullptr, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
        SDL_RenderClear(gSdlRenderer);
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, nullptr, nullptr);
    }
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
