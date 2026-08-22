// appbar.h — modalità docked: striscia di schermo riservata via SHAppBarMessage.
// Non è una deskband (morte su Windows 11): è una finestra adiacente alla
// taskbar con spazio riservato, quindi le finestre massimizzate la rispettano.
#pragma once
#include "common.h"
#include "config.h"

class AppBar {
public:
    // reserveSpace=false registra la appbar solo per ricevere le notifiche
    // (taskbar spostata, auto-hide, app a schermo intero) senza sottrarre
    // spazio allo schermo: è quello che serve alla modalità taskbar.
    bool Register(HWND hwnd, bool reserveSpace = true);
    void Unregister();
    bool Registered() const { return registered_; }
    bool ReservesSpace() const { return reserves_; }

    // Ricalcola tutto da zero: su Windows 11 i rect memorizzati non sono
    // affidabili (taskbar XAML, monitor secondari con DPI diverso).
    void Reposition(DockEdge edge, int thicknessPx);

    // Notifiche ABN_*: taskbar spostata/nascosta, cambio risoluzione.
    void OnCallback(WPARAM notification);

private:
    HWND     hwnd_       = nullptr;
    bool     registered_ = false;
    bool     reserves_   = true;
    DockEdge edge_       = DockEdge::Bottom;
    int      thickness_  = 0;
};
