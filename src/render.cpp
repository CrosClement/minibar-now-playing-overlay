#include "render.h"

#include <d2d1_1helper.h>
#include <cmath>

using winrt::com_ptr;

namespace {

// ---------------------------------------------------------------- metriche --
// Overlay e docked stanno a circa il 70% delle misure originali: la barretta
// deve occupare l'angolo dello schermo, non presidiarlo. Le costanti restano
// in DIP, quindi la riduzione vale identica a ogni densità.
constexpr float kProgressH = 2.5f;

// Bar / Docked. Dalla 1.1 la barretta è ~10% più alta e ha più aria attorno ai
// contenuti: il margine dal bordo passa da 6 a 7 DIP e lo stacco fra copertina,
// testo e comandi da 6 a 8. Spessore della progress, bordo e pesi del testo
// restano quelli di prima: qui si cambia la misura, non lo stile.
constexpr float kBarHeight  = 50.f;
constexpr float kBarPad     = 7.f;
constexpr float kBarArt     = 36.f;  // = kBarHeight - kBarPad * 2
constexpr float kBarButton  = 23.f;
constexpr float kBarCorner  = 8.f;
constexpr float kBarProgY   = 40.5f;  // = kBarHeight - kBarPad - kProgressH
constexpr float kBarGap     = 8.f;    // copertina/testo/controlli
constexpr float kDockedMaxW = 420.f;
constexpr float kBarMinW    = 185.f;
constexpr float kBarMaxW    = 840.f;

// Docked sui bordi laterali: striscia stretta e alta quanto lo schermo. Il
// testo non c'è: in 56 DIP non ci sta, e ruotarlo sarebbe peggio che ometterlo.
constexpr float kSideWidth  = 56.f;
constexpr float kSideArt    = 44.f;
constexpr float kSideButton = 26.f;
constexpr float kSideGap    = 7.f;   // copertina → progress
constexpr float kSideGap2   = 12.f;  // progress → comandi

// Tile
constexpr float kTileMin    = 68.f;
constexpr float kTileMax    = 224.f;
constexpr float kTileCorner = 9.f;
constexpr float kTileButton = 24.f;
constexpr float kTileInset  = 7.f;

// Taskbar
constexpr float kTbPad    = 5.f;
constexpr float kTbButton = 26.f;
constexpr float kTbTextW  = 190.f;
constexpr float kTbMinH   = 28.f;
constexpr float kTbMaxH   = 72.f;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- geometria --
// Calcolata una volta sola e condivisa fra disegno e hit test: se divergessero,
// i pulsanti risponderebbero dove non sono disegnati.
struct Geometry {
    D2D1_RECT_F box{};
    D2D1_RECT_F art{};
    D2D1_RECT_F text{};
    D2D1_RECT_F progress{};
    D2D1_RECT_F buttons[3]{};
    float       corner      = 0.f;
    float       artCorner   = 0.f;
    bool        showButtons = false;
    bool        showText    = false;
};

void ThreeButtons(Geometry& g, float left, float centerY, float size) {
    for (int i = 0; i < 3; ++i) {
        const float x = left + static_cast<float>(i) * size;
        g.buttons[i]  = D2D1::RectF(x, centerY - size * 0.5f, x + size, centerY + size * 0.5f);
    }
}

Geometry ComputeGeometry(const RenderState& st, float w, float h) {
    Geometry g;

    switch (st.layout.kind) {
        case LayoutKind::Bar:
        case LayoutKind::Docked: {
            const bool docked = st.layout.kind == LayoutKind::Docked;
            const float cw    = docked ? Clamp(w, 0.f, kDockedMaxW) : w;
            const float x0    = docked ? (w - cw) * 0.5f : 0.f;

            g.box       = D2D1::RectF(x0, 0, x0 + cw, h);
            g.corner    = docked ? 0.f : kBarCorner;
            g.artCorner = 4.f;
            g.art = D2D1::RectF(x0 + kBarPad, kBarPad, x0 + kBarPad + kBarArt, kBarPad + kBarArt);

            const float ctrlRight = x0 + cw - kBarPad;
            const float ctrlLeft  = ctrlRight - kBarButton * 3.f;
            ThreeButtons(g, ctrlLeft, h * 0.5f, kBarButton);

            const float textLeft = x0 + kBarPad + kBarArt + kBarGap;
            g.text = D2D1::RectF(textLeft, 4.f, ctrlLeft - kBarGap, kBarProgY - 3.f);
            g.progress = D2D1::RectF(textLeft, kBarProgY, x0 + cw - kBarPad, kBarProgY + kProgressH);

            g.showText    = true;
            g.showButtons = docked || st.hovered;
            break;
        }

        case LayoutKind::DockedSide: {
            // Il gruppo sta al centro verticale della striscia: appiccicarlo in
            // alto lascerebbe un metro di vuoto sotto.
            g.box       = D2D1::RectF(0, 0, w, h);
            g.corner    = 0.f;
            g.artCorner = 5.f;

            const float pad     = (w - kSideArt) * 0.5f;
            const float buttons = kSideButton * 3.f;
            const float group   = kSideArt + kSideGap + kProgressH + kSideGap2 + buttons;
            const float top     = (h - group) * 0.5f;

            g.art      = D2D1::RectF(pad, top, pad + kSideArt, top + kSideArt);
            g.progress = D2D1::RectF(pad, top + kSideArt + kSideGap, pad + kSideArt,
                                     top + kSideArt + kSideGap + kProgressH);

            const float btnTop  = g.progress.bottom + kSideGap2;
            const float btnLeft = (w - kSideButton) * 0.5f;
            for (int i = 0; i < 3; ++i)
                g.buttons[i] = D2D1::RectF(btnLeft, btnTop + i * kSideButton, btnLeft + kSideButton,
                                           btnTop + (i + 1) * kSideButton);

            g.showText    = false;
            g.showButtons = true;
            break;
        }

        case LayoutKind::Tile: {
            // La copertina è il riquadro: niente testo, i comandi arrivano in
            // hover sopra una velatura.
            g.box       = D2D1::RectF(0, 0, w, h);
            g.corner    = kTileCorner;
            g.artCorner = kTileCorner;
            g.art       = g.box;

            g.progress = D2D1::RectF(kTileInset, h - kTileInset - kProgressH, w - kTileInset,
                                     h - kTileInset);

            ThreeButtons(g, (w - kTileButton * 3.f) * 0.5f, h * 0.5f - 2.f, kTileButton);

            g.showText    = false;
            g.showButtons = st.hovered;
            break;
        }

        case LayoutKind::Taskbar: {
            const float artSide = h - kTbPad * 2.f - kProgressH - 3.f;
            g.box       = D2D1::RectF(0, 0, w, h);
            g.corner    = 8.f;
            g.artCorner = 4.f;
            g.art       = D2D1::RectF(kTbPad, kTbPad, kTbPad + artSide, kTbPad + artSide);

            const float ctrlRight = w - kTbPad;
            const float ctrlLeft  = ctrlRight - kTbButton * 3.f;
            ThreeButtons(g, ctrlLeft, kTbPad + artSide * 0.5f, kTbButton);

            g.text     = D2D1::RectF(kTbPad + artSide + 8.f, kTbPad - 1.f, ctrlLeft - 8.f,
                                     kTbPad + artSide + 1.f);
            g.progress = D2D1::RectF(kTbPad, h - kTbPad - kProgressH, w - kTbPad, h - kTbPad);

            g.showText    = st.layout.expand > 0.02f;
            g.showButtons = true;
            break;
        }
    }
    return g;
}

// ------------------------------------------------------------------ colore --
void RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
    const float mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
    const float d  = mx - mn;

