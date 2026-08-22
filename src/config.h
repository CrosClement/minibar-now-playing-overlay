// config.h — lettura/scrittura di minibar.ini accanto all'eseguibile.
#pragma once
#include "common.h"

enum class BarMode { Overlay, Docked, Taskbar };
enum class OverlayLayout { Bar, Tile };
enum class DockEdge { Left, Top, Right, Bottom };
enum class TaskbarAnchor { Auto, Left, Center, Right };
enum class Theme { Auto, Dark, Light };

struct Config {
    // [general]
    BarMode      mode         = BarMode::Overlay;
    std::wstring preferredApp = L"Spotify";
    Theme        theme        = Theme::Auto;  // auto = quello che dice Windows

    // [overlay]
    OverlayLayout overlayLayout = OverlayLayout::Bar;
    int           barWidth      = 266;  // DIP
    int           tileSize      = 92;   // DIP, lato del quadrato
    int           x             = INT_MIN;  // INT_MIN = posizione automatica
    int           y             = INT_MIN;
    int           idleOpacity   = 140;  // 0-255

    // [docked]
    DockEdge edge = DockEdge::Bottom;

    // [taskbar]
    TaskbarAnchor taskbarAnchor = TaskbarAnchor::Auto;

    // [player] — letta anche da [spotify], come si chiamava fino alla 1.0.
    bool         autoMinimize    = true;
    bool         hideInstead     = false;
    bool         launchIfMissing = false;
    bool         pauseOnSwitch   = true;  // il player che si lascia va in pausa
    bool         playOnSwitch    = true;  // ...e quello che si sceglie parte
    std::wstring launchPath;              // vuoto = percorsi tipici dedotti dal nome

    // [volume]
    int volumeStep = 4;  // punti percentuali per scatto di rotellina

    // [hotkey]
    bool hotkeyEnabled = false;
    UINT hotkeyMods    = MOD_ALT | MOD_SHIFT;
    UINT hotkeyVk      = 'M';

    void Load();
    void WriteDefaultsIfMissing();

    // Dimensione corrente del layout overlay attivo, in DIP.
    int  OverlaySize() const;
    void SaveOverlaySize(int dip);

    void SavePosition(int nx, int ny);
    void SavePreferredApp(const std::wstring& app);
    void SaveTheme(Theme t);
    void SavePauseOnSwitch(bool v);
    void SavePlayOnSwitch(bool v);
    void SaveIdleOpacity(int value);
    void SaveMode(BarMode m);
    void SaveOverlayLayout(OverlayLayout l);
    void SaveDockEdge(DockEdge e);
    void SaveAutoMinimize(bool v);

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
    void         EnsurePath();
};
