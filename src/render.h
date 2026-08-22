// render.h — Direct2D software su bitmap WIC, presentata con UpdateLayeredWindow.
//
// Il piano (§6.3) dà due strade: swapchain di composizione + DirectComposition,
// oppure WS_EX_LAYERED + alpha per-pixel. La prima è stata misurata a ~37 MB di
// memoria privata e 27 thread (device D3D11 + driver): fuori dal budget della
// §3. Per una superficie piccola ridisegnata al massimo una volta al secondo la
// rasterizzazione software costa frazioni di millisecondo, non tocca la GPU e
// sta in pochi MB. Nessun render loop: si disegna solo su richiesta di App.
#pragma once
#include "common.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <winrt/base.h>

enum class LayoutKind {
    Bar,      // barretta orizzontale: copertina, testo, progress, controlli in hover
    Docked,   // striscia a tutta larghezza con contenuto centrato
    Tile,     // quadrato: solo copertina, controlli in hover sopra una velatura
    Taskbar,  // pillola dentro la taskbar: copertina + controlli, testo in hover
};

struct LayoutSpec {
    LayoutKind kind      = LayoutKind::Bar;
    float      widthDip  = 380.f;  // per Tile è il lato del quadrato
    float      heightDip = 64.f;   // per Taskbar la detta la taskbar
    float      expand    = 0.f;    // Taskbar: 0 pillola chiusa, 1 aperta sul testo
};

// Ciò che il renderer deve mostrare. Nessuna logica, solo dati.
struct RenderState {
    LayoutSpec   layout;
    std::wstring title;
    std::wstring artist;
    bool         playing   = false;
    bool         hovered   = false;
    bool         canPrev   = false;
    bool         canNext   = false;
    bool         canToggle = false;
    bool         hasTrack  = false;
    bool         lightTaskbar = false;  // taskbar chiara: sopra ci vuole roba scura
    bool         lightTheme   = false;  // tema app chiaro: la barretta si schiarisce
    double       posSec    = 0.0;
    double       durSec    = 0.0;
    float        opacity   = 1.0f;
    int          pressed   = 0;     // HitArea premuta, per il feedback visivo
    float        volume    = -1.f;  // 0-1 mentre lampeggia il volume, <0 = non mostrarlo

    // Millisecondi da quando è cominciato lo scorrimento del titolo; < 0 = fermo.
    // Scorre solo in hover: a riposo non esiste nessun timer.
    double marqueeMs = -1.0;

    // Avanzamento della dissolvenza fra la copertina vecchia e la nuova:
    // 0 = ancora tutta vecchia, 1 = transizione finita.
    float artFade = 1.f;
};

enum class HitArea { None = 0, Drag, Prev, PlayPause, Next, Progress };

// Tinta dominante della copertina, in HSV: il colore vero e proprio si ricava
// al momento del disegno, perché quanto dev'essere chiaro dipende dal fondo su
// cui finisce. `valid` è falso per le copertine senza un colore vero (bianco e
// nero, scale di grigi): in quel caso si resta sul verde di sempre.
struct ArtAccent {
    float h     = 0.f;  // 0-360
    float s     = 0.f;  // 0-1
    float v     = 0.f;  // 0-1
    bool  valid = false;
};

class Renderer {
public:
    bool Init(HWND hwnd, UINT dpi);
    void Shutdown();
    bool Valid() const { return rt_ != nullptr; }

    void Resize(UINT widthPx, UINT heightPx, UINT dpi);
    void SetThumbnail(const std::vector<uint8_t>& bytes);  // vuoto = placeholder
    void Draw(const RenderState& state);

    // clientPx in pixel client; frac riceve la posizione 0-1 sulla progress bar.
    HitArea HitTest(POINT clientPx, const RenderState& state, float* frac = nullptr) const;

    // Quanto sborda il titolo dallo spazio che ha, in DIP: 0 se ci sta tutto.
    // È l'unica cosa che App deve sapere per decidere se far scorrere il testo.
    float TitleOverflow(const RenderState& state) const;

    // Dimensione in pixel richiesta da un layout a un certo DPI.
    static SIZE Measure(const LayoutSpec& spec, UINT dpi);

    // Tinta dominante della copertina in cache; valida dopo il primo Draw che
    // la decodifica.
    const ArtAccent& Accent() const { return accent_; }

private:
    struct TitleSlot {
        std::wstring       text;
        D2D1_RECT_F        rect{};
        IDWriteTextFormat* format = nullptr;
        bool               shown  = false;
    };

    bool CreateSurface();
    void ReleaseSurface();
    void DecodeThumbnail();
    void ComputeAccent(IWICBitmapSource* source);
    void Present(float opacity);
    void DrawArt(const D2D1_RECT_F& rect, float radius, float fade);

    // Testo, riquadro e font del titolo, in DIP: li devono vedere uguali il
    // disegno e la misura dello sbordamento.
    TitleSlot   TitleFor(const RenderState& state, float w, float h) const;
    float       MeasureText(const std::wstring& text, IDWriteTextFormat* format) const;
    ID2D1Layer* Layer();  // layer riusabile: ritagli e maschere di opacità

    HWND hwnd_    = nullptr;
    UINT dpi_     = 96;
    UINT widthPx_ = 0, heightPx_ = 0;
    UINT artSidePx_ = 0;  // lato a cui è stata scalata la copertina in cache

    winrt::com_ptr<ID2D1Factory1>      d2dFactory_;
    winrt::com_ptr<IDWriteFactory>     dwrite_;
    winrt::com_ptr<IWICImagingFactory> wic_;

    winrt::com_ptr<IWICBitmap>           surface_;
    winrt::com_ptr<ID2D1RenderTarget>    rt_;
    winrt::com_ptr<ID2D1SolidColorBrush> brush_;
    winrt::com_ptr<ID2D1Layer>           layer_;

    // Destinazione GDI per UpdateLayeredWindow.
    HDC     memDC_   = nullptr;
    HBITMAP dib_     = nullptr;
    HGDIOBJ oldBmp_  = nullptr;
    void*   dibBits_ = nullptr;

    winrt::com_ptr<IDWriteTextFormat> fmtTitle_;
    winrt::com_ptr<IDWriteTextFormat> fmtArtist_;
    winrt::com_ptr<IDWriteTextFormat> fmtTitleSm_;   // taskbar
    winrt::com_ptr<IDWriteTextFormat> fmtArtistSm_;  // taskbar

    winrt::com_ptr<ID2D1Bitmap> art_;
    std::vector<uint8_t>        artBytes_;
    ArtAccent                   accent_;
    bool                        accentDone_ = false;  // calcolato una volta per copertina

    // La copertina di prima resta in vita quel tanto che basta a sfumare sotto
    // quella nuova, e con lei la tinta da cui il colore parte.
    winrt::com_ptr<ID2D1Bitmap> artPrev_;
    ArtAccent                   accentPrev_;
};