    v = mx;
    s = (mx > 0.f) ? d / mx : 0.f;
    if (d <= 0.f) {
        h = 0.f;
        return;
    }
    if (mx == r)
        h = 60.f * std::fmod((g - b) / d, 6.f);
    else if (mx == g)
        h = 60.f * ((b - r) / d + 2.f);
    else
        h = 60.f * ((r - g) / d + 4.f);
    if (h < 0.f) h += 360.f;
}

D2D1_COLOR_F HsvToRgb(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;

    float r = 0.f, g = 0.f, b = 0.f;
    if (h < 60.f) {
        r = c; g = x;
    } else if (h < 120.f) {
        r = x; g = c;
    } else if (h < 180.f) {
        g = c; b = x;
    } else if (h < 240.f) {
        g = x; b = c;
    } else if (h < 300.f) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }
    return D2D1::ColorF(r + m, g + m, b + m, 1.0f);
}

// La tinta della copertina non si usa com'è: su fondo scuro dev'essere accesa,
// su fondo chiaro deve scurire, e in entrambi i casi restare satura quanto
// basta per non sembrare fango.
D2D1_COLOR_F AccentColor(const ArtAccent& a, bool onLight) {
    const float s = Clamp(a.s, onLight ? 0.55f : 0.45f, 0.92f);
    const float v = onLight ? Clamp(a.v, 0.45f, 0.72f) : Clamp(a.v, 0.62f, 0.95f);
    return HsvToRgb(a.h, s, v);
}

D2D1_COLOR_F Mix(const D2D1_COLOR_F& base, const D2D1_COLOR_F& tint, float amount) {
    return D2D1::ColorF(base.r + (tint.r - base.r) * amount, base.g + (tint.g - base.g) * amount,
                        base.b + (tint.b - base.b) * amount, base.a);
}

// ----------------------------------------------------------------- palette --
struct Palette {
    D2D1_COLOR_F bg, border, title, artist, track, fill, vol, glyph, glyphOff, glyphDown, artHole,
        scrim;
};

