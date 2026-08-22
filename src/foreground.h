// foreground.h — mantiene l'overlay sopra il gioco reagendo al cambio di
// finestra in primo piano. WINEVENT_OUTOFCONTEXT: nessuna iniezione.
#pragma once
#include "common.h"

class ForegroundWatch {
public:
    void Start(HWND target);
    void Stop();
    bool Active() const { return hook_ != nullptr; }

    // Vero se qualche finestra visibile e non "cloaked" sta sopra di noi.
    static bool IsCovered(HWND hwnd);
    static void AssertTopmost(HWND hwnd);

private:
    static void CALLBACK Proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    HWINEVENTHOOK hook_ = nullptr;
    static HWND   s_target;
};
