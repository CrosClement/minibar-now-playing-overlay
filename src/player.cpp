#include "player.h"

#include <appmodel.h>
#include <propsys.h>   // IPropertyStore
#include <psapi.h>
#include <shellapi.h>  // SHGetPropertyStoreForWindow (WIN32_LEAN_AND_MEAN lo esclude da windows.h)
#include <shlwapi.h>

#include <vector>

namespace {

// PKEY_AppUserModel_ID, dichiarata a mano invece di tirarsi dietro <initguid.h>
// e propkey.h, che definirebbero in questa unità di compilazione un mucchio di
// GUID che non ci servono.
const PROPERTYKEY kPkeyAppUserModelId = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

struct FindCtx {
    const std::wstring* key;
    bool                includeHidden;
    HWND                best;
    LONG                bestArea;
};

std::wstring StripExe(std::wstring s) {
    if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0) s.resize(s.size() - 4);
    return s;
}

// Nome dell'eseguibile senza estensione; vuoto se il processo non è leggibile.
std::wstring ExeStem(HANDLE proc) {
    wchar_t path[MAX_PATH]{};
    DWORD   len = MAX_PATH;
    if (!QueryFullProcessImageNameW(proc, 0, path, &len)) return {};
    return StripExe(PathFindFileNameW(path));
}

// AUMID di un'app impacchettata; vuoto per le app Win32 classiche.
std::wstring PackagedAumid(HANDLE proc) {
    UINT32 len = 0;
    if (GetApplicationUserModelId(proc, &len, nullptr) != ERROR_INSUFFICIENT_BUFFER || len == 0)
        return {};

    std::wstring id(len, L'\0');
    if (GetApplicationUserModelId(proc, &len, id.data()) != ERROR_SUCCESS) return {};
    id.resize(len > 0 ? len - 1 : 0);  // len include il terminatore
    return id;
}

// Il nome del processo e quello dichiarato dalla sessione SMTC quasi mai
// coincidono alla lettera ("Chrome" contro chrome.exe): basta che uno contenga
// l'altro, con una lunghezza minima che eviti collisioni casuali.
bool StemsMatch(const std::wstring& exeStem, const std::wstring& keyStem) {
    if (exeStem.empty() || keyStem.size() < 2) return false;
    if (ContainsNoCase(exeStem, keyStem)) return true;
    return exeStem.size() >= 4 && ContainsNoCase(keyStem, exeStem);
}

std::wstring ProcessStem(DWORD pid) {
    if (!pid) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    const std::wstring stem = ExeStem(h);
    CloseHandle(h);
    return stem;
}

std::wstring ProcessPath(DWORD pid) {
    if (!pid) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};

    wchar_t     path[MAX_PATH]{};
    DWORD       len = MAX_PATH;
    const BOOL  ok  = QueryFullProcessImageNameW(h, 0, path, &len);
    CloseHandle(h);
    return ok ? std::wstring(path, len) : std::wstring{};
}

bool ProcessAlive(DWORD pid) {
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD      code = 0;
    const BOOL ok   = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
}

// Nome leggibile dichiarato dall'eseguibile stesso: "Firefox", "Google Chrome",
// "Spotify". È quello che Windows mostra in Gestione attività.
std::wstring FileDescription(const std::wstring& exePath) {
    if (exePath.empty()) return {};

    DWORD       ignored = 0;
    const DWORD size    = GetFileVersionInfoSizeW(exePath.c_str(), &ignored);
    if (!size) return {};

    std::vector<uint8_t> info(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, info.data())) return {};

    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* langs = nullptr;
    UINT         bytes = 0;
    if (!VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&langs), &bytes) ||
        bytes < sizeof(Translation))
        return {};

    wchar_t query[64]{};
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].language,
               langs[0].codePage);

    wchar_t* text = nullptr;
    UINT     len  = 0;
    if (!VerQueryValueW(info.data(), query, reinterpret_cast<void**>(&text), &len) || !text ||
        !len)
        return {};

    std::wstring out(text, len);
    while (!out.empty() && (out.back() == L'\0' || out.back() == L' ')) out.pop_back();
    return out;
}