// `accent` è già il colore risolto per questo fondo (nullptr = nessuna tinta).
Palette PaletteFor(const RenderState& st, const D2D1_COLOR_F* accent) {
    Palette p{};
    p.fill      = {0.114f, 0.725f, 0.329f, 1.0f};  // verde progress, se la copertina non tinge
    p.vol       = {0.95f, 0.95f, 0.98f, 0.95f};    // volume: chiaro, mai confondibile
    p.glyphDown = {1.0f, 1.0f, 1.0f, 1.0f};
    p.scrim     = {0.02f, 0.02f, 0.03f, 0.62f};

    if (st.layout.kind == LayoutKind::Taskbar) {
        // Qui lo sfondo è la taskbar, che segue il tema di sistema.
        if (st.lightTaskbar) {
            p.bg        = {0.0f, 0.0f, 0.0f, st.hovered ? 0.10f : 0.05f};
            p.border    = {0.0f, 0.0f, 0.0f, 0.08f};
            p.title     = {0.09f, 0.09f, 0.10f, 1.0f};
            p.artist    = {0.35f, 0.35f, 0.38f, 1.0f};
            p.track     = {0.0f, 0.0f, 0.0f, 0.18f};
            p.glyph     = {0.12f, 0.12f, 0.14f, 1.0f};
            p.glyphOff  = {0.0f, 0.0f, 0.0f, 0.25f};
            p.glyphDown = {0.0f, 0.0f, 0.0f, 1.0f};
            p.artHole   = {0.0f, 0.0f, 0.0f, 0.12f};
            p.vol       = {0.10f, 0.10f, 0.12f, 0.92f};
        } else {
            p.bg       = {1.0f, 1.0f, 1.0f, st.hovered ? 0.12f : 0.06f};
            p.border   = {1.0f, 1.0f, 1.0f, 0.07f};
            p.title    = {0.96f, 0.96f, 0.97f, 1.0f};
            p.artist   = {0.70f, 0.70f, 0.74f, 1.0f};
            p.track    = {1.0f, 1.0f, 1.0f, 0.20f};
            p.glyph    = {0.92f, 0.92f, 0.94f, 1.0f};
            p.glyphOff = {1.0f, 1.0f, 1.0f, 0.25f};
            p.artHole  = {1.0f, 1.0f, 1.0f, 0.12f};
        }
        // Dentro la taskbar il fondo è di Windows: si tinge solo la progress.
        if (accent) p.fill = *accent;
        return p;
    }

    // Overlay e docked disegnano il proprio fondo, e lo disegnano del colore che
    // il sistema dice alle applicazioni di usare.
    const bool  strip   = st.layout.kind == LayoutKind::Docked ||
                        st.layout.kind == LayoutKind::DockedSide;
    const float bgAlpha = strip ? 0.98f : 0.90f;

    if (st.lightTheme) {
        p.bg        = {0.97f, 0.97f, 0.98f, bgAlpha};
        p.border    = {0.0f, 0.0f, 0.0f, 0.12f};
        p.title     = {0.09f, 0.09f, 0.10f, 1.0f};
        p.artist    = {0.36f, 0.36f, 0.40f, 1.0f};
        p.track     = {0.0f, 0.0f, 0.0f, 0.14f};
        p.glyph     = {0.14f, 0.14f, 0.16f, 1.0f};
        p.glyphOff  = {0.0f, 0.0f, 0.0f, 0.25f};
        p.glyphDown = {0.0f, 0.0f, 0.0f, 1.0f};
        p.artHole   = {0.0f, 0.0f, 0.0f, 0.10f};
        p.vol       = {0.10f, 0.10f, 0.12f, 0.92f};
        // Sopra la copertina la velatura del riquadro va chiara, per reggere i
        // glifi scuri.
        p.scrim = {0.98f, 0.98f, 1.0f, 0.66f};
    } else {
        p.bg       = {0.078f, 0.078f, 0.086f, bgAlpha};
        p.border   = {1.0f, 1.0f, 1.0f, 0.09f};
        p.title    = {0.96f, 0.96f, 0.97f, 1.0f};
        p.artist   = {0.70f, 0.70f, 0.74f, 1.0f};
        p.track    = {1.0f, 1.0f, 1.0f, 0.16f};
        p.glyph    = {0.92f, 0.92f, 0.94f, 1.0f};
        p.glyphOff = {1.0f, 1.0f, 1.0f, 0.25f};
        p.artHole  = {1.0f, 1.0f, 1.0f, 0.10f};
    }

    if (accent) {
        const D2D1_COLOR_F tint = *accent;
        p.fill                  = tint;
        // Un soffio di colore nel fondo e nel bordo: si deve notare che la
        // barretta "sa" cosa sta suonando, non che è stata dipinta.
        p.bg     = Mix(p.bg, tint, 0.09f);
        p.border = Mix(p.border, tint, 0.30f);
    }
    return p;
}

bool Inside(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// ---------------------------------------------------------------- marquee ---
constexpr float kMarqueeSpeed = 34.f;   // DIP al secondo
constexpr float kMarqueeFade  = 12.f;   // larghezza della dissolvenza ai bordi
constexpr float kMarqueeWait  = 900.f;  // fermo prima di partire
constexpr float kMarqueeRest  = 1100.f; // fermo a fine corsa, prima di tornare

// Di quanto è spostato il titolo a sinistra, dato il tempo trascorso. Andata,
// pausa, ritorno: un ciclo che non fa mai saltare il testo.
float MarqueeOffset(double ms, float overflow) {
    if (overflow <= 0.f || ms < 0.0) return 0.f;

    const double run   = static_cast<double>(overflow) / kMarqueeSpeed * 1000.0;
    const double cycle = kMarqueeWait + run + kMarqueeRest + run;
    double       t     = std::fmod(ms, cycle);

    if (t < kMarqueeWait) return 0.f;
    t -= kMarqueeWait;
    if (t < run) return Clamp(static_cast<float>(t / 1000.0 * kMarqueeSpeed), 0.f, overflow);
    t -= run;
    if (t < kMarqueeRest) return overflow;
    t -= kMarqueeRest;
    return Clamp(overflow - static_cast<float>(t / 1000.0 * kMarqueeSpeed), 0.f, overflow);
}

}  // namespace

// ------------------------------------------------------------------ misura --
SIZE Renderer::Measure(const LayoutSpec& spec, UINT dpi) {
    const float k = static_cast<float>(dpi ? dpi : 96) / 96.f;
    float       w = spec.widthDip;
    float       h = spec.heightDip;

    switch (spec.kind) {
        case LayoutKind::Bar:
            w = Clamp(w, kBarMinW, kBarMaxW);
            h = kBarHeight;
            break;
        case LayoutKind::Docked:
            h = kBarHeight;
            break;
        case LayoutKind::DockedSide:
            w = kSideWidth;  // lo spessore lo decide il layout, l'altezza lo schermo
            break;
        case LayoutKind::Tile:
            w = h = Clamp(spec.widthDip, kTileMin, kTileMax);
            break;
        case LayoutKind::Taskbar: {
            h                 = Clamp(h, kTbMinH, kTbMaxH);
            const float art   = h - kTbPad * 2.f - kProgressH - 3.f;
            w                 = kTbPad + art + 10.f + kTbButton * 3.f + kTbPad;
            w += kTbTextW * Clamp(spec.expand, 0.f, 1.f);
            break;
        }
    }
    return SIZE{static_cast<LONG>(std::ceil(w * k)), static_cast<LONG>(std::ceil(h * k))};
}

