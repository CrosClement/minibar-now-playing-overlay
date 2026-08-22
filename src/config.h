// config.h — lettura/scrittura di minibar.ini accanto all'eseguibile.
#pragma once
#include "common.h"

enum class BarMode { Overlay, Docked, Taskbar };
enum class OverlayLayout { Bar, Tile };
enum class DockEdge { Left, Top, Right, Bottom };
enum class TaskbarAnchor { Auto, Left, Center, Right };

struct Config {
    // [general]
    BarMode      mode         = BarMode::Overlay;
    std::wstring preferredApp = L"Spotify";

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

    // [spotify]
    bool autoMinimize    = true;
    bool hideInstead     = false;
    bool launchIfMissing = false;

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
    void SaveMode(BarMode m);
    void SaveOverlayLayout(OverlayLayout l);
    void SaveAutoMinimize(bool v);

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
    void         EnsurePath();
};