// AUMID dichiarato da una finestra. Firefox e le PWA registrano le SMTC con una
// stringa opaca (Firefox usa un hash esadecimale del percorso d'installazione):
// quella stringa non somiglia a nessun nome di processo, ma è esattamente la
// stessa che la finestra espone qui.
std::wstring WindowAppId(HWND hwnd) {
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) || !store) return {};

    PROPVARIANT value;
    PropVariantInit(&value);

    std::wstring id;
    if (SUCCEEDED(store->GetValue(kPkeyAppUserModelId, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal)
        id = value.pwszVal;

    PropVariantClear(&value);
    store->Release();
    return id;
}

struct ResolveCtx {
    const std::wstring* key;
    bool                visibleOnly;
    DWORD               pid;
};

BOOL CALLBACK ResolveProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<ResolveCtx*>(lp);
    if (ctx->visibleOnly && !IsWindowVisible(hwnd)) return TRUE;

    // Chiedere l'AUMID a una finestra interroga il thread che la possiede: su
    // una finestra bloccata la chiamata aspetta il timeout, e a rimetterci
    // sarebbe il nostro thread UI. Le finestre piantate si saltano.
    if (IsHungAppWindow(hwnd)) return TRUE;

    const std::wstring id = WindowAppId(hwnd);
    if (id.empty() || !EqualsNoCase(id, *ctx->key)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;

    ctx->pid = pid;
    return FALSE;
}

// Risoluzione chiave SMTC → processo, con cache: chiedere l'AUMID a ogni
// finestra del desktop costa una chiamata COM per finestra, e non è roba da
// fare a ogni scatto di rotellina. Si rifà solo se il processo trovato muore, e
// i buchi (nessuna finestra con quella chiave) si ripetono al massimo ogni due
// secondi. Tutto su thread UI, come il resto del modulo.
struct Resolved {
    std::wstring key;
    std::wstring stem;   // nome dell'eseguibile, vuoto se non risolta
    std::wstring name;   // FileDescription, se l'eseguibile la dichiara
    DWORD        pid   = 0;
    ULONGLONG    stamp = 0;
};

std::vector<Resolved> g_resolved;
constexpr ULONGLONG   kMissRetryMs = 2000;

void Fill(Resolved& entry, const std::wstring& appKey) {
    ResolveCtx ctx{&appKey, true, 0};
    EnumWindows(ResolveProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.pid) {
        ctx.visibleOnly = false;  // il player potrebbe essere nascosto proprio da noi
        EnumWindows(ResolveProc, reinterpret_cast<LPARAM>(&ctx));
    }

    entry.key   = appKey;
    entry.pid   = ctx.pid;
    entry.stem  = ProcessStem(ctx.pid);
    entry.name  = FileDescription(ProcessPath(ctx.pid));
    entry.stamp = GetTickCount64();
}

const Resolved& ResolveKey(const std::wstring& appKey) {
    static const Resolved kNone;
    if (appKey.empty()) return kNone;

    for (auto& entry : g_resolved) {
        if (!EqualsNoCase(entry.key, appKey)) continue;

        const bool fresh = entry.pid ? ProcessAlive(entry.pid)
                                     : (GetTickCount64() - entry.stamp < kMissRetryMs);
        if (!fresh) Fill(entry, appKey);
        return entry;
    }

    if (g_resolved.size() >= 8) g_resolved.erase(g_resolved.begin());
    g_resolved.emplace_back();
    Fill(g_resolved.back(), appKey);
    return g_resolved.back();
}

// Il nome da cui dedurre i percorsi tipici quando si deve avviare il player.
// Non è il nome da mostrare: quello lo decide FriendlyName.
std::wstring KeyStem(const std::wstring& appKey) {
    if (appKey.empty()) return {};

    std::wstring name = appKey;
    const size_t bang = name.find_last_of(L'!');
    if (bang != std::wstring::npos) name = name.substr(bang + 1);
    const size_t dot = name.find_last_of(L'.');
    if (bang != std::wstring::npos && dot != std::wstring::npos) name = name.substr(dot + 1);

    name = StripExe(name);
    if (name.empty()) return appKey;

    name[0] = static_cast<wchar_t>(towupper(name[0]));
    return name;
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);

    // I player basati su Chromium creano molte top-level invisibili con la
    // stessa classe: la classe non è un criterio utile. Filtriamo su processo +
    // stili + rect + titolo.
    if (!ctx->includeHidden && !IsWindowVisible(hwnd)) return TRUE;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const LONG w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return TRUE;

    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!PlayerManager::ProcessMatches(pid, *ctx->key)) return TRUE;

    const LONG area = w * h;
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->best     = hwnd;
    }
    return TRUE;
}

void WriteMarker(const std::wstring& path, const std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

std::wstring ReadMarker(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};

    wchar_t buf[256]{};
    DWORD   read = 0;
    ReadFile(f, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr);
    CloseHandle(f);
    return std::wstring(buf, read / sizeof(wchar_t));
}

}  // namespace

// ------------------------------------------------------------------ identità --
bool PlayerManager::ProcessMatches(DWORD pid, const std::wstring& appKey) {
    if (!pid || appKey.empty()) return false;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    const bool         packaged = appKey.find(L'!') != std::wstring::npos;
    const std::wstring stem     = ExeStem(h);
    const std::wstring aumid    = packaged ? PackagedAumid(h) : std::wstring{};
    CloseHandle(h);

    if (packaged) {
        if (!aumid.empty()) return _wcsicmp(aumid.c_str(), appKey.c_str()) == 0;
        // AUMID non leggibile: il nome del pacchetto contiene quasi sempre
        // quello dell'eseguibile ("SpotifyAB.SpotifyMusic_…!Spotify").
        return stem.size() >= 4 && ContainsNoCase(appKey, stem);
    }

    if (StemsMatch(stem, StripExe(appKey))) return true;

    // Chiave opaca: non somiglia a nessun nome di processo perché non è un
    // nome. Si scopre chi è chiedendo alle finestre quale AUMID dichiarano, e
    // poi si confronta col nome dell'eseguibile trovato — così prende anche i
    // processi figli, che è dove i browser mettono l'audio.
    const Resolved& resolved = ResolveKey(appKey);
    return !resolved.stem.empty() && EqualsNoCase(stem, resolved.stem);
}