// -------------------------------------------------------------------- init --
bool Renderer::Init(HWND hwnd, UINT dpi) {
    hwnd_ = hwnd;
    dpi_  = dpi ? dpi : 96;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    widthPx_  = static_cast<UINT>(rc.right - rc.left);
    heightPx_ = static_cast<UINT>(rc.bottom - rc.top);
    if (widthPx_ == 0) widthPx_ = 1;
    if (heightPx_ == 0) heightPx_ = 1;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 nullptr, d2dFactory_.put_void())))
        return false;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put()))))
        return false;

    auto makeFormat = [&](float size, DWRITE_FONT_WEIGHT weight,
                          com_ptr<IDWriteTextFormat>& out) -> bool {
        if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI Variable Text", nullptr, weight,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             size, L"", out.put())))
            return false;
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // Titoli lunghi: ellissi, mai a capo.
        com_ptr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(out.get(), ellipsis.put()))) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            out->SetTrimming(&trim, ellipsis.get());
        }
        return true;
    };
    // Barretta e docked: cresciuti insieme all'altezza, stesso peso di prima.
    if (!makeFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, fmtTitle_)) return false;
    if (!makeFormat(9.5f, DWRITE_FONT_WEIGHT_NORMAL, fmtArtist_)) return false;
    if (!makeFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, fmtTitleSm_)) return false;
    if (!makeFormat(9.5f, DWRITE_FONT_WEIGHT_NORMAL, fmtArtistSm_)) return false;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic_.put()))))
        return false;

    return CreateSurface();
}

bool Renderer::CreateSurface() {
    ReleaseSurface();

    if (FAILED(wic_->CreateBitmap(widthPx_, heightPx_, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, surface_.put())))
        return false;

    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    if (FAILED(d2dFactory_->CreateWicBitmapRenderTarget(surface_.get(), props, rt_.put())))
        return false;

    // Su fondo trasparente ClearType non è applicabile: meglio dirlo noi che
    // lasciare che D2D scelga a sorpresa.
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    if (FAILED(rt_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.put())))
        return false;

    // DIB top-down 32 bpp: destinazione di UpdateLayeredWindow.
    HDC screen = GetDC(nullptr);
    memDC_     = CreateCompatibleDC(screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = static_cast<LONG>(widthPx_);
    bi.bmiHeader.biHeight      = -static_cast<LONG>(heightPx_);
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!dib_ || !memDC_) {
        ReleaseSurface();
        return false;
    }
    oldBmp_ = SelectObject(memDC_, dib_);

    artSidePx_ = 0;  // forza il ri-decode alla prossima Draw
    return true;
}

void Renderer::ReleaseSurface() {
    brush_   = nullptr;
    layer_   = nullptr;
    art_     = nullptr;
    artPrev_ = nullptr;
    rt_      = nullptr;
    surface_ = nullptr;

    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    oldBmp_    = nullptr;
    dibBits_   = nullptr;
    artSidePx_ = 0;
}

void Renderer::Shutdown() {
    ReleaseSurface();
    fmtTitle_    = nullptr;
    fmtArtist_   = nullptr;
    fmtTitleSm_  = nullptr;
    fmtArtistSm_ = nullptr;
    dwrite_      = nullptr;
    wic_         = nullptr;
    d2dFactory_  = nullptr;
    hwnd_        = nullptr;
}

void Renderer::Resize(UINT widthPx, UINT heightPx, UINT dpi) {
    if (widthPx == 0) widthPx = 1;
    if (heightPx == 0) heightPx = 1;
    const bool dpiChanged = (dpi != 0 && dpi != dpi_);
    if (widthPx == widthPx_ && heightPx == heightPx_ && !dpiChanged) return;

    widthPx_  = widthPx;
    heightPx_ = heightPx;
    if (dpi) dpi_ = dpi;

    if (d2dFactory_) CreateSurface();
}

// ------------------------------------------------------------- copertina ---
void Renderer::SetThumbnail(const std::vector<uint8_t>& bytes) {
    // La copertina di prima non si butta: resta sotto mentre la nuova compare.
    artPrev_    = art_;
    accentPrev_ = accent_;

    artBytes_   = bytes;
    artSidePx_  = 0;
    art_        = nullptr;
    accent_     = ArtAccent{};
    accentDone_ = false;
}

// Colore dominante della copertina. Si guarda una miniatura di 12×12 pixel —
// la scala la fa WIC insieme a quella della copertina vera — e si sceglie la
// fascia di tinta che pesa di più, contando ogni pixel per quanto è saturo e
// scartando grigi ed estremi. Costo: una volta per traccia, su 144 pixel.
void Renderer::ComputeAccent(IWICBitmapSource* source) {
    accent_ = ArtAccent{};
    if (!source || !wic_) return;

    constexpr UINT kSide = 12;

    com_ptr<IWICBitmapScaler> scaler;
    if (FAILED(wic_->CreateBitmapScaler(scaler.put()))) return;
    if (FAILED(scaler->Initialize(source, kSide, kSide, WICBitmapInterpolationModeFant))) return;

    com_ptr<IWICFormatConverter> converter;
    if (FAILED(wic_->CreateFormatConverter(converter.put()))) return;
    if (FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut)))
        return;

    uint8_t pixels[kSide * kSide * 4]{};
    if (FAILED(converter->CopyPixels(nullptr, kSide * 4, sizeof(pixels), pixels))) return;

    constexpr int kBuckets = 18;  // fasce di tinta da 20°
    double        weight[kBuckets]{};
    double        sumH[kBuckets]{}, sumS[kBuckets]{}, sumV[kBuckets]{};

    for (UINT i = 0; i < kSide * kSide; ++i) {
        const uint8_t* p = pixels + i * 4;
        if (p[3] < 128) continue;  // trasparente: non fa colore

        float h = 0.f, s = 0.f, v = 0.f;
        RgbToHsv(p[2] / 255.f, p[1] / 255.f, p[0] / 255.f, h, s, v);
        if (s < 0.18f || v < 0.12f || v > 0.97f) continue;  // grigi, nero, bianco

        // Un pixel conta per quanto è saturo, e i mezzi toni contano più di
        // quelli quasi neri o quasi bianchi.
        const double w = s * (1.0 - std::fabs(v - 0.65f));
        const int    k = static_cast<int>(h / (360.f / kBuckets)) % kBuckets;

        weight[k] += w;
        sumH[k] += h * w;
        sumS[k] += s * w;
        sumV[k] += v * w;
    }

    auto wrap = [](int k) { return (k + kBuckets) % kBuckets; };

    // Una tinta a cavallo di due fasce non deve perdere metà del suo peso: si
    // sceglie la fascia guardando anche le vicine, e si media su tutte e tre.
    int    best   = -1;
    double bestW  = 0.0;
    double totalW = 0.0;
    for (int k = 0; k < kBuckets; ++k) totalW += weight[k];
    for (int k = 0; k < kBuckets; ++k) {
        const double mass = weight[wrap(k - 1)] + weight[k] + weight[wrap(k + 1)];
        if (weight[k] > 0.0 && mass > bestW) {
            bestW = mass;
            best  = k;
        }
    }

    // Troppo poco colore, o colore sparso su tutta la ruota (copertine in bianco
    // e nero, collage): non si tinge niente, resta il verde.
    if (best < 0 || bestW < 1.5 || bestW < totalW * 0.20) return;

    const double centerH = sumH[best] / weight[best];
    double       sw = 0.0, hAcc = 0.0, sAcc = 0.0, vAcc = 0.0;
    for (int d = -1; d <= 1; ++d) {
        const int k = wrap(best + d);
        if (weight[k] <= 0.0) continue;

        // Le tinte si sommano su un cerchio: 350° e 10° sono vicine, non lontane.
        double h = sumH[k] / weight[k];
        if (h - centerH > 180.0)
            h -= 360.0;
        else if (centerH - h > 180.0)
            h += 360.0;

        sw += weight[k];
        hAcc += h * weight[k];
        sAcc += sumS[k];
        vAcc += sumV[k];
    }
    if (sw <= 0.0) return;

    double h = std::fmod(hAcc / sw, 360.0);
    if (h < 0.0) h += 360.0;

    accent_.h     = static_cast<float>(h);
    accent_.s     = static_cast<float>(sAcc / sw);
    accent_.v     = static_cast<float>(vAcc / sw);
    accent_.valid = true;
}

void Renderer::DecodeThumbnail() {
    art_ = nullptr;
    if (artBytes_.empty() || !rt_ || !wic_ || artSidePx_ == 0) return;

    com_ptr<IWICStream> stream;
    if (FAILED(wic_->CreateStream(stream.put()))) return;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(artBytes_.data()),
                                            static_cast<DWORD>(artBytes_.size()))))
        return;

    com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(wic_->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                             decoder.put())))
        return;
    com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) return;

    // La tinta si calcola sul fotogramma decodificato, una volta per copertina:
    // un cambio di DPI o di layout ridecodifica l'immagine ma non il colore.
    if (!accentDone_) {
        ComputeAccent(frame.get());
        accentDone_ = true;
    }

    // Si scala subito alla dimensione di visualizzazione: in memoria non finisce
    // mai la copertina a piena risoluzione.
    com_ptr<IWICBitmapScaler> scaler;
    if (FAILED(wic_->CreateBitmapScaler(scaler.put()))) return;
    if (FAILED(scaler->Initialize(frame.get(), artSidePx_, artSidePx_,
                                  WICBitmapInterpolationModeFant)))
        return;

    com_ptr<IWICFormatConverter> converter;
    if (FAILED(wic_->CreateFormatConverter(converter.put()))) return;
    if (FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut)))
        return;

    rt_->CreateBitmapFromWicBitmap(converter.get(), nullptr, art_.put());
}

ID2D1Layer* Renderer::Layer() {
    if (!layer_ && rt_) rt_->CreateLayer(nullptr, layer_.put());
    return layer_.get();
}