std::wstring PlayerManager::FriendlyName(const std::wstring& appKey) {
    if (appKey.empty()) return {};

    // Se la chiave si risolve a un processo, il nome buono lo dichiara
    // l'eseguibile stesso: "Firefox", "Google Chrome", "Spotify". Vale anche
    // per le chiavi leggibili, dove "chrome.exe" diventa "Google Chrome".
    const Resolved& resolved = ResolveKey(appKey);
    if (!resolved.name.empty()) return resolved.name;
    if (!resolved.stem.empty()) return KeyStem(resolved.stem);

    // Nessuna finestra con quella chiave: si fa quel che si può con la stringa.
    return KeyStem(appKey);
}

// ------------------------------------------------------------------ finestra --
HWND PlayerManager::FindMainWindow(const std::wstring& appKey, bool includeHidden) {
    if (appKey.empty()) return nullptr;

    // Si risolve prima la chiave, così ProcessMatches trova la cache già calda
    // e non parte un'enumerazione di finestre dentro un'altra.
    ResolveKey(appKey);

    FindCtx ctx{&appKey, includeHidden, nullptr, 0};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool PlayerManager::IsRunning(const std::wstring& appKey) {
    return FindMainWindow(appKey, true) != nullptr;
}

std::wstring PlayerManager::MarkerPath() { return ExeDir() + L"minibar-hidden.flag"; }

void PlayerManager::Apply(const std::wstring& appKey, bool hideInstead) {
    HWND w = FindMainWindow(appKey, false);
    if (!w) return;

    if (hideInstead) {
        // Una finestra nascosta non è recuperabile da taskbar/Alt-Tab: lasciamo
        // un marker su disco perché il prossimo avvio sappia chi recuperare.
        WriteMarker(MarkerPath(), appKey);
        ShowWindow(w, SW_HIDE);
        hid_       = true;
        minimized_ = false;
    } else {
        ShowWindow(w, SW_MINIMIZE);
        minimized_ = true;
        hid_       = false;
    }
    applied_ = appKey;
}

void PlayerManager::Restore() {
    if (!hid_ && !minimized_) return;

    HWND w = FindMainWindow(applied_, true);
    if (w) {
        if (hid_) ShowWindow(w, SW_SHOW);
        ShowWindow(w, SW_RESTORE);
    }
    DeleteFileW(MarkerPath().c_str());
    hid_       = false;
    minimized_ = false;
    applied_.clear();
}

void PlayerManager::RecoverOrphan() {
    // Il marker delle versioni "solo Spotify" non conteneva il nome del player.
    const std::wstring legacy = ExeDir() + L"minibar-spotify-hidden.flag";

    struct Candidate {
        std::wstring path;
        std::wstring fallbackKey;
    } const candidates[] = {{MarkerPath(), {}}, {legacy, L"Spotify"}};

    for (const auto& c : candidates) {
        if (!PathFileExistsW(c.path.c_str())) continue;

        std::wstring key = ReadMarker(c.path);
        if (key.empty()) key = c.fallbackKey;

        HWND w = FindMainWindow(key, true);
        if (w && !IsWindowVisible(w)) {
            ShowWindow(w, SW_SHOW);
            ShowWindow(w, SW_RESTORE);
        }
        DeleteFileW(c.path.c_str());
    }
}

bool PlayerManager::Launch(const std::wstring& appKey, const std::wstring& explicitPath) {
    std::vector<std::wstring> candidates;
    if (!explicitPath.empty()) candidates.push_back(explicitPath);

    // Percorsi tipici: <root>\<Nome>\<Nome>.exe copre le installazioni
    // per-utente (Spotify, Tidal) e per-macchina della maggior parte dei player.
    // Qui serve il nome del file, non quello leggibile: "Google Chrome" non è
    // una cartella, "Chrome" sì.
    const std::wstring stem = KeyStem(appKey);
    if (!stem.empty()) {
        for (const wchar_t* var :
             {L"APPDATA", L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)"}) {
            wchar_t root[MAX_PATH]{};
            if (GetEnvironmentVariableW(var, root, MAX_PATH))
                candidates.push_back(std::wstring(root) + L"\\" + stem + L"\\" + stem + L".exe");
        }
    }

    for (const auto& exe : candidates) {
        if (!PathFileExistsW(exe.c_str())) continue;

        std::wstring cmd = L"\"" + exe + L"\"";
        if (ContainsNoCase(exe, L"spotify.exe")) cmd += L" --minimized";  // lo capisce solo lui

        STARTUPINFOW         si{sizeof(si)};
        PROCESS_INFORMATION  pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        if (CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    }
    return false;
}