// La copertina nuova compare sopra quella vecchia, che resta piena finché non
// è coperta: dissolvere entrambe a metà strada le farebbe schiarire tutte e due.
void Renderer::DrawArt(const D2D1_RECT_F& rect, float radius, float fade) {
    const auto  rr      = D2D1::RoundedRect(rect, radius, radius);
    const float opacity = Clamp(fade, 0.f, 1.f);

    if (!art_ && !artPrev_) {
        rt_->FillRoundedRectangle(rr, brush_.get());  // il chiamante ha già scelto il colore
        return;
    }

    auto paint = [&] {
        if (art_) {
            // Sotto la nuova ci vuole qualcosa: la vecchia se c'è, il
            // placeholder se questa è la prima copertina che arriva.
            if (opacity < 1.f) {
                if (artPrev_)
                    rt_->DrawBitmap(artPrev_.get(), rect);
                else
                    rt_->FillRoundedRectangle(rr, brush_.get());
            }
            rt_->DrawBitmap(art_.get(), rect, opacity);
        } else {
            // Nessuna copertina nuova (player chiuso): quella di prima svanisce
            // sul placeholder invece di sparire di colpo.
            rt_->FillRoundedRectangle(rr, brush_.get());
            if (artPrev_ && opacity < 1.f) rt_->DrawBitmap(artPrev_.get(), rect, 1.f - opacity);
        }
    };

    com_ptr<ID2D1RoundedRectangleGeometry> clip;
    if (SUCCEEDED(d2dFactory_->CreateRoundedRectangleGeometry(rr, clip.put()))) {
        if (ID2D1Layer* layer = Layer()) {
            rt_->PushLayer(D2D1::LayerParameters(rect, clip.get()), layer);
            paint();
            rt_->PopLayer();
            return;
        }
    }
    paint();
}

// ------------------------------------------------------------------ titolo --
Renderer::TitleSlot Renderer::TitleFor(const RenderState& st, float w, float h) const {
    TitleSlot slot;

    const Geometry g = ComputeGeometry(st, w, h);
    if (!g.showText || g.text.right <= g.text.left + 16.f) return slot;

    // NB: 'small' è una macro del Windows SDK (rpcndr.h), non usarla come nome.
    const bool compact = st.layout.kind == LayoutKind::Taskbar;
    slot.format        = compact ? fmtTitleSm_.get() : fmtTitle_.get();
    slot.text          = st.hasTrack ? st.title : std::wstring(L"Nothing playing");

    const bool  twoLines = st.hasTrack && !st.artist.empty();
    const float mid      = (g.text.top + g.text.bottom) * 0.5f;
    slot.rect  = twoLines ? D2D1::RectF(g.text.left, g.text.top, g.text.right, mid) : g.text;
    slot.shown = true;
    return slot;
}

float Renderer::MeasureText(const std::wstring& text, IDWriteTextFormat* format) const {
    if (!dwrite_ || !format || text.empty()) return 0.f;

    com_ptr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                         100000.f, 1000.f, layout.put())))
        return 0.f;

    DWRITE_TEXT_METRICS m{};
    if (FAILED(layout->GetMetrics(&m))) return 0.f;
    return m.widthIncludingTrailingWhitespace;
}

float Renderer::TitleOverflow(const RenderState& st) const {
    if (!rt_) return 0.f;

    const float     scale = 96.f / static_cast<float>(dpi_);
    const TitleSlot slot  = TitleFor(st, widthPx_ * scale, heightPx_ * scale);
    if (!slot.shown || slot.text.empty()) return 0.f;

    const float avail = slot.rect.right - slot.rect.left;
    const float wide  = MeasureText(slot.text, slot.format);
    return (wide > avail + 1.f) ? wide - avail : 0.f;
}

// ----------------------------------------------------------------- disegno --
void Renderer::Draw(const RenderState& st) {
    if (!rt_ || !dibBits_) return;

    const float    scale = 96.f / static_cast<float>(dpi_);
    const float    w     = widthPx_ * scale;
    const float    h     = heightPx_ * scale;
    const Geometry g     = ComputeGeometry(st, w, h);

    // La copertina va rigenerata se cambia la dimensione a cui si mostra
    // (cambio layout o cambio DPI). Fuori da BeginDraw.
    const UINT wantSide = static_cast<UINT>(
        std::ceil((g.art.right - g.art.left) * static_cast<float>(dpi_) / 96.f));
    if (wantSide != artSidePx_) {
        artSidePx_ = wantSide;
        DecodeThumbnail();
    }

    // Finita la dissolvenza, la copertina di prima non serve più.
    const float artFade = Clamp(st.artFade, 0.f, 1.f);
    if (artFade >= 1.f && (artPrev_ || accentPrev_.valid)) {
        artPrev_    = nullptr;
        accentPrev_ = ArtAccent{};
    }

    // Dopo il decode: è lì che si scopre la tinta della copertina. Anche il
    // colore attraversa la dissolvenza, altrimenti la progress bar scatterebbe
    // al colore nuovo mentre la copertina sta ancora sfumando.
    const bool onLight =
        (st.layout.kind == LayoutKind::Taskbar) ? st.lightTaskbar : st.lightTheme;
    D2D1_COLOR_F tint{};
    bool         tinted = false;
    if (accent_.valid) {
        tint   = AccentColor(accent_, onLight);
        tinted = true;
    }
    if (accentPrev_.valid && artFade < 1.f) {
        const D2D1_COLOR_F before = AccentColor(accentPrev_, onLight);
        tint                      = tinted ? Mix(before, tint, artFade) : before;
        tinted                    = true;
    }
    const Palette pal = PaletteFor(st, tinted ? &tint : nullptr);

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0, 0.0f));

    // --- sfondo ---
    // Le strisce agganciate a un bordo non hanno angoli tondi: toccano il bordo
    // dello schermo, e un angolo li' dentro sembrerebbe un errore.
    if (st.layout.kind == LayoutKind::Docked || st.layout.kind == LayoutKind::DockedSide) {
        brush_->SetColor(pal.bg);
        rt_->FillRectangle(D2D1::RectF(0, 0, w, h), brush_.get());
    } else {
        const auto rr = D2D1::RoundedRect(
            D2D1::RectF(g.box.left + 0.5f, 0.5f, g.box.right - 0.5f, g.box.bottom - 0.5f),
            g.corner, g.corner);
        brush_->SetColor(pal.bg);
        rt_->FillRoundedRectangle(rr, brush_.get());
        if (st.layout.kind != LayoutKind::Tile) {
            brush_->SetColor(pal.border);
            rt_->DrawRoundedRectangle(rr, brush_.get(), 1.0f);
        }
    }

    // --- copertina ---
    brush_->SetColor(pal.artHole);
    DrawArt(g.art, g.artCorner, artFade);

    // --- testo ---
    const TitleSlot title = TitleFor(st, w, h);
    if (title.shown) {
        const UINT32 len   = static_cast<UINT32>(title.text.size());
        const float  avail = title.rect.right - title.rect.left;

        // La larghezza vera del titolo si misura solo quando può servire, cioè
        // in hover: a riposo il titolo lungo resta troncato con l'ellissi.
        const float wide = (st.marqueeMs >= 0.0) ? MeasureText(title.text, title.format) : 0.f;
        const float over = (wide > avail + 1.f) ? wide - avail : 0.f;

        brush_->SetColor(pal.title);
        if (over > 0.f) {
            const float offset = MarqueeOffset(st.marqueeMs, over);
            const auto  wideRect =
                D2D1::RectF(title.rect.left - offset, title.rect.top,
                            title.rect.left - offset + wide + 2.f, title.rect.bottom);

            // Maschera di opacità: il testo sfuma ai bordi invece di essere
            // tagliato di netto, e sfuma solo dal lato in cui continua davvero.
            com_ptr<ID2D1LinearGradientBrush> mask;
            if (avail > kMarqueeFade * 3.f) {
                const float f     = kMarqueeFade / avail;
                const float left  = Clamp(offset / kMarqueeFade, 0.f, 1.f);
                const float right = Clamp((over - offset) / kMarqueeFade, 0.f, 1.f);

                const D2D1_GRADIENT_STOP stops[] = {
                    {0.0f, D2D1::ColorF(D2D1::ColorF::White, 1.f - left)},
                    {f, D2D1::ColorF(D2D1::ColorF::White, 1.f)},
                    {1.f - f, D2D1::ColorF(D2D1::ColorF::White, 1.f)},
                    {1.0f, D2D1::ColorF(D2D1::ColorF::White, 1.f - right)},
                };
                com_ptr<ID2D1GradientStopCollection> ramp;
                if (SUCCEEDED(rt_->CreateGradientStopCollection(stops, 4, ramp.put()))) {
                    const float midY = (title.rect.top + title.rect.bottom) * 0.5f;
                    rt_->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(
                            D2D1::Point2F(title.rect.left, midY),
                            D2D1::Point2F(title.rect.right, midY)),
                        ramp.get(), mask.put());
                }
            }

            ID2D1Layer* layer = mask ? Layer() : nullptr;
            if (layer) {
                rt_->PushLayer(D2D1::LayerParameters(title.rect, nullptr,
                                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::IdentityMatrix(), 1.0f, mask.get(),
                                                     D2D1_LAYER_OPTIONS_NONE),
                               layer);
                rt_->DrawTextW(title.text.c_str(), len, title.format, wideRect, brush_.get());
                rt_->PopLayer();
            } else {
                rt_->PushAxisAlignedClip(title.rect, D2D1_ANTIALIAS_MODE_ALIASED);
                rt_->DrawTextW(title.text.c_str(), len, title.format, wideRect, brush_.get());
                rt_->PopAxisAlignedClip();
            }
        } else {
            rt_->DrawTextW(title.text.c_str(), len, title.format, title.rect, brush_.get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if (st.hasTrack && !st.artist.empty()) {
            IDWriteTextFormat* fa =
                (st.layout.kind == LayoutKind::Taskbar) ? fmtArtistSm_.get() : fmtArtist_.get();
            brush_->SetColor(pal.artist);
            const auto ar =
                D2D1::RectF(g.text.left, title.rect.bottom, g.text.right, g.text.bottom);
            rt_->DrawTextW(st.artist.c_str(), static_cast<UINT32>(st.artist.size()), fa, ar,
                           brush_.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    // --- progress bar / volume ---
    if (g.progress.right > g.progress.left + 8.f) {
        const float r  = (g.progress.bottom - g.progress.top) * 0.5f;
        const auto  tr = D2D1::RoundedRect(g.progress, r, r);
        brush_->SetColor(pal.track);
        rt_->FillRoundedRectangle(tr, brush_.get());

        // Subito dopo una rotellina la stessa striscia mostra per un attimo il
        // volume del player, in chiaro invece che in verde: nessun pannello che
        // compare, nessun pixel in più occupato.
        const bool showVol = st.volume >= 0.f;
        double     frac    = -1.0;
        if (showVol)
            frac = st.volume;
        else if (st.durSec > 0.5 && st.hasTrack)
            frac = st.posSec / st.durSec;

        if (frac >= 0.0) {
            if (frac > 1.0) frac = 1.0;
            const float end =
                g.progress.left + static_cast<float>((g.progress.right - g.progress.left) * frac);
            if (end > g.progress.left + 1.f) {
                const auto fr = D2D1::RoundedRect(
                    D2D1::RectF(g.progress.left, g.progress.top, end, g.progress.bottom), r, r);
                brush_->SetColor(showVol ? pal.vol : pal.fill);
                rt_->FillRoundedRectangle(fr, brush_.get());
            }
        }
    }

    // --- controlli ---
    if (g.showButtons) {
        // Nel riquadro i comandi stanno sopra la copertina: senza velatura
        // sarebbero illeggibili su copertine chiare.
        if (st.layout.kind == LayoutKind::Tile) {
            const auto rr = D2D1::RoundedRect(g.box, g.corner, g.corner);
            brush_->SetColor(pal.scrim);
            rt_->FillRoundedRectangle(rr, brush_.get());
        }

        const bool enabled[3] = {st.canPrev, st.canToggle, st.canNext};
        const int  areas[3]   = {static_cast<int>(HitArea::Prev),
                                 static_cast<int>(HitArea::PlayPause),
                                 static_cast<int>(HitArea::Next)};
        const float glyphScale =
            (st.layout.kind == LayoutKind::Tile) ? 0.90f
            : (st.layout.kind == LayoutKind::Taskbar) ? 0.85f
            : (st.layout.kind == LayoutKind::DockedSide) ? 0.85f
                                                      : 0.72f;

        auto triangle = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
            com_ptr<ID2D1PathGeometry> geo;
            if (FAILED(d2dFactory_->CreatePathGeometry(geo.put()))) return;
            com_ptr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(sink.put()))) return;
            sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x1, y1));
            sink->AddLine(D2D1::Point2F(x2, y2));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt_->FillGeometry(geo.get(), brush_.get());
        };

        for (int i = 0; i < 3; ++i) {
            const float cx = (g.buttons[i].left + g.buttons[i].right) * 0.5f;
            const float cy = (g.buttons[i].top + g.buttons[i].bottom) * 0.5f;
            const float s  = glyphScale;

            if (!enabled[i])
                brush_->SetColor(pal.glyphOff);
            else if (st.pressed == areas[i])
                brush_->SetColor(pal.glyphDown);
            else
                brush_->SetColor(pal.glyph);

            if (i == 0) {  // precedente
                rt_->FillRectangle(
                    D2D1::RectF(cx - 6.f * s, cy - 5.f * s, cx - 4.4f * s, cy + 5.f * s),
                    brush_.get());
                triangle(cx + 5.f * s, cy - 5.5f * s, cx + 5.f * s, cy + 5.5f * s, cx - 3.6f * s,
                         cy);
            } else if (i == 2) {  // successivo
                rt_->FillRectangle(
                    D2D1::RectF(cx + 4.4f * s, cy - 5.f * s, cx + 6.f * s, cy + 5.f * s),
                    brush_.get());
                triangle(cx - 5.f * s, cy - 5.5f * s, cx - 5.f * s, cy + 5.5f * s, cx + 3.6f * s,
                         cy);
            } else if (st.playing) {  // pausa
                rt_->FillRectangle(
                    D2D1::RectF(cx - 4.6f * s, cy - 6.f * s, cx - 1.6f * s, cy + 6.f * s),
                    brush_.get());
                rt_->FillRectangle(
                    D2D1::RectF(cx + 1.6f * s, cy - 6.f * s, cx + 4.6f * s, cy + 6.f * s),
                    brush_.get());
            } else {  // play
                triangle(cx - 4.f * s, cy - 6.5f * s, cx - 4.f * s, cy + 6.5f * s, cx + 6.f * s,
                         cy);
            }
        }
    }

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        CreateSurface();
        return;
    }
    Present(st.opacity);
}

void Renderer::Present(float opacity) {
    if (!surface_ || !dibBits_ || !memDC_ || !hwnd_) return;

    WICRect                 rc{0, 0, static_cast<INT>(widthPx_), static_cast<INT>(heightPx_)};
    com_ptr<IWICBitmapLock> lock;
    if (FAILED(surface_->Lock(&rc, WICBitmapLockRead, lock.put()))) return;

    UINT  stride = 0, size = 0;
    BYTE* pixels = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &pixels))) return;

    const UINT dibStride = widthPx_ * 4;
    if (stride == dibStride) {
        memcpy(dibBits_, pixels, static_cast<size_t>(dibStride) * heightPx_);
    } else {
        auto* dst = static_cast<BYTE*>(dibBits_);
        for (UINT y = 0; y < heightPx_; ++y)
            memcpy(dst + static_cast<size_t>(y) * dibStride,
                   pixels + static_cast<size_t>(y) * stride, dibStride);
    }
    lock = nullptr;

    if (opacity < 0.f) opacity = 0.f;
    if (opacity > 1.f) opacity = 1.f;

    POINT         src{0, 0};
    SIZE          dim{static_cast<LONG>(widthPx_), static_cast<LONG>(heightPx_)};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(opacity * 255.f + 0.5f), AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &dim, memDC_, &src, 0, &blend, ULW_ALPHA);
}

// ---------------------------------------------------------------- hit test --
HitArea Renderer::HitTest(POINT clientPx, const RenderState& st, float* frac) const {
    const float scale = 96.f / static_cast<float>(dpi_);
    const float x     = clientPx.x * scale;
    const float y     = clientPx.y * scale;

    const Geometry g = ComputeGeometry(st, widthPx_ * scale, heightPx_ * scale);
    if (x < g.box.left || x >= g.box.right) return HitArea::None;

    if (g.showButtons) {
        const HitArea areas[3] = {HitArea::Prev, HitArea::PlayPause, HitArea::Next};
        for (int i = 0; i < 3; ++i)
            if (Inside(g.buttons[i], x, y)) return areas[i];
    }

    // Fascia generosa attorno alla progress bar: 2,5 DIP di spessore reale sono
    // impossibili da colpire.
    if (x >= g.progress.left && x <= g.progress.right && y >= g.progress.top - 5.f &&
        y <= g.progress.bottom + 5.f) {
        if (frac && g.progress.right > g.progress.left)
            *frac = (x - g.progress.left) / (g.progress.right - g.progress.left);
        return HitArea::Progress;
    }

    return HitArea::Drag;
}
