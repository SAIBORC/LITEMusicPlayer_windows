// LITE Music Player — C++ Win32 (zero dependencies)
#define _CRT_SECURE_NO_WARNINGS
#define GDIPVER 0x0110
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mmdevapi.lib")

#pragma comment(lib, "runtimeobject.lib")

#ifndef APPCOMMAND_MEDIA_NEXT
#define APPCOMMAND_MEDIA_NEXT 47
#define APPCOMMAND_MEDIA_PREV 48
#endif

#include <windows.h>
#include <windowsx.h>
#include <roapi.h>
#include <winstring.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cmath>

#include <shlwapi.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

#include "resource.h"
#include "stb_vorbis.h"

// ---------------------------------------------------------------------------
// Acrylic blur for title bar
// ---------------------------------------------------------------------------
struct ACCENTPOLICY { DWORD state; DWORD flags; DWORD color; DWORD animId; };
struct WINCOMPATTR { DWORD attrib; void *data; ULONG dataSize; };
typedef BOOL(WINAPI *WCAFunc)(HWND, WINCOMPATTR*);
static WCAFunc s_wca = NULL;
static void InitWCA() {
    if (!s_wca) {
        HMODULE h = GetModuleHandleW(L"user32.dll");
        if (h) s_wca = (WCAFunc)GetProcAddress(h, "SetWindowCompositionAttribute");
    }
}
static void SetAcrylicBlur(HWND hw, int intensity) {
    InitWCA();
    if (!s_wca) return;
    ACCENTPOLICY ap = {};
    WINCOMPATTR wc = {19, &ap, sizeof(ap)};
    if (intensity > 0) {
        ap.state = 4;
        int a = intensity * 180 / 100;
        if (a > 180) a = 180;
        // Blend base color from light (glass) to dark (blur)
        int c = 0xE0 - (intensity * 0xC8 / 100);
        ap.color = (DWORD)((a << 24) | (c << 16) | (c << 8) | c);
    }
    s_wca(hw, &wc);
}

// ---------------------------------------------------------------------------
// Modern dark color palette
// ---------------------------------------------------------------------------
static COLORREF       COL_BG          = RGB(0x12,0x12,0x12);
static COLORREF       COL_BG_DARK     = RGB(0x1A,0x1A,0x1A);
static COLORREF       COL_TITLE_BG    = RGB(0x12,0x12,0x12);
static COLORREF       COL_TITLE_TEXT  = RGB(0xFF,0xFF,0xFF);
static COLORREF       COL_TEXT        = RGB(0xE0,0xE0,0xE0);
static COLORREF       COL_TEXT_DIM    = RGB(0x66,0x66,0x66);
static COLORREF       g_accentColor   = RGB(0x1D,0xB9,0x54);
static COLORREF       COL_SLIDER_BG   = RGB(0x33,0x33,0x33);
static COLORREF       g_sliderFill    = RGB(0x1D,0xB9,0x54);
static COLORREF       COL_SLIDER_THUMB= RGB(0xEE,0xEE,0xEE);
static COLORREF       COL_LIST_BG     = RGB(0x0D,0x0D,0x0D);
static COLORREF       COL_ART_BG      = RGB(0x1E,0x1E,0x1E);
static int            g_windowAlpha   = 255;

static int            g_rainbowTick   = 0;
static bool           g_rainbowEnabled = true;
static bool           g_acrylicBlur    = true;
static int            g_blurIntensity  = 50;
static bool           g_blurSliding    = false;
static RECT           g_blurSliderRect = {};
static int            g_manualEvent    = -1;
static bool           g_useLightTheme = false;
static RECT           g_cacheBtnRect = {};
static RECT           g_loadAllBtnRect = {};

// Audio output device selection (empty = default system device)
static std::vector<std::wstring> g_audioDeviceNames; // display names for dropdown
static std::vector<std::wstring> g_audioDeviceIds;   // persistent device IDs for routing
static std::wstring               g_audioDeviceId;   // stored ID (empty = default)
static RECT                      g_audioDdBtn       = {};

static const COLORREF g_accentColors[] = {
    RGB(0x1D,0xB9,0x54), // Emerald
    RGB(0xFF,0x57,0x22), // Deep Orange
    RGB(0x21,0x96,0xF3), // Blue
    RGB(0x9C,0x27,0xB0), // Purple
    RGB(0xFF,0xC1,0x07), // Amber
    RGB(0xE9,0x1E,0x63), // Pink
    RGB(0x00,0xBC,0xD4), // Cyan
    RGB(0xFF,0xFF,0xFF), // White
    RGB(0x4C,0xAF,0x50), // Green
    RGB(0xFF,0x98,0x00), // Orange
    RGB(0x3F,0x51,0xB5), // Indigo
    RGB(0x79,0x55,0x48), // Brown
    RGB(0x60,0x7D,0x8B), // Blue Grey
    RGB(0xCD,0xDC,0x39), // Lime
    RGB(0x00,0x96,0x88), // Teal
    RGB(0xF4,0x43,0x36), // Red
};
static const int g_accentColorCount = sizeof(g_accentColors) / sizeof(g_accentColors[0]);
static COLORREF g_custColors[16] = {};
static RECT g_customBtnRect = {};

// ---------------------------------------------------------------------------
// SMTC (SystemMediaTransportControls) COM interface definitions
// ---------------------------------------------------------------------------

// {ddb0472d-c911-4a1f-86d9-dc3d71a95f5a}
MIDL_INTERFACE("ddb0472d-c911-4a1f-86d9-dc3d71a95f5a")
ISystemMediaTransportControlsInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND appWindow, REFIID riid, void **mediaTransportControl) = 0;
};

// {99FA3FF4-1742-42A6-902E-087D41F965EC}
// NOTE: ISystemMediaTransportControls extends IInspectable in WinRT.
// We add the 3 IInspectable vtable slots as placeholders to keep the vtable aligned.
MIDL_INTERFACE("99FA3FF4-1742-42A6-902E-087D41F965EC")
ISystemMediaTransportControls : public IUnknown {
    // IInspectable placeholders (3 methods)
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    // Actual ISystemMediaTransportControls methods
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(struct EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(struct EventRegistrationToken) = 0;
};

// {EA98D2F6-7F3C-4AF2-A586-72889808EFB1}
MIDL_INTERFACE("EA98D2F6-7F3C-4AF2-A586-72889808EFB1")
ISystemMediaTransportControls2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoRepeatMode(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShuffleEnabled(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShuffleEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(double*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackRate(double) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTimelineProperties(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackPositionChangeRequested(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackPositionChangeRequested(struct EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackRateChangeRequested(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackRateChangeRequested(struct EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ShuffleEnabledChangeRequested(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ShuffleEnabledChangeRequested(struct EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AutoRepeatModeChangeRequested(IUnknown*, struct EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AutoRepeatModeChangeRequested(struct EventRegistrationToken) = 0;
};

// {B7F47116-A56F-4DC8-9E11-92031F4A87C2}
MIDL_INTERFACE("B7F47116-A56F-4DC8-9E11-92031F4A87C2")
ISystemMediaTransportControlsButtonPressedEventArgs : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Button(int *value) = 0;
};

// {6BBF0C59-D0A0-4D26-92A0-F978E1D18E7B}
MIDL_INTERFACE("6BBF0C59-D0A0-4D26-92A0-F978E1D18E7B")
IMusicDisplayProperties : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumTitle(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(unsigned int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TrackNumber(unsigned int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(void**) = 0;
};

// {8ABBC53E-FA55-4ECF-AD8E-C984E5DD1550}
MIDL_INTERFACE("8ABBC53E-FA55-4ECF-AD8E-C984E5DD1550")
ISystemMediaTransportControlsDisplayUpdater : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Type(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(int, void*, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update() = 0;
};

// {5125316A-C3A2-475B-8507-93534DC88F15}
MIDL_INTERFACE("5125316A-C3A2-475B-8507-93534DC88F15")
ISystemMediaTransportControlsTimelineProperties : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(__int64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StartTime(__int64) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(__int64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_EndTime(__int64) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(__int64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinSeekTime(__int64) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(__int64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MaxSeekTime(__int64) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(__int64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Position(__int64) = 0;
};

// SMTC button enum
enum SMTCButton {
    SMTC_Play     = 0,
    SMTC_Pause    = 1,
    SMTC_Stop     = 2,
    SMTC_Next     = 3,
    SMTC_Previous = 4,
    SMTC_FastForward = 5,
    SMTC_Rewind   = 6,
};

// PlaybackStatus enum
enum SMTCPlaybackStatus {
    SMTC_Closed   = 0,
    SMTC_Changing = 1,
    SMTC_Stopped  = 2,
    SMTC_Playing  = 3,
    SMTC_Paused   = 4,
};

// MediaPlaybackType enum
enum SMTCMediaPlaybackType {
    SMTC_Unknown = 0,
    SMTC_Music   = 1,
    SMTC_Video   = 2,
    SMTC_Image   = 3,
};

// EventRegistrationToken for WinRT events
struct EventRegistrationToken { __int64 value; };

// Registry key for saving settings
static const wchar_t *REG_KEY = L"Software\\LITEMusicPlayer";

static const int TITLE_H    = 28;
static const int BOTTOM_H   = 72;
static const int SCROLL_W   = 8;
static const int ART_SZ     = 180;
static const int PADDING    = 12;

enum BtnID {
    BTN_NONE=0,
    BTN_SHUFFLE, BTN_STOP, BTN_PREV, BTN_PLAY, BTN_NEXT,
    BTN_SETTINGS, BTN_LOCATE, BTN_VOLUME,
    BTN_SEARCH, BTN_EQ, BTN_WIDGET, BTN_MINIMIZE, BTN_MAXIMIZE, BTN_CLOSE,
    BTN_LAST
};

static HINSTANCE    g_hInst       = NULL;
static HWND         g_hWnd        = NULL;
static ULONG_PTR    g_gdiplusToken= 0;
static HFONT        g_hFont       = NULL;
static HFONT        g_hFontBold   = NULL;
static RECT         g_rcClient    = {0,0,700,520};

// Audio state
static bool g_playing  = false;
static bool g_paused   = false;
static bool g_userStop = false;
static int  g_volume   = 500;
static bool g_muted    = false;
static int  g_prevVolume = 500;
static IMFSourceReader *g_pReader = NULL;
static IAudioClient *g_pAudioClient = NULL;
static IAudioRenderClient *g_pRenderClient = NULL;
static HANDLE g_hAudioThread = NULL;
static volatile bool g_audioRun = false;
static volatile bool g_audioPaused = false;
static UINT32 g_audioBufFrames = 0;
static UINT32 g_audioFrameSize = 0;

// Playlist
static std::vector<std::wstring> g_playlist;
static int  g_trackIdx   = -1;
static std::wstring g_folderName;
static std::wstring g_songTitle;
static std::wstring g_artistName;
static bool g_hasFolder    = false;

static HWND g_hSettingsWnd = NULL;
static std::wstring g_searchBuf;
static DWORD g_searchTick = 0;
static bool g_compactMode = false;
static RECT g_normalRect = {};

// Album art and thumbnail cache
static Gdiplus::Image *g_albumArt = NULL;
static std::vector<Gdiplus::Image*> g_thumbs;
static int g_browserScroll = 0;
static std::vector<int> g_filteredIndices;
static RECT g_searchBox = {};
static bool g_searchFocused = false;

// Button tracking
static BtnID g_hoverBtn = BTN_NONE;
static BtnID g_pressBtn = BTN_NONE;

// Window drag/resize
static bool  g_dragging = false;
static bool  g_resizing = false;
static POINT g_dragPt   = {};
static SIZE  g_resizeSz = {};
static const int RESIZE_BORDER = 8;

// Seek / volume / scroll drag
static bool g_dragSeek = false;
static int  g_volDragX = -1;
static int  g_opacityDragX = -1;

static bool g_bDragScroll = false;
static double g_curSec = 0;
static int  g_maxSec = 0;
static POINT g_mousePt = {};

// Button rects
static RECT g_btnRects[BTN_LAST];

static ISimpleAudioVolume *g_pSimpleVol = NULL;
static int g_repeatMode = 0; // 0=normal, 1=repeat once, 2=repeat all
static bool g_repeatOnce = false; // track one extra play in mode 1

// Equalizer
#define EQ_NUM_BANDS 10
static const float g_eqFreqs[EQ_NUM_BANDS] = {31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static float g_eqGains[EQ_NUM_BANDS] = {};
static bool g_eqEnabled = false;
static HWND g_hEqWnd = NULL;
struct BiquadState { float x1,x2,y1,y2; };
static BiquadState g_eqState[2][EQ_NUM_BANDS]; // per channel, per band
static float g_eqCoeffs[EQ_NUM_BANDS][5]; // b0,b1,b2,a1,a2
static int g_eqSampleRate = 0;
static void CalcEQCoeffs(int sr);

// Tray icon
#define WM_TRAYICON (WM_APP+4)
static NOTIFYICONDATAW g_nid = {};
static bool g_trayIcon = false;
static HICON g_trayIconHandle = NULL;

static HICON CreatePlayerIcon(bool dark) {
    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    int s = 16;
    HBITMAP xorBmp = CreateCompatibleBitmap(hdc, s, s);
    HBITMAP andBmp = CreateCompatibleBitmap(hdc, s, s);
    COLORREF fg = dark ? RGB(255,255,255) : RGB(0,0,0);
    // Draw XOR bitmap (the actual image)
    SelectObject(memDC, xorBmp);
    HBRUSH hb = CreateSolidBrush(fg);
    HPEN hp = CreatePen(PS_SOLID, 1, fg);
    HPEN oldPen = (HPEN)SelectObject(memDC, hp);
    // Play triangle
    POINT pts[3] = {{s/4, s/6}, {s/4, s*5/6}, {s*3/4, s/2}};
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Polygon(memDC, pts, 3);
    // Pause bars
    int bx1 = s*5/8, by1 = s/4, bw = s/10, bh = s/2;
    RECT bar1 = {bx1, by1, bx1+bw, by1+bh};
    RECT bar2 = {bx1+bw+s/12, by1, bx1+bw*2+s/12, by1+bh};
    FillRect(memDC, &bar1, hb); FillRect(memDC, &bar2, hb);
    SelectObject(memDC, oldPen); DeleteObject(hp); DeleteObject(hb);
    // Draw AND mask (0=opaque, 1=transparent)
    SelectObject(memDC, andBmp);
    HBRUSH hw = CreateSolidBrush(RGB(255,255,255));
    HBRUSH hk = CreateSolidBrush(RGB(0,0,0));
    HBRUSH oldBr = (HBRUSH)SelectObject(memDC, hw);
    PatBlt(memDC, 0, 0, s, s, PATCOPY); // fill white (transparent)
    SelectObject(memDC, hk);
    // Black out the icon area to make it opaque
    POINT apts[3] = {{s/4, s/6}, {s/4, s*5/6}, {s*3/4, s/2}};
    Polygon(memDC, apts, 3);
    RECT b1 = {s*5/8, s/4, s*5/8+s/10, s*3/4};
    RECT b2 = {s*5/8+s/10+s/12, s/4, s*5/8+s/10*2+s/12, s*3/4};
    FillRect(memDC, &b1, hk); FillRect(memDC, &b2, hk);
    SelectObject(memDC, oldBr); DeleteObject(hw); DeleteObject(hk);
    DeleteDC(memDC); ReleaseDC(NULL, hdc);
    ICONINFO ii = {TRUE, 0, 0, andBmp, xorBmp};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(xorBmp); DeleteObject(andBmp);
    return icon;
}

static bool IsTaskbarDark() {
    HKEY hk; DWORD v = 0, sz = sizeof(v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"SystemUsesLightTheme", 0, NULL, (BYTE*)&v, &sz);
        RegCloseKey(hk);
    }
    return v == 0; // 0 = dark, 1 = light
}

static void InitTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    wcscpy_s(g_nid.szTip, L"LITE Music Player");
    if (g_trayIconHandle) { DestroyIcon(g_trayIconHandle); g_trayIconHandle = NULL; }
    g_trayIconHandle = CreatePlayerIcon(IsTaskbarDark());
    g_nid.hIcon = g_trayIconHandle;
    if (g_trayIcon) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayIcon = true;
}
static void RemoveTrayIcon() {
    if (g_trayIcon) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_trayIcon = false; }
    if (g_trayIconHandle) { DestroyIcon(g_trayIconHandle); g_trayIconHandle = NULL; }
}

// Tooltips
static const wchar_t *g_btnTooltips[BTN_LAST] = {};
static void InitTooltips() {
    g_btnTooltips[BTN_PLAY]     = L"Play / Pause";
    g_btnTooltips[BTN_STOP]     = L"Stop";
    g_btnTooltips[BTN_PREV]     = L"Previous";
    g_btnTooltips[BTN_NEXT]     = L"Next";
    g_btnTooltips[BTN_SHUFFLE]  = L"Repeat: Off / Once / All";
    g_btnTooltips[BTN_VOLUME]   = L"Volume (click to mute)";
    g_btnTooltips[BTN_SETTINGS] = L"Settings";
    g_btnTooltips[BTN_LOCATE]   = L"Locate Current Track";
    g_btnTooltips[BTN_SEARCH]   = L"Search Playlist";
    g_btnTooltips[BTN_EQ]       = L"Equalizer";
    g_btnTooltips[BTN_WIDGET]   = L"Toggle Compact Mode";
    g_btnTooltips[BTN_CLOSE]    = L"Close";
    g_btnTooltips[BTN_MINIMIZE] = L"Minimize";
    g_btnTooltips[BTN_MAXIMIZE] = L"Maximize";
}
static void DrawTooltip(HDC hdc, const wchar_t *text, int mx, int my) {
    if (!text || !*text) return;
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    int tw = sz.cx + 12, th = sz.cy + 6;
    int tx = mx - tw/2, ty = my - th - 10;
    if (tx < 2) tx = 2; if (tx + tw > 698) tx = 698 - tw;
    if (ty < 2) ty = my + 14;
    RECT tr = {tx, ty, tx + tw, ty + th};
    HBRUSH tb = CreateSolidBrush(COL_BG_DARK);
    FillRect(hdc, &tr, tb); DeleteObject(tb);
    HPEN tp = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN otp = (HPEN)SelectObject(hdc, tp);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, tr.left, tr.top, tr.right, tr.bottom);
    SelectObject(hdc, otp); DeleteObject(tp);
    SetTextColor(hdc, COL_TEXT);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, -1, &tr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void ApplyEQ(short *samples, int frames, int channels);

// Mini visualizer — ring buffer of recent amplitude peaks
#define VIZ_BARS 64
#define VIZ_MASK (VIZ_BARS-1)
static float g_vizRing[VIZ_BARS] = {};
static int g_vizHead = 0;
static void VizPush(float peak) {
    g_vizRing[g_vizHead] = peak;
    g_vizHead = (g_vizHead + 1) & VIZ_MASK;
}
static void DrawVisualizer(HDC hdc, int x, int y, int w, int h) {
    if (w < 20 || h < 4) return;
    SetBkMode(hdc, TRANSPARENT);
    float barW = (float)w / VIZ_BARS;
    for (int i = 0; i < VIZ_BARS; i++) {
        int idx = (g_vizHead - 1 - i) & VIZ_MASK;
        float val = g_vizRing[idx];
        if (val < 0.01f && !g_playing) val = 0;
        int barH = (int)(val * h);
        if (barH < 1 && val > 0) barH = 1;
        int bx = x + (int)((VIZ_BARS-1-i) * barW);
        RECT br = {bx, y + h - barH, bx + (int)barW - 1, y + h};
        HBRUSH bb = CreateSolidBrush(g_accentColor);
        FillRect(hdc, &br, bb); DeleteObject(bb);
    }
}

// Ogg Vorbis fallback via stb_vorbis
struct DecodedPCM {
    short *samples;
    int totalSamples; // total interleaved shorts
    int sampleRate;
    int channels;
    int readPos; // shorts consumed
};
static DecodedPCM g_decPCM = {NULL, 0, 0, 0, 0};
static bool g_useVorbis = false;
static volatile LONGLONG g_lastSamplePos = 0;

// Crossfade / gapless
static int g_crossfadeSec = 3; // seconds; 0=gapless, -1=off
static volatile bool g_xfading = false;
static bool g_xfadeUseVorbis = false;
static IMFSourceReader *g_xfadeReader = NULL;
static DecodedPCM g_xfadePCM = {NULL, 0, 0, 0, 0};
static int g_xfadeTrackIdx = -1;
static double g_xfadeStartPos = 0;
static HANDLE g_hXfadeThread = NULL;

static void CleanupXfade() {
    g_xfading = false;
    if (g_xfadeReader) { g_xfadeReader->Release(); g_xfadeReader = NULL; }
    if (g_xfadePCM.samples) { free(g_xfadePCM.samples); memset(&g_xfadePCM, 0, sizeof(g_xfadePCM)); }
    g_xfadeTrackIdx = -1;
    if (g_hXfadeThread) {
        WaitForSingleObject(g_hXfadeThread, 200);
        CloseHandle(g_hXfadeThread); g_hXfadeThread = NULL;
    }
}

// ---------------------------------------------------------------------------
// SMTC globals
// ---------------------------------------------------------------------------
static ISystemMediaTransportControls *g_smtc = NULL;
static struct EventRegistrationToken g_smtcButtonToken = {};
static bool g_inOpenFolder = false;

// SMTC button event handler (simple COM object)
struct SMTCButtonHandler : IUnknown {
    LONG ref = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (riid == IID_IUnknown) { *ppv = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return 0; }
};

struct SMTCButtonEventHandler : SMTCButtonHandler {
    HRESULT STDMETHODCALLTYPE Invoke(ISystemMediaTransportControls*, ISystemMediaTransportControlsButtonPressedEventArgs *args) {
        if (!args) return S_OK;
        int btn;
        if (SUCCEEDED(args->get_Button(&btn)))
            PostMessageW(g_hWnd, WM_APP+3, btn, 0);
        return S_OK;
    }
};

static void InitSMTC(HWND hWnd) {
    HSTRING hstr = NULL;
    HRESULT hr = WindowsCreateString(
        L"Windows.Media.SystemMediaTransportControls",
        46, &hstr);
    if (FAILED(hr)) { return; }

    IUnknown *factory = NULL;
    hr = RoGetActivationFactory(hstr, IID_PPV_ARGS(&factory));
    WindowsDeleteString(hstr);
    if (FAILED(hr) || !factory) { return; }

    ISystemMediaTransportControlsInterop *interop = NULL;
    hr = factory->QueryInterface(IID_PPV_ARGS(&interop));
    factory->Release();
    if (FAILED(hr) || !interop) { return; }

    hr = interop->GetForWindow(hWnd, IID_PPV_ARGS(&g_smtc));
    interop->Release();
    if (FAILED(hr) || !g_smtc) { return; }

    g_smtc->put_IsPlayEnabled(TRUE);
    g_smtc->put_IsPauseEnabled(TRUE);
    g_smtc->put_IsNextEnabled(TRUE);
    g_smtc->put_IsPreviousEnabled(TRUE);
    g_smtc->put_IsEnabled(TRUE);

    SMTCButtonEventHandler *handler = new SMTCButtonEventHandler();
    IUnknown *handlerUnk = NULL;
    handler->QueryInterface(IID_IUnknown, (void**)&handlerUnk);
    g_smtc->add_ButtonPressed(handlerUnk, &g_smtcButtonToken);
    handlerUnk->Release();
}

static void UninitSMTC() {
    if (g_smtc) {
        g_smtc->remove_ButtonPressed(g_smtcButtonToken);
        g_smtc->Release();
        g_smtc = NULL;
    }
}

static void UpdateSMTC() {
    if (!g_smtc) { return; }

    if (g_playing) {
        g_smtc->put_PlaybackStatus(g_paused ? SMTC_Paused : SMTC_Playing);
    } else {
        g_smtc->put_PlaybackStatus(SMTC_Stopped);
    }

    ISystemMediaTransportControlsDisplayUpdater *updater = NULL;
    if (FAILED(g_smtc->get_DisplayUpdater((void**)&updater))) return;

    updater->put_Type(SMTC_Music);

    IMusicDisplayProperties *music = NULL;
    if (SUCCEEDED(updater->get_MusicProperties((void**)&music))) {
        const wchar_t *title = g_songTitle.empty() ? L"Unknown Track" : g_songTitle.c_str();
        HSTRING hTitle = NULL;
        if (SUCCEEDED(WindowsCreateString(title, (UINT32)wcslen(title), &hTitle))) {
            music->put_Title(hTitle);
            WindowsDeleteString(hTitle);
        }
        const wchar_t *artist = g_artistName.empty() ? L"Unknown Artist" : g_artistName.c_str();
        HSTRING hArtist = NULL;
        if (SUCCEEDED(WindowsCreateString(artist, (UINT32)wcslen(artist), &hArtist))) {
            music->put_Artist(hArtist);
            WindowsDeleteString(hArtist);
        }
        music->Release();
    }

    updater->Update();
    updater->Release();
}
// ---------------------------------------------------------------------------
static void CalcLayout(const RECT &rc) {
    int w = rc.right - rc.left;
    int bh = BOTTOM_H;
    int bw = (w < 600) ? 22 : 28;
    int bgap = 4;
    int by = rc.bottom - bh + (bh - bw) / 2 + 4;
    int cx = PADDING + 85;
    SetRect(&g_btnRects[BTN_SHUFFLE],  cx,   by,  cx+bw,   by+bw);   cx += bw+bgap;
    SetRect(&g_btnRects[BTN_STOP],     cx,   by,  cx+bw,   by+bw);   cx += bw+bgap+8;
    SetRect(&g_btnRects[BTN_PREV],     cx,   by,  cx+bw,   by+bw);   cx += bw+bgap;
    SetRect(&g_btnRects[BTN_PLAY],     cx,   by,  cx+bw,   by+bw);   cx += bw+bgap;
    SetRect(&g_btnRects[BTN_NEXT],     cx,   by,  cx+bw,   by+bw);   cx += bw+bgap+8;
    SetRect(&g_btnRects[BTN_SETTINGS], cx,   by,  cx+bw,   by+bw);   cx += bw+bgap;
    SetRect(&g_btnRects[BTN_LOCATE],   cx,   by,  cx+bw,   by+bw);   cx += bw+bgap+8;
    SetRect(&g_btnRects[BTN_VOLUME],   cx,   by,  cx+bw,   by+bw);
    // Title bar buttons
    int tbw = 20, tby = (TITLE_H-tbw)/2;
    SetRect(&g_btnRects[BTN_SEARCH],   w-PADDING-tbw*6-10, tby, w-PADDING-tbw*5-10, tby+tbw);
    SetRect(&g_btnRects[BTN_EQ],       w-PADDING-tbw*5-8, tby, w-PADDING-tbw*4-8, tby+tbw);
    SetRect(&g_btnRects[BTN_WIDGET],   w-PADDING-tbw*4-6, tby, w-PADDING-tbw*3-6, tby+tbw);
    SetRect(&g_btnRects[BTN_MINIMIZE], w-PADDING-tbw*3-4, tby, w-PADDING-tbw*2-4, tby+tbw);
    SetRect(&g_btnRects[BTN_MAXIMIZE], w-PADDING-tbw*2-2, tby, w-PADDING-tbw-2, tby+tbw);
    SetRect(&g_btnRects[BTN_CLOSE],    w-PADDING-tbw,    tby, w-PADDING,    tby+tbw);
}

static int ReadSS(const unsigned char *p) { return (p[0]<<21)|(p[1]<<14)|(p[2]<<7)|p[3]; }

static void ParseID3(const wchar_t *path) {
    g_songTitle.clear(); g_artistName.clear();
    if (g_albumArt) { delete g_albumArt; g_albumArt = NULL; }
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    unsigned char hdr[10];
    if (fread(hdr,1,10,f)!=10 || memcmp(hdr,"ID3",3)!=0) { fclose(f); return; }
    int ver = hdr[3], tagSz = ReadSS(hdr+6);
    if (tagSz<=0||tagSz>50*1024*1024) { fclose(f); return; }
    unsigned char *tag = new unsigned char[tagSz];
    if (fread(tag,1,tagSz,f)!=(size_t)tagSz) { delete[]tag; fclose(f); return; }
    fclose(f);
    int pos = 0;
    while (pos+10 <= tagSz) {
        char fid[5]={}; memcpy(fid,tag+pos,4);
        int fs = (ver>=4)?ReadSS(tag+pos+4):(tag[pos+4]<<24)|(tag[pos+5]<<16)|(tag[pos+6]<<8)|tag[pos+7];
        if (fs<=0||fs>tagSz) break;
        int d = pos+10;
        if (d+fs>tagSz) break;
        auto readStr = [&](unsigned char *src, int len, std::wstring &out) {
            if (len<2) return;
            int enc = src[0];
            if (enc==0 && len>1) {
                // Try UTF-8 first; fall back to system ANSI
                int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (char*)(src+1), len-1, NULL, 0);
                UINT cp = CP_UTF8;
                if (n <= 0) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, (char*)(src+1), len-1, NULL, 0); }
                if (n>0 && n<=len*4){ out.resize(n); MultiByteToWideChar(cp, 0, (char*)(src+1), len-1, &out[0], n); }
            } else if (enc==3 && len>1) {
                int n = MultiByteToWideChar(CP_UTF8,0,(char*)(src+1),len-1,NULL,0);
                if (n>0){ out.resize(n); MultiByteToWideChar(CP_UTF8,0,(char*)(src+1),len-1,&out[0],n); }
            } else if ((enc==1||enc==2) && len>=4) {
                unsigned char *u=src+1; int sl=len-1;
                int bom=(u[0]<<8)|u[1], skip=(bom==0xFFFE||bom==0xFEFF)?2:0, chars=(sl-skip)/2;
                bool le = (bom == 0xFFFE);
                if (chars>0){ out.resize(chars);
                    for(int i=0;i<chars;i++) 
                        out[i]=le?(wchar_t)((u[skip+i*2+1]<<8)|u[skip+i*2]):(wchar_t)((u[skip+i*2]<<8)|u[skip+i*2+1]);
                    size_t nz=out.find(L'\0'); if(nz!=std::wstring::npos) out.resize(nz); }
            }
        };
        if (strcmp(fid,"TIT2")==0||strcmp(fid,"TT2")==0) readStr(tag+d, fs, g_songTitle);
        else if (strcmp(fid,"TPE1")==0||strcmp(fid,"TP1")==0) readStr(tag+d, fs, g_artistName);
        else if (strcmp(fid,"APIC")==0) {
            int enc=tag[d], ptr=d+1;
            while(ptr<d+fs && tag[ptr]) ptr++;
            if(++ptr>=d+fs){ pos+=fs+10; continue; }
            int picType=tag[ptr++];
            if (enc==0) { while(ptr<d+fs && tag[ptr]) ptr++; if(ptr<d+fs) ptr++; }
            else { while(ptr+1<d+fs && (tag[ptr]||tag[ptr+1])) ptr+=2; if(ptr+1<d+fs) ptr+=2; }
            int imgSz = d+fs-ptr;
            if (imgSz>0 && (picType==0x03||picType==0x00)) {
                IStream *stm = SHCreateMemStream(tag+ptr, imgSz);
                if (stm) { g_albumArt = Gdiplus::Image::FromStream(stm); stm->Release(); }
            }
        }
        pos += fs + 10;
    }
    delete[] tag;
    auto trim=[](std::wstring&s){ while(!s.empty()&&(s.back()==L' '||s.back()==L'\0')) s.pop_back(); };
    trim(g_songTitle); trim(g_artistName);
    for(size_t i=0;i<g_artistName.size();i++) if(g_artistName[i]==L'/') g_artistName[i]=L',';
}

// Per-file metadata cache for the browser
static std::vector<std::wstring> g_songTitles;
static std::vector<std::wstring> g_songArtists;
static std::vector<int> g_songDurations;
static std::vector<bool> g_metaCached;
static std::vector<int> g_metaPending;
static CRITICAL_SECTION g_metaCS;
static HANDLE g_hMetaThread = NULL;
static HANDLE g_hMetaWakeEvent = NULL;
static volatile bool g_metaThreadRun = false;

static void CacheCurrentMeta() {
    int i = g_trackIdx;
    if (i < 0 || i >= (int)g_songTitles.size()) return;
    EnterCriticalSection(&g_metaCS);
    g_songTitles[i] = g_songTitle;
    g_songArtists[i] = g_artistName;
    g_songDurations[i] = g_maxSec;
    g_metaCached[i] = true;
    LeaveCriticalSection(&g_metaCS);
}

static int DurationFromFile(const wchar_t *path) {
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext) return 0;
    wchar_t lowExt[8]; swprintf(lowExt,8,L"%s",ext);
    for(wchar_t *p=lowExt;*p;p++)*p=towlower(*p);
    if (wcscmp(lowExt, L".ogg") == 0) {
        char pathA[1024]; WideCharToMultiByte(CP_UTF8,0,path,-1,pathA,sizeof(pathA),NULL,NULL);
        stb_vorbis *v = stb_vorbis_open_filename(pathA, NULL, NULL);
        if (!v) return 0;
        float sec = stb_vorbis_stream_length_in_seconds(v);
        stb_vorbis_close(v);
        return (int)(sec + 0.5f);
    }
    if (wcscmp(lowExt, L".wav") == 0) {
        FILE *f = _wfopen(path, L"rb");
        if (!f) return 0;
        unsigned char h[44];
        if (fread(h,1,44,f)!=44||memcmp(h,"RIFF",4)!=0||memcmp(h+8,"WAVE",4)!=0){fclose(f);return 0;}
        int sr=*(int*)(h+24), bps=*(int*)(h+28), dataSz=0;
        if (*(int*)(h+36)!=0x61746164){
            fseek(f,44,SEEK_SET); unsigned char ch[8];
            while (fread(ch,1,8,f)==8){if(memcmp(ch,"data",4)==0){dataSz=*(int*)(ch+4);break;} fseek(f,*(int*)(ch+4),SEEK_CUR);}
        } else dataSz=*(int*)(h+40);
        fclose(f);
        if(sr>0&&bps>0) return dataSz/(sr*(bps/8));
        return 0;
    }
    if (wcscmp(lowExt, L".flac") == 0) {
        FILE *f = _wfopen(path, L"rb");
        if (!f) return 0;
        unsigned char h[42];
        if (fread(h,1,42,f)!=42||memcmp(h,"fLaC",4)!=0){fclose(f);return 0;}
        int sr=(h[18]<<12)|(h[19]<<4)|(h[20]>>4);
        long long total=((long long)(h[20]&0x0F)<<32)|((long long)h[21]<<24)|(h[22]<<16)|(h[23]<<8)|h[24];
        fclose(f);
        if(sr>0&&total>0) return (int)(total/sr);
        return 0;
    }
    IMFSourceReader *r=NULL;
    if (SUCCEEDED(MFCreateSourceReaderFromURL(path, NULL, &r))) {
        PROPVARIANT pv; PropVariantInit(&pv);
        HRESULT hr=r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv);
        int dur=0;
        if(SUCCEEDED(hr)&&pv.vt==VT_UI8) dur=(int)(pv.uhVal.QuadPart/10000000);
        PropVariantClear(&pv); r->Release();
        return dur;
    }
    return 0;
}

static void CacheFileMeta(int idx) {
    if (idx < 0 || idx >= (int)g_playlist.size()) return;
    const wchar_t *path = g_playlist[idx].c_str();
    // Duration
    g_songDurations[idx] = DurationFromFile(path);
    // Title/artist for MP3 via ID3
    const wchar_t *ext = wcsrchr(path, L'.');
    if (ext) {
        wchar_t lowExt[8]; swprintf(lowExt,8,L"%s",ext);
        for(wchar_t *p=lowExt;*p;p++)*p=towlower(*p);
        if (wcscmp(lowExt, L".mp3") == 0) {
            Gdiplus::Image *oldArt = g_albumArt; g_albumArt = NULL;
            std::wstring oldTitle = g_songTitle, oldArtist = g_artistName;
            ParseID3(path);
            if (!g_songTitle.empty()) g_songTitles[idx] = g_songTitle;
            if (!g_artistName.empty()) g_songArtists[idx] = g_artistName;
            if (g_albumArt && idx < (int)g_thumbs.size()) {
                if (g_thumbs[idx]) delete g_thumbs[idx];
                g_thumbs[idx] = new Gdiplus::Bitmap(32, 32);
                Gdiplus::Graphics g((Gdiplus::Bitmap*)g_thumbs[idx]);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(g_albumArt, 0, 0, 32, 32);
            }
            if (g_albumArt) delete g_albumArt;
            g_albumArt = oldArt;
            g_songTitle = oldTitle; g_artistName = oldArtist;
        }
    }
}

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL, buf, MAX_PATH);
    wchar_t *p = wcsrchr(buf, L'\\'); if (p) *p = 0;
    return buf;
}

static std::wstring GetCacheDir() {
    // Use directory of first playlist entry as key
    if (g_playlist.empty()) return L"";
    std::wstring key = g_playlist[0];
    size_t bs = key.rfind(L'\\');
    if (bs != std::wstring::npos) key = key.substr(0, bs);
    for (auto &c : key) { if (c == L'\\' || c == L':' || c == L'/') c = L'_'; }
    return GetExeDir() + L"\\cache_" + key + L".bin";
}

static __int64 GetTotalCacheSize() {
    __int64 total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((GetExeDir() + L"\\cache_*.bin").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { total += ((__int64)fd.nFileSizeHigh << 32) + fd.nFileSizeLow; }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return total;
}

static void DeleteAllCacheFiles() {
    WIN32_FIND_DATAW fd;
    std::wstring pattern = GetExeDir() + L"\\cache_*.bin";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            DeleteFileW((GetExeDir() + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // Reset metadata so it gets re-scanned
    g_songTitles.assign(g_playlist.size(), L"");
    g_songArtists.assign(g_playlist.size(), L"");
    g_songDurations.assign(g_playlist.size(), 0);
    g_metaCached.assign(g_playlist.size(), false);
}

static void SaveMetaCache() {
    std::wstring cp = GetCacheDir();
    if (g_playlist.empty()) return;
    FILE *f = _wfopen(cp.c_str(), L"wb");
    if (!f) return;
    int cnt = (int)g_playlist.size();
    fwrite("LMC1", 4, 1, f);
    fwrite(&cnt, 4, 1, f);
    for (int i = 0; i < cnt; i++) {
        int tl = (int)g_songTitles[i].size() * 2;
        fwrite(&tl, 4, 1, f);
        if (tl > 0) fwrite(g_songTitles[i].c_str(), 2, g_songTitles[i].size(), f);
        int al = (int)g_songArtists[i].size() * 2;
        fwrite(&al, 4, 1, f);
        if (al > 0) fwrite(g_songArtists[i].c_str(), 2, g_songArtists[i].size(), f);
        fwrite(&g_songDurations[i], 4, 1, f);
    }
    fclose(f);
}

static bool LoadMetaCache() {
    std::wstring cp = GetCacheDir();
    if (g_playlist.empty()) return false;
    FILE *f = _wfopen(cp.c_str(), L"rb");
    if (!f) return false;
    char magic[4];
    if (fread(magic,1,4,f)!=4 || memcmp(magic,"LMC1",4)!=0) { fclose(f); return false; }
    int cnt;
    if (fread(&cnt,4,1,f)!=1 || cnt != (int)g_playlist.size()) { fclose(f); return false; }
    for (int i = 0; i < cnt; i++) {
        int tl = 0, al = 0, dur = 0;
        if (fread(&tl,4,1,f)!=1) { fclose(f); return false; }
        std::wstring t; t.resize(tl/2);
        if (tl > 0 && (int)fread(&t[0],2,tl/2,f) != tl/2) { fclose(f); return false; }
        if (fread(&al,4,1,f)!=1) { fclose(f); return false; }
        std::wstring a; a.resize(al/2);
        if (al > 0 && (int)fread(&a[0],2,al/2,f) != al/2) { fclose(f); return false; }
        if (fread(&dur,4,1,f)!=1) { fclose(f); return false; }
        g_songTitles[i] = t; g_songArtists[i] = a; g_songDurations[i] = dur;
    }
    fclose(f);
    std::fill(g_metaCached.begin(), g_metaCached.end(), true);
    return true;
}

static void CacheMetaRange(int centerIdx) {
    if (g_playlist.empty() || !g_metaThreadRun) return;
    int total = (int)g_playlist.size();
    int start = max(0, centerIdx - 15);
    int end = min(total - 1, centerIdx + 25);
    bool added = false;
    EnterCriticalSection(&g_metaCS);
    for (int i = start; i <= end; i++) {
        if (i < (int)g_metaCached.size() && !g_metaCached[i]) {
            if (std::find(g_metaPending.begin(), g_metaPending.end(), i) == g_metaPending.end()) {
                g_metaPending.push_back(i);
                added = true;
            }
        }
    }
    LeaveCriticalSection(&g_metaCS);
    if (added) SetEvent(g_hMetaWakeEvent);
}

static void QueueAllMeta() {
    if (g_playlist.empty() || !g_metaThreadRun) return;
    bool added = false;
    EnterCriticalSection(&g_metaCS);
    for (int i = 0; i < (int)g_playlist.size(); i++) {
        if (i < (int)g_metaCached.size() && !g_metaCached[i]) {
            if (std::find(g_metaPending.begin(), g_metaPending.end(), i) == g_metaPending.end()) {
                g_metaPending.push_back(i);
                added = true;
            }
        }
    }
    LeaveCriticalSection(&g_metaCS);
    if (added) SetEvent(g_hMetaWakeEvent);
}

static DWORD WINAPI MetaThreadProc(LPVOID) {
    while (g_metaThreadRun) {
        WaitForSingleObject(g_hMetaWakeEvent, INFINITE);
        if (!g_metaThreadRun) break;
        while (g_metaThreadRun) {
            EnterCriticalSection(&g_metaCS);
            if (g_metaPending.empty()) { LeaveCriticalSection(&g_metaCS); break; }
            int idx = g_metaPending.back();
            g_metaPending.pop_back();
            bool last = g_metaPending.empty();
            LeaveCriticalSection(&g_metaCS);
            CacheFileMeta(idx);
            EnterCriticalSection(&g_metaCS);
            g_metaCached[idx] = true;
            LeaveCriticalSection(&g_metaCS);
            PostMessageW(g_hWnd, WM_APP, 0, 0);
            if (last) {
                PostMessageW(g_hWnd, WM_APP + 1, 0, 0);
            }
        }
    }
    return 0;
}

static Gdiplus::Image *LoadThumb(int idx) {
    if (idx < 0 || idx >= (int)g_thumbs.size()) return NULL;
    if (g_thumbs[idx]) return g_thumbs[idx];
    const wchar_t *path = g_playlist[idx].c_str();
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    unsigned char hdr[10];
    if (fread(hdr,1,10,f)!=10 || memcmp(hdr,"ID3",3)!=0) { fclose(f); return NULL; }
    int tagSz = ReadSS(hdr+6);
    if (tagSz<=0||tagSz>10*1024*1024) { fclose(f); return NULL; }
    unsigned char *tag = new unsigned char[tagSz];
    if (fread(tag,1,tagSz,f)!=tagSz) { delete[]tag; fclose(f); return NULL; }
    fclose(f);
    int pos = 0;
    while (pos+10 <= tagSz) {
        char fid[5]={}; memcpy(fid,tag+pos,4);
        int fs = (hdr[3]>=4)?ReadSS(tag+pos+4):(tag[pos+4]<<24)|(tag[pos+5]<<16)|(tag[pos+6]<<8)|tag[pos+7];
        if (fs<=0||fs>tagSz) break;
        int d = pos+10;
        if (d+fs>tagSz) break;
        if (strcmp(fid,"APIC")==0) {
            int enc=tag[d], ptr=d+1;
            while(ptr<d+fs && tag[ptr]) ptr++;
            if(++ptr>=d+fs){ pos+=fs+10; continue; }
            ptr++; // skip pic type
            if (enc==0) { while(ptr<d+fs && tag[ptr]) ptr++; if(ptr<d+fs) ptr++; }
            else { while(ptr+1<d+fs && (tag[ptr]||tag[ptr+1])) ptr+=2; if(ptr+1<d+fs) ptr+=2; }
            int imgSz = d+fs-ptr;
            if (imgSz>0) {
                IStream *stm = SHCreateMemStream(tag+ptr, imgSz);
                if (stm) {
                    Gdiplus::Image *full = Gdiplus::Image::FromStream(stm);
                    if (full) {
                        g_thumbs[idx] = new Gdiplus::Bitmap(32, 32);
                        Gdiplus::Graphics g((Gdiplus::Bitmap*)g_thumbs[idx]);
                        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                        g.DrawImage(full, 0, 0, 32, 32);
                        delete full;
                    }
                    stm->Release();
                }
            }
            break;
        }
        pos += fs + 10;
    }
    delete[] tag;
    return g_thumbs[idx];
}

static void LoadVisibleThumbs() {
    if (!g_hasFolder || g_playlist.empty()) return;
    RECT rc; GetClientRect(g_hWnd, &rc);
    rc.top += TITLE_H; rc.bottom -= BOTTOM_H;
    int bw = (rc.right - PADDING*2);
    if (bw < 50) return;
    int visRows = (rc.bottom - rc.top - PADDING*2) / 44;
    bool loaded = false;
    for (int i = g_browserScroll; i < (int)g_playlist.size() && i < g_browserScroll + visRows + 1; i++) {
        if (!g_thumbs[i] && LoadThumb(i)) loaded = true;
    }
    if (loaded) InvalidateRect(g_hWnd, NULL, TRUE);
}

// ---------------------------------------------------------------------------

constexpr DWORD AUDIO_FLAGS = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

static void EnumerateAudioDevices();
static IMMDevice *GetAudioDevice();
static WAVEFORMATEX *GetDeviceMixFormat();

static UINT32 GetDeviceMixRate() {
    UINT32 sr = 48000;
    IMMDevice *pDev = GetAudioDevice();
    if (pDev) {
        IAudioClient *pTemp = NULL;
        if (SUCCEEDED(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pTemp))) {
            WAVEFORMATEX *pMix = NULL;
            if (SUCCEEDED(pTemp->GetMixFormat(&pMix)) && pMix) {
                sr = pMix->nSamplesPerSec;
                CoTaskMemFree(pMix);
            }
            pTemp->Release();
        }
        pDev->Release();
    }
    return sr;
}

// Get full mix format from the selected audio device (caller must CoTaskMemFree)
static WAVEFORMATEX *GetDeviceMixFormat() {
    IMMDevice *pDev = GetAudioDevice();
    if (!pDev) return NULL;
    IAudioClient *pTemp = NULL;
    WAVEFORMATEX *pMix = NULL;
    if (SUCCEEDED(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pTemp))) {
        pTemp->GetMixFormat(&pMix);
        pTemp->Release();
    }
    pDev->Release();
    return pMix; // caller CoTaskMemFree
}

static short *Resample16(const short *input, int inRate, int channels, int inFrames, int outRate, int &outFrames) {
    if (inRate == outRate) { outFrames = inFrames; return NULL; }
    double ratio = (double)outRate / inRate;
    outFrames = (int)(inFrames * ratio);
    if (outFrames < 1) outFrames = 1;
    short *out = (short*)malloc(outFrames * channels * sizeof(short));
    if (!out) { outFrames = inFrames; return NULL; }
    for (int i = 0; i < outFrames; i++) {
        double srcPos = i / ratio;
        int idx = (int)srcPos;
        double frac = srcPos - idx;
        if (idx >= inFrames - 1) {
            for (int c = 0; c < channels; c++)
                out[i * channels + c] = input[(inFrames - 1) * channels + c];
        } else {
            for (int c = 0; c < channels; c++) {
                int s0 = input[idx * channels + c];
                int s1 = input[(idx + 1) * channels + c];
                out[i * channels + c] = (short)(s0 + (int)((s1 - s0) * frac));
            }
        }
    }
    return out;
}

static void MixXfadeSamples(short *buf, int ns, double fadeOut, double fadeIn) {
    for (int j = 0; j < ns; j++)
        buf[j] = (short)(buf[j] * fadeOut);
}

static DWORD WINAPI AudioThreadVorbis(LPVOID) {
    bool eosSent = false;
    while (g_audioRun) {
        if (g_audioPaused) { Sleep(1); continue; }

        // Crossfade handling
        if (g_xfading && g_xfadePCM.samples) {
            UINT32 pad = 0;
            g_pAudioClient->GetCurrentPadding(&pad);
            UINT32 freeFrames = g_audioBufFrames - pad;
            if (freeFrames == 0) { Sleep(1); continue; }
            int ch = g_decPCM.channels;
            int availCur = g_decPCM.totalSamples - g_decPCM.readPos;
            int availNext = g_xfadePCM.totalSamples - g_xfadePCM.readPos;
            if (availCur <= 0 && availNext <= 0) break;
            int availFrames = max(availCur, availNext) / ch;
            UINT32 nf = (UINT32)min(availFrames, (int)freeFrames);
            if (nf == 0) { Sleep(1); continue; }
            // Compute crossfade progress
            double curTime = (double)g_decPCM.readPos / ch / g_decPCM.sampleRate;
            double progress = (curTime - g_xfadeStartPos) / g_crossfadeSec;
            if (progress > 1.0) progress = 1.0;
            if (progress < 0) progress = 0;
            double fadeOut = cos(progress * 3.14159 / 2);
            double fadeIn = sin(progress * 3.14159 / 2);
            BYTE *pR = NULL;
            if (!g_audioRun) break;
            if (SUCCEEDED(g_pRenderClient->GetBuffer(nf, &pR)) && pR) {
                int ns = nf * ch;
                // Read current track with fade-out
                if (availCur > 0) {
                    int copySamples = min(availCur, ns);
                    memcpy(pR, g_decPCM.samples + g_decPCM.readPos, copySamples * sizeof(short));
                    MixXfadeSamples((short*)pR, copySamples, fadeOut, 0);
                    g_decPCM.readPos += copySamples;
                    // Zero-fill rest
                    if (copySamples < ns) memset(pR + copySamples * sizeof(short), 0, (ns - copySamples) * sizeof(short));
                } else {
                    memset(pR, 0, ns * sizeof(short));
                }
                // Add next track with fade-in
                if (availNext > 0) {
                    int copySamples = min(availNext, ns);
                    short *nextBuf = g_xfadePCM.samples + g_xfadePCM.readPos;
                    for (int j = 0; j < copySamples; j++)
                        ((short*)pR)[j] = (short)(((short*)pR)[j] + nextBuf[j] * fadeIn);
                    g_xfadePCM.readPos += copySamples;
                }
                if (g_eqEnabled) ApplyEQ((short*)pR, (int)nf, ch);
                float peak = 0;
                for (int j = 0; j < ns; j++) { float a = abs(((short*)pR)[j]) / 32768.0f; if (a > peak) peak = a; }
                VizPush(peak);
                g_pRenderClient->ReleaseBuffer(nf, 0);
                // Check if crossfade is done
                if (progress >= 1.0 || availCur <= 0) {
                    // Switch to next track permanently
                    g_xfading = false;
                    if (g_decPCM.samples) { free(g_decPCM.samples); g_decPCM.samples = NULL; }
                    g_decPCM = g_xfadePCM;
                    memset(&g_xfadePCM, 0, sizeof(g_xfadePCM));
                    g_trackIdx = g_xfadeTrackIdx;
                    if (g_xfadeReader) { g_xfadeReader->Release(); g_xfadeReader = NULL; }
                    eosSent = false;
                    CacheCurrentMeta();
                    UpdateSMTC();
                    InvalidateRect(g_hWnd, NULL, TRUE);
                }
            }
            continue;
        }

        if (g_decPCM.readPos >= g_decPCM.totalSamples) {
            if (!g_xfading && !eosSent) { PostMessage(g_hWnd, WM_APP + 2, 0, 0); eosSent = true; }
            Sleep(10); continue;
        }
        if (!g_pAudioClient || !g_pRenderClient) { break; }
        UINT32 pad = 0;
        g_pAudioClient->GetCurrentPadding(&pad);
        UINT32 free = g_audioBufFrames - pad;
        if (free == 0) { Sleep(1); continue; }
        int availSamples = g_decPCM.totalSamples - g_decPCM.readPos;
        int availFrames = availSamples / g_decPCM.channels;
        UINT32 nf = (UINT32)availFrames;
        if (nf > free) nf = free;
        if (nf > 0) {
            BYTE *pR = NULL;
            if (!g_audioRun) break;
            if (SUCCEEDED(g_pRenderClient->GetBuffer(nf, &pR)) && pR) {
                int srcOff = g_decPCM.readPos;
                memcpy(pR, g_decPCM.samples + srcOff, nf * g_decPCM.channels * sizeof(short));
                if (g_eqEnabled) ApplyEQ((short*)pR, (int)nf, g_decPCM.channels);
                short *sp = (short*)pR;
                int ns = nf * g_decPCM.channels;
                float peak = 0;
                for (int j = 0; j < ns; j++) { float a = abs(sp[j]) / 32768.0f; if (a > peak) peak = a; }
                VizPush(peak);
                g_pRenderClient->ReleaseBuffer(nf, 0);
                g_decPCM.readPos += nf * g_decPCM.channels;
            }
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Media Foundation SourceReader + WASAPI audio
// ---------------------------------------------------------------------------

// Frame-level reader for MF source (used during crossfade)
struct MFFrameReader {
    IMFSourceReader *reader;
    BYTE *cache;
    DWORD cacheCap;
    DWORD cacheLen;
    DWORD cacheOff;
    UINT32 frameSize;
    bool eof;
    LONGLONG lastPos;
    void Init(IMFSourceReader *r, UINT32 fs) {
        reader = r; frameSize = fs; eof = false; cacheCap = 65536;
        cache = (BYTE*)malloc(cacheCap); cacheLen = 0; cacheOff = 0; lastPos = 0;
    }
    void Cleanup() { if (cache) { free(cache); cache = NULL; } }
    bool Read(BYTE *dst, UINT32 frames) {
        UINT32 needed = frames * frameSize;
        while (!eof && (cacheLen - cacheOff) < needed) {
            // Move remaining to front
            if (cacheOff > 0 && cacheLen > cacheOff) {
                memmove(cache, cache + cacheOff, cacheLen - cacheOff);
                cacheLen -= cacheOff; cacheOff = 0;
            }
            if (cacheCap - cacheLen < 65536) { cacheCap *= 2; cache = (BYTE*)realloc(cache, cacheCap); }
            DWORD flags = 0; LONGLONG ts = 0; IMFSample *pSamp = NULL;
            HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &ts, &pSamp);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { eof = true; break; }
            if (pSamp) {
                if (SUCCEEDED(pSamp->GetSampleTime(&ts))) lastPos = ts;
                IMFMediaBuffer *pBuf = NULL;
                pSamp->ConvertToContiguousBuffer(&pBuf);
                if (pBuf) {
                    BYTE *pData = NULL; DWORD cbData = 0;
                    pBuf->Lock(&pData, NULL, &cbData);
                    if (pData && cbData > 0) {
                        if (cacheCap - cacheLen < cbData) { cacheCap = max(cacheCap * 2, cacheLen + cbData); cache = (BYTE*)realloc(cache, cacheCap); }
                        memcpy(cache + cacheLen, pData, cbData); cacheLen += cbData;
                    }
                    pBuf->Unlock(); pBuf->Release();
                }
                pSamp->Release();
            }
        }
        DWORD avail = cacheLen - cacheOff;
        if (avail == 0) return false;
        UINT32 copy = min(needed, avail);
        memcpy(dst, cache + cacheOff, copy);
        cacheOff += copy;
        if (copy < needed) memset(dst + copy, 0, needed - copy);
        return true;
    }
};

static DWORD WINAPI AudioThread(LPVOID) {
    bool ended=false;
    bool eosSent=false;
    if(!g_pReader){return 0;}
    MFFrameReader curFR, nextFR; bool frInit = false;
    while(g_audioRun && !ended){
        if(g_audioPaused){Sleep(1);continue;}

        // Crossfade mode: frame-level mixing from two MF readers
        if (g_xfading && g_xfadeReader && !ended) {
            if (!frInit) { curFR.Init(g_pReader, g_audioFrameSize); nextFR.Init(g_xfadeReader, g_audioFrameSize); frInit = true; }
            LONGLONG xfStartHns = (LONGLONG)(g_xfadeStartPos * 10000000);
            LONGLONG xfDurHns = (LONGLONG)(g_crossfadeSec * 10000000);
            while (g_audioRun && g_xfading && g_xfadeReader && !ended) {
                if (g_audioPaused) { Sleep(1); continue; }
                UINT32 pad = 0;
                g_pAudioClient->GetCurrentPadding(&pad);
                UINT32 freeFrames = g_audioBufFrames - pad;
                if (freeFrames == 0) { Sleep(1); continue; }
                UINT32 nf = min(freeFrames, 4096u);
                BYTE tmpCur[65536], tmpNext[65536];
                UINT32 cb = nf * g_audioFrameSize;
                if (cb > sizeof(tmpCur)) { cb = sizeof(tmpCur); nf = cb / g_audioFrameSize; }
                bool curAlive = curFR.Read(tmpCur, nf);
                bool nextAlive = nextFR.Read(tmpNext, nf);
                if (!curAlive && !nextAlive) break;
                LONGLONG nowHns = (curFR.lastPos > 0) ? curFR.lastPos : nextFR.lastPos;
                double progress = (nowHns > 0) ? (double)(nowHns - xfStartHns) / xfDurHns : 0;
                if (progress > 1.0) progress = 1.0; if (progress < 0) progress = 0;
                double fadeOut = cos(progress * 3.14159 / 2);
                double fadeIn = sin(progress * 3.14159 / 2);
                BYTE *pR = NULL;
                if (SUCCEEDED(g_pRenderClient->GetBuffer(nf, &pR)) && pR) {
                    short *sCur = (short*)tmpCur, *sNext = (short*)tmpNext, *sOut = (short*)pR;
                    int ns = nf * (g_audioFrameSize / 2);
                    for (int j = 0; j < ns; j++)
                        sOut[j] = (short)(sCur[j] * fadeOut + sNext[j] * fadeIn);
                    if (g_eqEnabled) ApplyEQ(sOut, (int)nf, g_audioFrameSize / 2);
                    float peak = 0;
                    for (int j = 0; j < ns; j++) { float a = abs(sOut[j]) / 32768.0f; if (a > peak) peak = a; }
                    VizPush(peak);
                    g_pRenderClient->ReleaseBuffer(nf, 0);
                    g_lastSamplePos = nowHns;
                }
                if (progress >= 1.0 || (!curAlive && fadeIn > 0.99) || !curAlive) {
                    // Crossfade complete — switch to next track permanently
                    g_xfading = false;
                    g_trackIdx = g_xfadeTrackIdx;
                    g_userStop = false;
                    if (g_pReader) { g_pReader->Release(); g_pReader = NULL; }
                    g_pReader = g_xfadeReader; g_xfadeReader = NULL;
                    g_useVorbis = false;
                    ended = false; eosSent = false;
                    CacheCurrentMeta();
                    UpdateSMTC();
                    InvalidateRect(g_hWnd, NULL, TRUE);
                    curFR.Cleanup(); nextFR.Cleanup(); frInit = false;
                    // Now we need to continue normal playback with the new source
                    // Reset the reader state — the MFFrameReader is done, fall through to normal loop
                    if (g_xfadePCM.samples) { free(g_xfadePCM.samples); memset(&g_xfadePCM, 0, sizeof(g_xfadePCM)); }
                    CleanupXfade();
                    break;
                }
            }
            if (frInit) { curFR.Cleanup(); nextFR.Cleanup(); frInit = false; }
            if (g_xfading) break;
            // If we completed the xfade, the outer loop will continue with the new g_pReader
            continue;
        }

        DWORD flags=0;LONGLONG ts=0;IMFSample *pSamp=NULL;
        HRESULT hr=g_pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,NULL,&flags,&ts,&pSamp);
        if(FAILED(hr)){break;}
        if(flags & MF_SOURCE_READERF_ENDOFSTREAM){if(!g_xfading && !eosSent){PostMessage(g_hWnd,WM_APP+2,0,0);eosSent=true;}ended=true;Sleep(10);continue;}
        if(pSamp){
            if(SUCCEEDED(pSamp->GetSampleTime(&ts))) g_lastSamplePos=ts;
            IMFMediaBuffer *pBuf=NULL;
            pSamp->ConvertToContiguousBuffer(&pBuf);
            if(!pBuf){continue;}
            BYTE *pData=NULL;DWORD cbData=0;
            pBuf->Lock(&pData,NULL,&cbData);
            if(!pData){pBuf->Release();continue;}
            BYTE *pCur=pData;DWORD cbRem=cbData;
            while(cbRem>0&&g_audioRun){
                if(g_audioPaused){Sleep(1);continue;}
                if(!g_pAudioClient||!g_pRenderClient){break;}
                UINT32 pad=0;
                g_pAudioClient->GetCurrentPadding(&pad);
                UINT32 free=g_audioBufFrames-pad;
                if(free==0){Sleep(1);continue;}
                UINT32 nf=cbRem/g_audioFrameSize;
                if(nf>free) nf=free;
                if(nf>0){BYTE *pR=NULL;if(SUCCEEDED(g_pRenderClient->GetBuffer(nf,&pR))&&pR){memcpy(pR,pCur,nf*g_audioFrameSize);if(g_eqEnabled)ApplyEQ((short*)pR,(int)nf,g_audioFrameSize/2);short *sp=(short*)pR;int ns=nf*(g_audioFrameSize/2);float peak=0;for(int j=0;j<ns;j++){float a=abs(sp[j])/32768.0f;if(a>peak)peak=a;}VizPush(peak);g_pRenderClient->ReleaseBuffer(nf,0);}}
                UINT32 written=nf*g_audioFrameSize;
                pCur+=written;cbRem-=written;
            }
            pBuf->Unlock();pBuf->Release();pSamp->Release();
        }
    }

    if (frInit) { curFR.Cleanup(); nextFR.Cleanup(); }
    return 0;
}

static void StopAudio() {
    g_useVorbis = false;
    if (g_decPCM.samples) { free(g_decPCM.samples); g_decPCM.samples = NULL; }
    g_decPCM.totalSamples = 0; g_decPCM.readPos = 0;
    // Signal thread
    g_audioRun = false;
    g_audioPaused = false;
    // Mute
    if (g_pSimpleVol) g_pSimpleVol->SetMasterVolume(0.0f, NULL);
    // Flush SourceReader (best-effort) to unblock any pending ReadSample
    if (g_pReader) g_pReader->Flush(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    // Kill the audio thread — give it time to exit naturally (g_audioRun=false + Flush)
    if (g_hAudioThread) {
        if (WaitForSingleObject(g_hAudioThread, 200) != WAIT_OBJECT_0)
            TerminateThread(g_hAudioThread, 0);
        CloseHandle(g_hAudioThread);
        g_hAudioThread = NULL;
    }
    // Stop + Reset WASAPI — thread is dead, safe to do so
    if (g_pAudioClient) { g_pAudioClient->Stop(); g_pAudioClient->Reset(); }
    if(g_pSimpleVol){g_pSimpleVol->Release();g_pSimpleVol=NULL;}
    if(g_pRenderClient){g_pRenderClient->Release();g_pRenderClient=NULL;}
    if(g_pAudioClient){g_pAudioClient->Release();g_pAudioClient=NULL;}
    if(g_pReader){g_pReader->Release();g_pReader=NULL;}
    CleanupXfade(); g_playing=false;g_paused=false;g_curSec=0;g_maxSec=0;
    if(g_albumArt){delete g_albumArt;g_albumArt=NULL;}
    g_songTitle.clear();g_artistName.clear();
    UpdateSMTC();
    InvalidateRect(g_hWnd,NULL,TRUE);
}

static void SeekTo(double sec) {

    if(g_useVorbis){
        if(g_hAudioThread){g_audioRun=false;
        if(WaitForSingleObject(g_hAudioThread,200)!=WAIT_OBJECT_0) TerminateThread(g_hAudioThread,0);
        CloseHandle(g_hAudioThread);g_hAudioThread=NULL;}
        int samplePos = (int)(sec * g_decPCM.sampleRate) * g_decPCM.channels;
        if (samplePos > g_decPCM.totalSamples) samplePos = g_decPCM.totalSamples;
        if (samplePos < 0) samplePos = 0;
        g_decPCM.readPos = samplePos;
        if(g_pAudioClient){g_pAudioClient->Stop();g_pAudioClient->Reset();g_pAudioClient->Start();}
        g_curSec=sec;
        if(!g_pAudioClient||!g_pRenderClient||!g_decPCM.samples){return;}
        g_audioRun=true;g_audioPaused=false;
        g_hAudioThread=CreateThread(NULL,0,AudioThreadVorbis,NULL,0,NULL);
        return;
    }
    if(g_pReader){g_pReader->Flush(MF_SOURCE_READER_FIRST_AUDIO_STREAM);}
    if(g_hAudioThread){g_audioRun=false;
        if(WaitForSingleObject(g_hAudioThread,200)!=WAIT_OBJECT_0){TerminateThread(g_hAudioThread,0);}
        CloseHandle(g_hAudioThread);g_hAudioThread=NULL;}
    if(g_pReader){
        PROPVARIANT pv;PropVariantInit(&pv);pv.vt=VT_I8;pv.hVal.QuadPart=(LONGLONG)(sec*10000000);
        g_pReader->SetCurrentPosition(GUID_NULL,pv);
        PropVariantClear(&pv);
    }
    if(g_pAudioClient){g_pAudioClient->Stop();g_pAudioClient->Reset();g_pAudioClient->Start();}
    g_lastSamplePos=(LONGLONG)(sec*10000000);g_curSec=sec;
    if(!g_pReader||!g_pAudioClient){g_audioRun=false;return;}
    g_audioRun=true;g_audioPaused=false;
    g_hAudioThread=CreateThread(NULL,0,AudioThread,NULL,0,NULL);
}

static bool PlayVorbisInto(const wchar_t *path, DecodedPCM &out) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return false;
    unsigned char magic[4];
    bool isOgg = (fread(magic,1,4,f)==4 && magic[0]==0x4F && magic[1]==0x67 && magic[2]==0x67 && magic[3]==0x53);
    fclose(f);
    if (!isOgg) return false;
    char pathA[1024]; WideCharToMultiByte(CP_UTF8,0,path,-1,pathA,sizeof(pathA),NULL,NULL);
    int ch = 0, sr = 0;
    short *pcm = NULL;
    int totalSamples = stb_vorbis_decode_filename(pathA, &ch, &sr, &pcm);
    if (totalSamples <= 0 || !pcm) return false;
    UINT32 devSr = GetDeviceMixRate();
    short *finalPCM = pcm;
    int finalRate = sr;
    int finalFrames = totalSamples;
    if (sr != (int)devSr) {
        int outFrames = 0;
        short *resampled = Resample16(pcm, sr, ch, totalSamples, devSr, outFrames);
        if (resampled) { finalPCM = resampled; finalRate = devSr; finalFrames = outFrames; free(pcm); }
    }
    out.samples = finalPCM;
    out.totalSamples = finalFrames * ch;
    out.sampleRate = finalRate;
    out.channels = ch;
    out.readPos = 0;
    return true;
}

static bool PlayVorbis(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return false;
    unsigned char magic[4];
    bool isOgg = (fread(magic,1,4,f)==4 && magic[0]==0x4F && magic[1]==0x67 && magic[2]==0x67 && magic[3]==0x53);
    fclose(f);
    if (!isOgg) return false;

    char pathA[1024]; WideCharToMultiByte(CP_UTF8,0,path,-1,pathA,sizeof(pathA),NULL,NULL);
    int ch = 0, sr = 0;
    short *pcm = NULL;
    int totalSamples = stb_vorbis_decode_filename(pathA, &ch, &sr, &pcm);
    if (totalSamples <= 0 || !pcm) return false;
    UINT32 devSr = GetDeviceMixRate();
    short *finalPCM = pcm;
    int finalRate = sr;
    int finalFrames = totalSamples; // stb_vorbis returns samples-per-channel
    if (sr != (int)devSr) {
        int outFrames = 0;
        short *resampled = Resample16(pcm, sr, ch, totalSamples, devSr, outFrames);
        if (resampled) { finalPCM = resampled; finalRate = devSr; finalFrames = outFrames; free(pcm); }
    }

    g_decPCM.samples = finalPCM;
    g_decPCM.totalSamples = finalFrames * ch;
    g_decPCM.sampleRate = finalRate;
    g_decPCM.channels = ch;
    g_decPCM.readPos = 0;
    g_audioFrameSize = ch * sizeof(short);

    // Init WASAPI
    IMMDevice *pDev = GetAudioDevice();
    HRESULT hr = E_FAIL;
    if (pDev) {
        hr = pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&g_pAudioClient);
        pDev->Release();
    }
    if (!g_pAudioClient) return false;

    // Build WAVEFORMATEX: use device mix rate + 16-bit PCM (our decoded format)
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)ch;
    wfx.nSamplesPerSec = finalRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(ch * 2);
    wfx.nAvgBytesPerSec = finalRate * wfx.nBlockAlign;
    wfx.cbSize = 0;
    hr = g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDIO_FLAGS, 10000000, 0, &wfx, NULL);
    if (FAILED(hr)) {
        // Fallback: try device mix format rate instead
        WAVEFORMATEX *pMix = GetDeviceMixFormat();
        if (pMix) {
            wfx.nSamplesPerSec = pMix->nSamplesPerSec;
            wfx.nChannels = pMix->nChannels;
            wfx.nBlockAlign = wfx.nChannels * 2;
            wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
            hr = g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDIO_FLAGS, 10000000, 0, &wfx, NULL);
            if (SUCCEEDED(hr)) {
                // Resample our audio data to match
                int outFrames = 0;
                short *converted = Resample16(finalPCM, finalRate, ch, finalFrames, wfx.nSamplesPerSec, outFrames);
                if (converted) {
                    if (finalPCM != pcm) free(finalPCM);
                    finalPCM = converted; finalRate = wfx.nSamplesPerSec; finalFrames = outFrames;
                    ch = wfx.nChannels;
                    g_decPCM.samples = finalPCM;
                    g_decPCM.totalSamples = finalFrames * ch;
                    g_decPCM.sampleRate = finalRate;
                    g_decPCM.channels = ch;
                    g_decPCM.readPos = 0;
                    g_audioFrameSize = ch * sizeof(short);
                }
            }
            CoTaskMemFree(pMix);
        }
    }
    if (FAILED(hr)) {
        // Last resort: 44100 stereo 16-bit
        wfx.nChannels = 2; wfx.nSamplesPerSec = 44100;
        wfx.nBlockAlign = 4; wfx.nAvgBytesPerSec = 44100 * 4;
        hr = g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDIO_FLAGS, 10000000, 0, &wfx, NULL);
        if (FAILED(hr)) { g_pAudioClient->Release(); g_pAudioClient = NULL; return false; }
    }
    g_audioFrameSize = wfx.nBlockAlign;
    g_eqSampleRate = wfx.nSamplesPerSec;
    if (g_eqEnabled) CalcEQCoeffs(g_eqSampleRate);

    g_pAudioClient->GetBufferSize(&g_audioBufFrames);
    g_pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&g_pRenderClient);
    g_pAudioClient->GetService(__uuidof(ISimpleAudioVolume), (void**)&g_pSimpleVol);
    if (g_pSimpleVol) g_pSimpleVol->SetMasterVolume(g_volume / 1000.0f, NULL);
    if (g_muted && g_pSimpleVol) g_pSimpleVol->SetMasterVolume(0, NULL);
    g_pAudioClient->Start();

    g_maxSec = finalFrames / finalRate;
    g_curSec = 0;
    g_audioRun = true; g_audioPaused = false;
    g_hAudioThread = CreateThread(NULL, 0, AudioThreadVorbis, NULL, 0, NULL);
    g_useVorbis = true;
    return true;
}

static void PlayFile(const wchar_t *path, bool userStop) {
    char pathA[1024]; WideCharToMultiByte(CP_ACP,0,path,-1,pathA,sizeof(pathA),NULL,NULL);
    StopAudio();
        HRESULT hr=MFCreateSourceReaderFromURL(path,NULL,&g_pReader);
    
    if(FAILED(hr)||!g_pReader){
        if (PlayVorbis(path)) {
            goto done;
        }
        g_songTitle=L"Failed to open";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;
    }
    { // MF-specific scope
    UINT32 devSr=GetDeviceMixRate();
    IMFMediaType *pMT=NULL;
    MFCreateMediaType(&pMT);
    pMT->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
    pMT->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
    pMT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,devSr);
    pMT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);
    pMT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
    hr=g_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,NULL,pMT);
    pMT->Release();
    if(FAILED(hr)){g_pReader->Release();g_pReader=NULL;g_songTitle=L"Unsupported format";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;}
    IMFMediaType *pA=NULL;
    hr=g_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&pA);
    if(FAILED(hr)||!pA){g_pReader->Release();g_pReader=NULL;g_songTitle=L"No audio type";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;}

    WAVEFORMATEX *pwfx=NULL;UINT32 cbw=0;
    MFCreateWaveFormatExFromMFMediaType(pA,&pwfx,&cbw);
    UINT32 sr=0,ch=0,bps=0;
    pA->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&sr);
    pA->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch);
    pA->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bps);
    pA->Release();

    if(!pwfx){g_pReader->Release();g_pReader=NULL;g_songTitle=L"Bad format";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;}
    g_audioFrameSize=pwfx->nBlockAlign;

    PROPVARIANT pv;PropVariantInit(&pv);
    g_pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&pv);
    if(pv.vt==VT_UI8) g_maxSec=(int)(pv.uhVal.QuadPart/10000000);
    else g_maxSec=0;
    PropVariantClear(&pv);

    // Build WAVEFORMATEX matching the decoded output
    WAVEFORMATEX wfx={};
    wfx.wFormatTag=WAVE_FORMAT_PCM;
    wfx.nChannels=(WORD)ch;
    wfx.nSamplesPerSec=sr;
    wfx.wBitsPerSample=16;
    wfx.nBlockAlign=(WORD)(ch*16/8);
    wfx.nAvgBytesPerSec=sr*wfx.nBlockAlign;
    wfx.cbSize=0;

    IMMDevice *pDev=GetAudioDevice();
    hr=E_FAIL;
    if(pDev){
        hr=pDev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,NULL,(void**)&g_pAudioClient);
        pDev->Release();
    }
    if(!g_pAudioClient){CoTaskMemFree(pwfx);g_pReader->Release();g_pReader=NULL;g_songTitle=L"No audio device";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;}
    // Try 16-bit PCM at decoded rate first
    hr=g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDIO_FLAGS,10000000,0,&wfx,NULL);
    if(FAILED(hr)){
        // Fallback: try device mix format rate
        WAVEFORMATEX *pMix=GetDeviceMixFormat();
        if(pMix){
            wfx.nSamplesPerSec=pMix->nSamplesPerSec;
            wfx.nChannels=pMix->nChannels;
            wfx.nBlockAlign=wfx.nChannels*2;
            wfx.nAvgBytesPerSec=wfx.nSamplesPerSec*wfx.nBlockAlign;
            hr=g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDIO_FLAGS,10000000,0,&wfx,NULL);
            CoTaskMemFree(pMix);
        }
    }
    if(FAILED(hr)){
        wfx.nChannels=2;wfx.nSamplesPerSec=44100;wfx.nBlockAlign=4;wfx.nAvgBytesPerSec=44100*4;
        hr=g_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDIO_FLAGS,10000000,0,&wfx,NULL);
        if(FAILED(hr)){g_pAudioClient->Release();g_pAudioClient=NULL;CoTaskMemFree(pwfx);g_pReader->Release();g_pReader=NULL;g_songTitle=L"Audio init failed";g_artistName.clear();InvalidateRect(g_hWnd,NULL,TRUE);return;}
    }
    g_audioFrameSize = wfx.nBlockAlign;
    g_eqSampleRate = wfx.nSamplesPerSec;
    if (g_eqEnabled) CalcEQCoeffs(g_eqSampleRate);
    // If the output format differs from the source, reconfigure MF reader to convert
    if (wfx.nSamplesPerSec != sr || wfx.nChannels != ch) {
        IMFMediaType *pNewMT = NULL;
        if (SUCCEEDED(MFCreateMediaType(&pNewMT))) {
            pNewMT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pNewMT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            pNewMT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, wfx.nSamplesPerSec);
            pNewMT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, wfx.nChannels);
            pNewMT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            pNewMT->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, wfx.nBlockAlign);
            pNewMT->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, wfx.nAvgBytesPerSec);
            if (SUCCEEDED(g_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pNewMT))) {
                sr = wfx.nSamplesPerSec;
                ch = wfx.nChannels;
            }
            pNewMT->Release();
        }
    }
    g_pAudioClient->GetBufferSize(&g_audioBufFrames);
    g_pAudioClient->GetService(__uuidof(IAudioRenderClient),(void**)&g_pRenderClient);
    g_pAudioClient->GetService(__uuidof(ISimpleAudioVolume),(void**)&g_pSimpleVol);
    if(g_pSimpleVol) g_pSimpleVol->SetMasterVolume(g_volume/1000.0f,NULL);
    g_pAudioClient->Start();
    CoTaskMemFree(pwfx);
    g_audioRun=true;g_audioPaused=false;
    g_hAudioThread=CreateThread(NULL,0,AudioThread,NULL,0,NULL);

    ParseID3(path);
    } // end MF-specific scope
done:
    if(g_songTitle.empty()){
        std::wstring fn(path);size_t bs=fn.find_last_of(L'\\');
        if(bs!=std::wstring::npos)fn=fn.substr(bs+1);
        size_t dot=fn.find_last_of(L'.');if(dot!=std::wstring::npos)fn=fn.substr(0,dot);
        g_songTitle=fn;
    }
    if(g_artistName.empty())g_artistName=L"Unknown artist";
    g_curSec=0;g_playing=true;g_paused=false;g_userStop=userStop;
    CacheCurrentMeta();
    UpdateSMTC();
    InvalidateRect(g_hWnd,NULL,TRUE);
}

static void TogglePause() {
    if((!g_pReader&&!g_useVorbis)||!g_playing)return;
    if(g_paused){

        g_audioPaused=false; g_paused=false;
        if(g_pAudioClient){g_pAudioClient->Start();}
        UpdateSMTC();
    } else {

        // Drain stale window messages so COM doesn't dispatch them re-entrantly
        // during g_pAudioClient->Stop(). Discard any lingering WM_APP+2 (EOS)
        // since we are pausing and don't want automatic track advance.
        MSG m;
        while(PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)){
            if(m.message == WM_QUIT){ PostQuitMessage((int)m.wParam); break; }
            if(m.message == WM_APP+2) continue; // stale EOS, discard
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        g_audioPaused=true; g_paused=true;
        if(g_pAudioClient){g_pAudioClient->Stop();}
        UpdateSMTC();
    }
    InvalidateRect(g_hWnd,NULL,TRUE);
}

static bool InitXfadeSource(int idx) {
    const wchar_t *path = g_playlist[idx].c_str();
    char pathA[1024]; WideCharToMultiByte(CP_ACP,0,path,-1,pathA,sizeof(pathA),NULL,NULL);
    // Try MF first
    IMFSourceReader *r = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(path, NULL, &r);
    if (SUCCEEDED(hr) && r) {
        IMFMediaType *pMT = NULL;
        MFCreateMediaType(&pMT);
        pMT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pMT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pMT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        pMT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        pMT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        hr = r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pMT);
        pMT->Release();
        if (SUCCEEDED(hr)) {
            g_xfadeReader = r;
            g_xfadeUseVorbis = false;
            return true;
        }
        r->Release();
    }
    // Fallback Vorbis
    DecodedPCM pcm = {NULL, 0, 0, 0, 0};
    if (PlayVorbisInto(path, pcm)) {
        g_xfadePCM = pcm;
        g_xfadeUseVorbis = true;
        return true;
    }
    return false;
}

static void PlayTrack(int idx, bool us) {
    if(idx<0||idx>=(int)g_playlist.size()){return;}
    // Crossfade/gapless transition if currently playing and not user-stopped
    if (g_crossfadeSec >= 0 && g_playing && !g_paused && !g_xfading && !us && idx != g_trackIdx && g_pAudioClient) {
        // Don't crossfade if the current track has effectively ended — no samples to fade out from
        if (g_curSec >= g_maxSec - 0.5) goto normalPlay;
        CleanupXfade();
        g_xfadeTrackIdx = idx;
        g_xfadeStartPos = g_curSec;
        g_userStop = false;
        g_repeatOnce = false;
        if (InitXfadeSource(idx)) {
            g_xfading = true;
            return;
        }
    }
    // Normal immediate switch
normalPlay:
    CleanupXfade();
    g_trackIdx=idx; g_userStop=us; g_repeatOnce=false; PlayFile(g_playlist[idx].c_str(),us);
}

// ---------------------------------------------------------------------------
// Folder selection
// ---------------------------------------------------------------------------
static void OpenFolder() {
    if (g_inOpenFolder) { return; }
    g_inOpenFolder = true;
    wchar_t path[MAX_PATH]={};
    BROWSEINFOW bi={}; bi.hwndOwner=g_hWnd; bi.lpszTitle=L"Select a music folder";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl=SHBrowseForFolderW(&bi);
    if(!pidl) { g_inOpenFolder = false; return; }
    SHGetPathFromIDListW(pidl,path);
    IMalloc *im=NULL;
    if(SUCCEEDED(SHGetMalloc(&im))){im->Free(pidl);im->Release();}

    std::wstring dir(path);
    g_folderName=dir.substr(dir.find_last_of(L'\\')+1);
    g_playlist.clear();

    WIN32_FIND_DATAW fd;
    HANDLE hf=FindFirstFileW((dir+L"\\*").c_str(),&fd);
    if(hf!=INVALID_HANDLE_VALUE){
        do{
            if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
                std::wstring ext=PathFindExtensionW(fd.cFileName);
                std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
                if(ext==L".mp3"||ext==L".wav"||ext==L".flac"||ext==L".m4a"||ext==L".aac"||ext==L".ogg"||ext==L".wma")
                    g_playlist.push_back(dir+L"\\"+fd.cFileName);
            }
        }while(FindNextFileW(hf,&fd));
        FindClose(hf);
    }
    std::sort(g_playlist.begin(),g_playlist.end());
    if(g_playlist.empty()){ MessageBoxW(g_hWnd,L"No audio files found.",L"LITE Music Player",MB_OK|MB_ICONINFORMATION); g_inOpenFolder = false; return; }
    g_hasFolder=true; g_trackIdx=0; g_browserScroll=0;
    // Init metadata cache (lazy — only visible range + buffer)
    g_songTitles.assign(g_playlist.size(), L"");
    g_songArtists.assign(g_playlist.size(), L"");
    g_songDurations.assign(g_playlist.size(), 0);
    g_metaCached.assign(g_playlist.size(), false);
    // Try loading persistent cache first
    if (!LoadMetaCache())
        CacheMetaRange(0);
    for (auto *t : g_thumbs) if (t) delete t;
    g_thumbs.assign(g_playlist.size(), NULL);
    PlayFile(g_playlist[0].c_str(),false);
    InvalidateRect(g_hWnd,NULL,TRUE);
    g_inOpenFolder = false;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
static bool PtInBtn(POINT pt, BtnID id) { return PtInRect(&g_btnRects[id],pt); }

static void SeekRect(const RECT &rc, int&x,int&y,int&w,int&h) {
    x=PADDING; y=rc.bottom-BOTTOM_H+8; w=rc.right-PADDING-x; h=10;
}
static bool InSeek(POINT pt,const RECT&rc){int x,y,w,h;SeekRect(rc,x,y,w,h);RECT r={x,y,x+w,y+h};return PtInRect(&r,pt);}
static int SeekPct(POINT pt,const RECT&rc){int x,y,w,h;SeekRect(rc,x,y,w,h);if(pt.x<x)return 0;if(pt.x>x+w)return 10000;return(int)((float)(pt.x-x)/w*10000);}

static void VolRect(int&x,int&y,int&w,int&h){RECT&vb=g_btnRects[BTN_VOLUME];x=vb.right+4;y=vb.top+2;w=min(100,(g_rcClient.right-x-8));h=vb.bottom-vb.top-4;}
static bool InVol(POINT pt){int x,y,w,h;VolRect(x,y,w,h);RECT r={x,y,x+w,y+h};return PtInRect(&r,pt);}
static float VolPos(POINT pt){int x,y,w,h;VolRect(x,y,w,h);if(pt.x<x)return 0;if(pt.x>x+w)return 1.0f;return(float)(pt.x-x)/w;}

// Equalizer biquad filter computation and processing
static void CalcEQCoeffs(int sr) {
    float Q = 1.41f;
    for (int b = 0; b < EQ_NUM_BANDS; b++) {
        float w0 = 6.2831853f * g_eqFreqs[b] / sr;
        if (w0 > 3.14159f) { for(int k=0;k<5;k++)g_eqCoeffs[b][k]=0; continue; }
        float alpha = sinf(w0) / (2 * Q);
        float A = powf(10, g_eqGains[b] / 40);
        float b0=1+alpha*A, b1=-2*cosf(w0), b2=1-alpha*A;
        float a0=1+alpha/A, a1=-2*cosf(w0), a2=1-alpha/A;
        g_eqCoeffs[b][0]=b0/a0; g_eqCoeffs[b][1]=b1/a0; g_eqCoeffs[b][2]=b2/a0;
        g_eqCoeffs[b][3]=a1/a0; g_eqCoeffs[b][4]=a2/a0;
    }
    ZeroMemory(g_eqState, sizeof(g_eqState));
}
static void ApplyEQ(short *samples, int frames, int channels) {
    if (!g_eqEnabled || channels < 1) return;
    int nc = min(channels, 2);
    for (int f = 0; f < frames; f++) {
        for (int ch = 0; ch < nc; ch++) {
            float in = (float)samples[f * channels + ch];
            for (int b = 0; b < EQ_NUM_BANDS; b++) {
                BiquadState &s = g_eqState[ch][b];
                float *c = g_eqCoeffs[b];
                float out = c[0]*in + c[1]*s.x1 + c[2]*s.x2 - c[3]*s.y1 - c[4]*s.y2;
                s.x2 = s.x1; s.x1 = in; s.y2 = s.y1; s.y1 = out; in = out;
            }
            int val = (int)in;
            if (val < -32768) val = -32768;
            if (val > 32767) val = 32767;
            samples[f * channels + ch] = (short)val;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void DrawBtnShape(HDC hdc, RECT r, int shape) {
    HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT);
    HPEN old=(HPEN)SelectObject(hdc,hp);
    HBRUSH hb=CreateSolidBrush(COL_TEXT);
    HBRUSH ob=(HBRUSH)SelectObject(hdc,hb);

    int cx=(r.left+r.right)/2, cy=(r.top+r.bottom)/2;
    int hs=(r.right-r.left)/3;
    if (hs<4) hs=4;

    if (shape==0) { // play triangle (right-pointing)
        POINT pts[3]={{cx-hs/2,cy-hs},{cx-hs/2,cy+hs},{cx+hs,cy}};
        Polygon(hdc,pts,3);
    } else if (shape==1) { // pause (two vertical bars)
        int bw = hs/2;
        RECT b1 = {cx - bw - 2, cy - hs + 2, cx - 2, cy + hs - 2};
        RECT b2 = {cx + 2, cy - hs + 2, cx + bw + 2, cy + hs - 2};
        FillRect(hdc, &b1, hb); FillRect(hdc, &b2, hb);
    } else if (shape==2) { // stop (filled square)
        int s2 = hs;
        RECT sr={cx-s2,cy-s2,cx+s2,cy+s2};
        FillRect(hdc,&sr,hb);
    } else if (shape==3) { // prev (left-pointing triangle + bar)
        int third = hs/2;
        RECT bar = {cx - hs - 2, cy - hs, cx - hs, cy + hs};
        FillRect(hdc, &bar, hb);
        POINT pts[3]={{cx+third+2,cy-hs},{cx+third+2,cy+hs},{cx-hs-2,cy}};
        Polygon(hdc,pts,3);
    } else if (shape==4) { // next (right-pointing triangle + bar)
        int third = hs/2;
        RECT bar = {cx + hs, cy - hs, cx + hs + 2, cy + hs};
        FillRect(hdc, &bar, hb);
        POINT pts[3]={{cx-third-2,cy-hs},{cx-third-2,cy+hs},{cx+hs+2,cy}};
        Polygon(hdc,pts,3);
    }

    SelectObject(hdc,ob); DeleteObject(hb);
    SelectObject(hdc,old); DeleteObject(hp);
}

enum EventTheme { EVENT_NONE, EVENT_HALLOWEEN, EVENT_CHRISTMAS, EVENT_RAMADAN, EVENT_EID, EVENT_ASHURA };

static EventTheme GetCurrentEvent() {
    if (g_manualEvent >= 0 && g_manualEvent <= 4) return (EventTheme)(g_manualEvent + 1);
    SYSTEMTIME st; GetLocalTime(&st);
    int m=st.wMonth, d=st.wDay;
    if (m==10 && d>=25) return EVENT_HALLOWEEN;
    if (m==12 && d>=20) return EVENT_CHRISTMAS;
    if ((m==2 && d>=17) || (m==3 && d<=19)) return EVENT_RAMADAN;
    if (m==5 && d>=27 && d<=30) return EVENT_EID;
    if (m==6 && d>=26 && d<=28) return EVENT_ASHURA;
    return EVENT_NONE;
}

struct EventParticle { float x,y,vx,vy,size,phase; };
static std::vector<EventParticle> g_evParticles;
static EventTheme g_lastEvent = EVENT_NONE;

static void InitEventParticles(EventTheme ev) {
    g_evParticles.clear();
    RECT rc; GetClientRect(g_hWnd, &rc);
    srand((unsigned)time(NULL));
    if (ev == EVENT_CHRISTMAS) {
        for (int i = 0; i < 150; i++) {
            EventParticle p;
            p.x = (float)(rand() % rc.right);
            p.y = (float)(rand() % (rc.bottom + 50)) - 50.0f;
            p.vx = (float)(rand() % 20 - 10) * 0.02f;
            p.vy = 0.3f + (float)(rand() % 20) * 0.05f;
            p.size = 2.0f + (float)(rand() % 4);
            p.phase = (float)(rand() % 100) * 0.1f;
            g_evParticles.push_back(p);
        }
    } else if (ev == EVENT_HALLOWEEN) {
        for (int i = 0; i < 20; i++) {
            EventParticle p;
            p.x = (float)(rand() % rc.right);
            p.y = (float)(rand() % rc.bottom);
            p.vx = 0.5f + (float)(rand() % 10) * 0.2f;
            if (rand() % 2) p.vx = -p.vx;
            p.vy = 0.3f + (float)(rand() % 10) * 0.1f;
            if (rand() % 2) p.vy = -p.vy;
            p.size = 12.0f + (float)(rand() % 8);
            p.phase = (float)(rand() % 628) * 0.01f;
            g_evParticles.push_back(p);
        }
    } else if (ev == EVENT_EID || ev == EVENT_RAMADAN) {
        for (int i = 0; i < 10; i++) {
            EventParticle p;
            p.x = 30.0f + (float)(rand() % (rc.right - 60));
            p.y = 30.0f + (float)(rand() % (rc.bottom / 3));
            p.vx = 0.0f; p.vy = 0.0f;
            p.size = 14.0f + (float)(rand() % 8);
            p.phase = (float)(rand() % 628) * 0.01f;
            g_evParticles.push_back(p);
        }
    } else if (ev == EVENT_ASHURA) {
        for (int i = 0; i < 40; i++) {
            EventParticle p;
            p.x = (float)(rand() % rc.right);
            p.y = (float)(rand() % rc.bottom);
            p.vx = (float)(rand() % 10 - 5) * 0.1f;
            p.vy = -0.5f - (float)(rand() % 10) * 0.1f;
            p.size = 3.0f + (float)(rand() % 4);
            p.phase = (float)(rand() % 628) * 0.01f;
            g_evParticles.push_back(p);
        }
    }
}

static void UpdateEventParticles() {
    EventTheme ev = GetCurrentEvent();
    if (ev != g_lastEvent) { g_lastEvent = ev; InitEventParticles(ev); }
    if (g_evParticles.empty()) return;
    RECT rc; GetClientRect(g_hWnd, &rc);
    if (ev == EVENT_CHRISTMAS) {
        for (auto &p : g_evParticles) {
            p.vy += 0.015f;
            p.vx += sinf(p.phase + p.y * 0.01f) * 0.008f;
            p.x += p.vx; p.y += p.vy;
            p.phase += 0.02f;
            if (p.y > rc.bottom + 5) { p.y = -p.size; p.x = (float)(rand() % rc.right); p.vy = 0.3f; }
            if (p.x < -10) p.x = (float)(rc.right + 5);
            if (p.x > rc.right + 10) p.x = -5.0f;
        }
    } else if (ev == EVENT_HALLOWEEN) {
        for (auto &p : g_evParticles) {
            p.x += p.vx; p.y += p.vy;
            p.phase += 0.05f;
            p.vy += sinf(p.phase) * 0.01f;
            if (p.x < -30 || p.x > rc.right + 30) p.vx = -p.vx;
            if (p.y < -30 || p.y > rc.bottom + 30) p.vy = -p.vy;
        }
    } else if (ev == EVENT_EID || ev == EVENT_RAMADAN) {
        for (auto &p : g_evParticles) {
            p.phase += 0.03f;
        }
    } else if (ev == EVENT_ASHURA) {
        for (auto &p : g_evParticles) {
            p.vy -= 0.01f;
            p.vx += (float)(rand() % 10 - 5) * 0.02f;
            p.x += p.vx; p.y += p.vy;
            p.phase += 0.1f;
            if (p.y < -10) { p.y = (float)(rc.bottom + 5); p.x = (float)(rand() % rc.right); p.vy = -0.5f; }
        }
    }
}

// ---------------------------------------------------------------------------
static void DrawTitleBar(HDC hdc, const RECT &rc) {
    RECT tr={rc.left,rc.top,rc.right,rc.top+TITLE_H};
    if (g_playing && g_rainbowEnabled) {
        Gdiplus::Graphics g(hdc);
        int tw = rc.right - rc.left;
        Gdiplus::Color cols[7];
        float pos[]={0,1.0f/6,2.0f/6,3.0f/6,4.0f/6,5.0f/6,1};
        int nCols=7;
        EventTheme ev=GetCurrentEvent();
        switch(ev){
        case EVENT_HALLOWEEN:
            cols[0]=Gdiplus::Color(255,255,120,0); cols[1]=Gdiplus::Color(255,180,0,180);
            cols[2]=Gdiplus::Color(255,255,120,0); cols[3]=Gdiplus::Color(255,180,0,180);
            cols[4]=Gdiplus::Color(255,255,120,0); cols[5]=Gdiplus::Color(255,180,0,180);
            cols[6]=Gdiplus::Color(255,255,120,0);
            pos[0]=0;pos[1]=1.0f/3;pos[2]=2.0f/3;pos[3]=1; nCols=4;
            break;
        case EVENT_CHRISTMAS:
            cols[0]=Gdiplus::Color(255,255,50,50); cols[1]=Gdiplus::Color(255,0,180,0);
            cols[2]=Gdiplus::Color(255,255,255,255); cols[3]=Gdiplus::Color(255,0,180,0);
            cols[4]=Gdiplus::Color(255,255,50,50);
            pos[0]=0;pos[1]=0.3f;pos[2]=0.5f;pos[3]=0.7f;pos[4]=1; nCols=5;
            break;
        case EVENT_RAMADAN:
        case EVENT_EID:
            cols[0]=Gdiplus::Color(255,0,200,0); cols[1]=Gdiplus::Color(255,255,215,0);
            cols[2]=Gdiplus::Color(255,0,200,0);
            pos[0]=0;pos[1]=0.5f;pos[2]=1; nCols=3;
            break;
        case EVENT_ASHURA:
            cols[0]=Gdiplus::Color(255,220,0,0); cols[1]=Gdiplus::Color(255,20,20,20);
            cols[2]=Gdiplus::Color(255,220,0,0);
            pos[0]=0;pos[1]=0.5f;pos[2]=1; nCols=3;
            break;
        default:
            cols[0]=Gdiplus::Color(255,255,0,0); cols[1]=Gdiplus::Color(255,255,255,0);
            cols[2]=Gdiplus::Color(255,0,255,0); cols[3]=Gdiplus::Color(255,0,255,255);
            cols[4]=Gdiplus::Color(255,0,0,255); cols[5]=Gdiplus::Color(255,255,0,255);
            cols[6]=Gdiplus::Color(255,255,0,0);
            nCols=7;
            break;
        }
        Gdiplus::LinearGradientBrush br(Gdiplus::PointF(0,0),Gdiplus::PointF(360,0),cols[0],cols[0]);
        br.SetInterpolationColors(cols,pos,nCols);
        br.SetWrapMode(Gdiplus::WrapModeTile);
        br.TranslateTransform((float)g_rainbowTick,0);
        // Glow pass — blurred rainbow underneath
        Gdiplus::Bitmap glowBmp(tw, TITLE_H, PixelFormat32bppPARGB);
        Gdiplus::Graphics gb(&glowBmp);
        gb.FillRectangle(&br, Gdiplus::Rect(0, 0, tw, TITLE_H));
        Gdiplus::BlurParams bp = {6.0f, FALSE};
        Gdiplus::Blur blur; blur.SetParameters(&bp);
        glowBmp.ApplyEffect(&blur, NULL);
        Gdiplus::ColorMatrix cm = {
            1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,0.4f,0, 0,0,0,0,1
        };
        Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
        g.DrawImage(&glowBmp, Gdiplus::Rect(rc.left, rc.top, tw, TITLE_H),
            0, 0, tw, TITLE_H, Gdiplus::UnitPixel, &ia);
        // Sharp rainbow on top
        g.FillRectangle(&br, Gdiplus::Rect(rc.left, rc.top, tw, TITLE_H));
    } else {
        HBRUSH hb=CreateSolidBrush(COL_TITLE_BG);
        FillRect(hdc,&tr,hb); DeleteObject(hb);
    }
    SetBkMode(hdc,TRANSPARENT);
    SelectObject(hdc,g_hFontBold);
    SetTextColor(hdc,COL_TEXT_DIM);
    RECT t2={PADDING,tr.top,tr.right-100,tr.bottom};
    DrawTextW(hdc,L"LITE",-1,&t2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    // Track counter right after "LITE"
    if (g_playing && g_trackIdx >= 0 && (int)g_playlist.size() > 0) {
        wchar_t tc[32];
        int len = swprintf(tc, 32, L"  %d / %d", g_trackIdx + 1, (int)g_playlist.size());
        RECT tcr = {PADDING + 56, tr.top + 3, tr.right - 100, tr.bottom - 3};
        SelectObject(hdc, g_hFontBold);
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, tc, len, &sz);
        Gdiplus::REAL l = (Gdiplus::REAL)(PADDING + 56);
        Gdiplus::REAL t = (Gdiplus::REAL)(tr.top + 4);
        Gdiplus::REAL rgt = (Gdiplus::REAL)(PADDING + 58 + sz.cx);
        Gdiplus::REAL b = (Gdiplus::REAL)(tr.bottom - 4);
        Gdiplus::REAL rad = (b - t) / 2.0f;
        // Semi-transparent dark pill (75% opacity) using GDI+
        Gdiplus::Graphics gfx(hdc);
        Gdiplus::GraphicsPath path;
        path.AddLine(l + rad, t, rgt - rad, t);
        path.AddArc(rgt - rad * 2, t, rad * 2, rad * 2, 270.0f, 180.0f);
        path.AddLine(rgt - rad, b, l + rad, b);
        path.AddArc(l, t, rad * 2, rad * 2, 90.0f, 180.0f);
        path.CloseFigure();
        Gdiplus::SolidBrush br(Gdiplus::Color(192, 0, 0, 0));
        gfx.FillPath(&br, &path);
        SetTextColor(hdc, COL_TEXT_DIM);
        DrawTextW(hdc, tc, -1, &tcr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    for(int i=BTN_SEARCH;i<BTN_LAST;i++){
        BtnID id=(BtnID)i; RECT&br=g_btnRects[id];
        COLORREF bg = COL_TITLE_BG, tc = COL_TEXT;
        if(id==BTN_CLOSE){
            if(id==g_pressBtn) { bg=RGB(0xBF,0x0F,0x1C); tc=RGB(0xFF,0xFF,0xFF); }
            else if(id==g_hoverBtn) { bg=RGB(0xE8,0x11,0x23); tc=RGB(0xFF,0xFF,0xFF); }
        } else {
            if(id==g_pressBtn) bg=RGB(0x4A,0x4A,0x4A);
            else if(id==g_hoverBtn) bg=RGB(0x3A,0x3A,0x3A);
        }
        bool interacting = (id==g_pressBtn || id==g_hoverBtn);
        if(!g_playing || !g_rainbowEnabled || interacting) {
            HBRUSH hbb=CreateSolidBrush(bg); FillRect(hdc,&br,hbb); DeleteObject(hbb);
        }
        if(id==BTN_CLOSE){
            HPEN hp=CreatePen(PS_SOLID,1,tc);
            HPEN op=(HPEN)SelectObject(hdc,hp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            int cx=(br.left+br.right)/2, cy=(br.top+br.bottom)/2;
            MoveToEx(hdc,cx-5,cy-5,NULL); LineTo(hdc,cx+5,cy+5);
            MoveToEx(hdc,cx+5,cy-5,NULL); LineTo(hdc,cx-5,cy+5);
            SelectObject(hdc,op); DeleteObject(hp);
        } else if(id==BTN_SEARCH){
            HPEN hp=CreatePen(PS_SOLID,1,tc);
            HPEN op=(HPEN)SelectObject(hdc,hp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            int cx=(br.left+br.right)/2, cy=(br.top+br.bottom)/2;
            Ellipse(hdc,cx-5,cy-5,cx+3,cy+3);
            MoveToEx(hdc,cx+1,cy+1,NULL); LineTo(hdc,cx+6,cy+6);
            SelectObject(hdc,op); DeleteObject(hp);
        } else if(id==BTN_EQ){
            wchar_t eq[]=L"EQ";
            SetTextColor(hdc,tc); SelectObject(hdc,g_hFontBold);
            DrawTextW(hdc,eq,-1,&br,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        } else {
            HPEN hp=CreatePen(PS_SOLID,1,tc);
            HPEN op=(HPEN)SelectObject(hdc,hp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            int cx=(br.left+br.right)/2, cy=(br.top+br.bottom)/2;
            if(id==BTN_MINIMIZE){
                MoveToEx(hdc,cx-6,cy,NULL); LineTo(hdc,cx+6,cy);
            } else if(id==BTN_MAXIMIZE){
                Rectangle(hdc,cx-6,cy-5,cx+6,cy+5);
            } else {
                SelectObject(hdc, GetStockObject(DC_BRUSH));
                SetDCBrushColor(hdc, tc);
                int s=2;
                Ellipse(hdc,cx-5,cy-5,cx-5+s*2,cy-5+s*2);
                Ellipse(hdc,cx-1,cy-6,cx-1+s*2,cy-6+s*2);
                Ellipse(hdc,cx+3,cy-5,cx+3+s*2,cy-5+s*2);
                Ellipse(hdc,cx-3,cy,cx-3+s*3,cy+s*3);
            }
            SelectObject(hdc,op); DeleteObject(hp);
        }
    }
}



static void ApplyTheme() {
    if (g_useLightTheme) {
        COL_BG          = RGB(0xF0,0xF0,0xF0);
        COL_BG_DARK     = RGB(0xE0,0xE0,0xE0);
        COL_TITLE_BG    = RGB(0xE0,0xE0,0xE0);
        COL_TITLE_TEXT  = RGB(0x00,0x00,0x00);
        COL_TEXT        = RGB(0x1A,0x1A,0x1A);
        COL_TEXT_DIM    = RGB(0x77,0x77,0x77);
        COL_LIST_BG     = RGB(0xF5,0xF5,0xF5);
        COL_ART_BG      = RGB(0xDD,0xDD,0xDD);
        COL_SLIDER_BG   = RGB(0xCC,0xCC,0xCC);
        COL_SLIDER_THUMB= RGB(0x44,0x44,0x44);
    } else {
        COL_BG          = RGB(0x12,0x12,0x12);
        COL_BG_DARK     = RGB(0x1A,0x1A,0x1A);
        COL_TITLE_BG    = RGB(0x12,0x12,0x12);
        COL_TITLE_TEXT  = RGB(0xFF,0xFF,0xFF);
        COL_TEXT        = RGB(0xE0,0xE0,0xE0);
        COL_TEXT_DIM    = RGB(0x66,0x66,0x66);
        COL_LIST_BG     = RGB(0x0D,0x0D,0x0D);
        COL_ART_BG      = RGB(0x1E,0x1E,0x1E);
        COL_SLIDER_BG   = RGB(0x33,0x33,0x33);
        COL_SLIDER_THUMB= RGB(0xEE,0xEE,0xEE);
    }
}

static void DrawSliderGDI(HDC hdc, int x, int y, int w, int h, float pct);

static void DrawSettingsPanel(HDC hdc) {
    int pw = 300, ph = 560, px = 0, py = 0;
    RECT fullR={px,py,px+pw,py+ph}; HBRUSH hb = CreateSolidBrush(COL_BG_DARK); FillRect(hdc, &fullR, hb); DeleteObject(hb);
    HPEN hp = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN opOld = (HPEN)SelectObject(hdc, hp);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, px, py, px + pw, py + ph);
    SelectObject(hdc, opOld); DeleteObject(hp);
    // Title
    SelectObject(hdc, g_hFontBold); SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_TEXT);
    RECT tR = {px + 12, py + 8, px + pw - 36, py + 32};
    DrawTextW(hdc, L"Settings", -1, &tR, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // Close X
    RECT cR = {px + pw - 28, py + 8, px + pw - 8, py + 28};
    HPEN hpx = CreatePen(PS_SOLID, 1, COL_TEXT);
    HPEN opx = (HPEN)SelectObject(hdc, hpx);
    int ccx = (cR.left + cR.right) / 2, ccy = (cR.top + cR.bottom) / 2;
    MoveToEx(hdc, ccx - 4, ccy - 4, NULL); LineTo(hdc, ccx + 4, ccy + 4);
    MoveToEx(hdc, ccx + 4, ccy - 4, NULL); LineTo(hdc, ccx - 4, ccy + 4);
    SelectObject(hdc, opx); DeleteObject(hpx);
    // Separator
    HPEN hps = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN ops = (HPEN)SelectObject(hdc, hps);
    MoveToEx(hdc, px + 12, py + 36, NULL); LineTo(hdc, px + pw - 12, py + 36);
    SelectObject(hdc, ops); DeleteObject(hps);
    // Opacity row
    int oy = py + 72;
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT);
    RECT oL = {px + 12, oy, px + 90, oy + 20};
    DrawTextW(hdc, L"Opacity", -1, &oL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t pctTxt[8]; swprintf_s(pctTxt, L"%d%%", g_windowAlpha * 100 / 255);
    RECT oP = {px + pw - 50, oy, px + pw - 12, oy + 20};
    SetTextColor(hdc, COL_TEXT_DIM);
    DrawTextW(hdc, pctTxt, -1, &oP, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    int osx = px + 85, osy = oy + 22, osw = pw - 100, osh = 18;
    float opct = g_windowAlpha / 255.0f;
    int oth = 4, ots = 10, oty = osy + (osh - oth) / 2, otmy = osy + (osh - ots) / 2, otmx = osx + (int)(opct * (osw - ots));
    RECT obg = {osx, oty, osx + osw, oty + oth};
    HBRUSH obb = CreateSolidBrush(COL_SLIDER_BG); FillRect(hdc, &obg, obb); DeleteObject(obb);
    if (opct > 0) { RECT ofl = {osx, oty, otmx + ots / 2, oty + oth}; HBRUSH obf = CreateSolidBrush(g_sliderFill); FillRect(hdc, &ofl, obf); DeleteObject(obf); }
    HBRUSH obt = CreateSolidBrush(COL_SLIDER_THUMB);
    RECT otR = {otmx, otmy, otmx + ots, otmy + ots};
    FillRect(hdc, &otR, obt); DeleteObject(obt);
    // Accent Color label
    int ay = py + 108;
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT);
    RECT aL = {px + 12, ay, px + pw - 12, ay + 20};
    DrawTextW(hdc, L"Accent Color", -1, &aL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // Color swatches: 4 rows x 4 cols
    int swSize = 22, swGap = 6, swStartX = px + 16, swStartY = ay + 24;
    for (int i = 0; i < g_accentColorCount; i++) {
        int sx = swStartX + (i % 4) * (swSize + swGap);
        int sy = swStartY + (i / 4) * (swSize + swGap);
        RECT swR = {sx, sy, sx + swSize, sy + swSize};
        HBRUSH swb = CreateSolidBrush(g_accentColors[i]); FillRect(hdc, &swR, swb); DeleteObject(swb);
        int sel = (g_accentColor == g_accentColors[i]) ? 1 : 0;
        HPEN swp = CreatePen(PS_SOLID, sel ? 2 : 1, sel ? COL_TEXT : COL_TEXT_DIM);
        HPEN opsw = (HPEN)SelectObject(hdc, swp);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, sx - sel, sy - sel, sx + swSize + sel, sy + swSize + sel);
        SelectObject(hdc, opsw); DeleteObject(swp);
    }
    // Custom color button
    RECT custR = {swStartX + 4*(swSize+swGap), swStartY + 2*(swSize+swGap), swStartX + 4*(swSize+swGap) + swSize, swStartY + 2*(swSize+swGap) + swSize};
    HBRUSH custB = CreateSolidBrush(COL_BG);
    FillRect(hdc, &custR, custB); DeleteObject(custB);
    HPEN custP = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN ocp = (HPEN)SelectObject(hdc, custP);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, custR.left, custR.top, custR.right, custR.bottom);
    SelectObject(hdc, ocp); DeleteObject(custP);
    SetTextColor(hdc, COL_TEXT_DIM); SelectObject(hdc, g_hFont);
    DrawTextW(hdc, L"...", -1, &custR, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    g_customBtnRect = custR;
    // Theme row (pushed down after 4 color rows)
    int thY = swStartY + 4 * (swSize + swGap) + 10;
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT);
    RECT thL = {px + 12, thY, px + pw - 12, thY + 20};
    DrawTextW(hdc, L"Theme", -1, &thL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int btnW = 90, btnH = 24, btnGap = 8, btnY = thY + 24;
    RECT darkR = {px + 16, btnY, px + 16 + btnW, btnY + btnH};
    RECT lightR = {px + 16 + btnW + btnGap, btnY, px + 16 + btnW * 2 + btnGap, btnY + btnH};
    HBRUSH darkB = CreateSolidBrush(g_useLightTheme ? COL_BG : g_accentColor);
    HBRUSH lightB = CreateSolidBrush(g_useLightTheme ? g_accentColor : COL_BG);
    FillRect(hdc, &darkR, darkB); DeleteObject(darkB);
    FillRect(hdc, &lightR, lightB); DeleteObject(lightB);
    HPEN btnPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN obtn = (HPEN)SelectObject(hdc, btnPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, darkR.left, darkR.top, darkR.right, darkR.bottom);
    Rectangle(hdc, lightR.left, lightR.top, lightR.right, lightR.bottom);
    SelectObject(hdc, obtn); DeleteObject(btnPen);
    // Active button → accent fill + white text; inactive → COL_TEXT_DIM text
    SetTextColor(hdc, g_useLightTheme ? COL_TEXT_DIM : COL_TITLE_TEXT);
    RECT dT = darkR; dT.left += 4;
    DrawTextW(hdc, L"Dark", -1, &dT, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(hdc, g_useLightTheme ? COL_TITLE_TEXT : COL_TEXT_DIM);
    RECT lT = lightR; lT.left += 4;
    DrawTextW(hdc, L"Light", -1, &lT, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // Rainbow Title toggle
    int rbY = thY + 54;
    RECT rbBox = {px + 16, rbY, px + 30, rbY + 14};
    HPEN rbPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN orb = (HPEN)SelectObject(hdc, rbPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rbBox.left, rbBox.top, rbBox.right, rbBox.bottom);
    SelectObject(hdc, orb); DeleteObject(rbPen);
    if (g_rainbowEnabled) {
        HPEN rChk = CreatePen(PS_SOLID, 2, g_accentColor);
        HPEN orChk = (HPEN)SelectObject(hdc, rChk);
        MoveToEx(hdc, rbBox.left + 2, rbBox.top + 7, NULL);
        LineTo(hdc, rbBox.left + 5, rbBox.top + 11);
        LineTo(hdc, rbBox.left + 11, rbBox.top + 3);
        SelectObject(hdc, orChk); DeleteObject(rChk);
    }
    SetTextColor(hdc, COL_TEXT);
    RECT rbL = {px + 34, rbY - 2, px + pw - 12, rbY + 16};
    SelectObject(hdc, g_hFont);
    DrawTextW(hdc, L"Rainbow Title", -1, &rbL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // Acrylic Blur toggle
    int abY = rbY + 24;
    RECT abBox = {px + 16, abY, px + 30, abY + 14};
    HPEN abPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN oab = (HPEN)SelectObject(hdc, abPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, abBox.left, abBox.top, abBox.right, abBox.bottom);
    SelectObject(hdc, oab); DeleteObject(abPen);
    if (g_acrylicBlur) {
        HPEN aChk = CreatePen(PS_SOLID, 2, g_accentColor);
        HPEN oaChk = (HPEN)SelectObject(hdc, aChk);
        MoveToEx(hdc, abBox.left + 2, abBox.top + 7, NULL);
        LineTo(hdc, abBox.left + 5, abBox.top + 11);
        LineTo(hdc, abBox.left + 11, abBox.top + 3);
        SelectObject(hdc, oaChk); DeleteObject(aChk);
    }
    SetTextColor(hdc, COL_TEXT);
    RECT abL = {px + 34, abY - 2, px + pw - 12, abY + 16};
    SelectObject(hdc, g_hFont);
    DrawTextW(hdc, L"Acrylic Blur", -1, &abL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // Blur intensity slider (only visible when blur is on)
    if (g_acrylicBlur) {
        int slY = abY + 22, slH = 8, slW = pw - 40, slX = px + 20;
        g_blurSliderRect = {slX, slY, slX + slW, slY + slH};
        DrawSliderGDI(hdc, slX, slY, slW, slH, g_blurIntensity / 100.0f);
        wchar_t lbl[16]; swprintf(lbl, 16, L"Intensity: %d%%", g_blurIntensity);
        SetTextColor(hdc, COL_TEXT_DIM);
        SelectObject(hdc, g_hFont);
        RECT slLbl = {slX, slY + slH + 2, slX + slW, slY + slH + 16};
        DrawTextW(hdc, lbl, -1, &slLbl, DT_CENTER|DT_TOP|DT_SINGLELINE);
    } else g_blurSliderRect = {};
    // Audio Output device dropdown
    int devY = abY + 30, ddBtnH = 22;
    RECT devH = {px + 12, devY, px + pw - 12, devY + 20};
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT);
    DrawTextW(hdc, L"Audio Output", -1, &devH, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT ddR = {px + 16, devY + 22, px + pw - 16, devY + 22 + ddBtnH};
    HBRUSH ddBg = CreateSolidBrush(COL_BG);
    FillRect(hdc, &ddR, ddBg); DeleteObject(ddBg);
    HPEN ddPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN oddp = (HPEN)SelectObject(hdc, ddPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, ddR.left, ddR.top, ddR.right, ddR.bottom);
    SelectObject(hdc, oddp); DeleteObject(ddPen);
    // Show friendly name matching stored device ID
    std::wstring curDev = L"Default";
    if (!g_audioDeviceId.empty()) {
        for (size_t i = 0; i < g_audioDeviceIds.size(); i++) {
            if (g_audioDeviceId == g_audioDeviceIds[i]) { curDev = g_audioDeviceNames[i]; break; }
        }
    }
    RECT curR = ddR; curR.left += 6; curR.right -= 20;
    SetTextColor(hdc, COL_TEXT);
    SelectObject(hdc, g_hFont);
    DrawTextW(hdc, curDev.c_str(), -1, &curR, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    // Dropdown arrow
    int arrX = ddR.right + 6, arrYc = (ddR.top + ddR.bottom) / 2;
    POINT arrPts[3] = {{arrX - 5, arrYc - 3}, {arrX + 5, arrYc - 3}, {arrX, arrYc + 3}};
    HPEN aPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN oap = (HPEN)SelectObject(hdc, aPen);
    HBRUSH aBr2 = CreateSolidBrush(COL_TEXT_DIM);
    HBRUSH oab2 = (HBRUSH)SelectObject(hdc, aBr2);
    Polygon(hdc, arrPts, 3);
    SelectObject(hdc, oab2); DeleteObject(aBr2);
    SelectObject(hdc, oap); DeleteObject(aPen);
    g_audioDdBtn = ddR;

    // Cache section
    int cacheY = devY + 52;
    HPEN caSep = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN ocaSep = (HPEN)SelectObject(hdc, caSep);
    MoveToEx(hdc, px + 12, cacheY, NULL); LineTo(hdc, px + pw - 12, cacheY);
    SelectObject(hdc, ocaSep); DeleteObject(caSep);
    SelectObject(hdc, g_hFontBold);
    SetTextColor(hdc, COL_TEXT);
    RECT caL = {px + 12, cacheY + 6, px + pw - 12, cacheY + 26};
    DrawTextW(hdc, L"Cache", -1, &caL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // "Load All" button — green accent, white text centered
    int laY = cacheY + 28, laH = 26;
    RECT laBtn = {px + 16, laY, px + pw - 16, laY + laH};
    g_loadAllBtnRect = laBtn;
    HBRUSH laBg = CreateSolidBrush(g_accentColor);
    FillRect(hdc, &laBtn, laBg); DeleteObject(laBg);
    SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
    SelectObject(hdc, g_hFontBold);
    RECT laTxt = laBtn; laTxt.left += 4; laTxt.right -= 4;
    DrawTextW(hdc, L"Load All", -1, &laTxt, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    // Clear Cache button — black bg, red border, red "Clear" text
    int cbY = laY + laH + 6, cbH = 26;
    RECT cacheBtn = {px + 16, cbY, px + pw - 16, cbY + cbH};
    g_cacheBtnRect = cacheBtn;
    HBRUSH cbBg = CreateSolidBrush(RGB(0x05, 0x05, 0x05));
    FillRect(hdc, &cacheBtn, cbBg); DeleteObject(cbBg);
    HPEN cbPen = CreatePen(PS_SOLID, 1, RGB(0xBB, 0x22, 0x22));
    HPEN ocbPen = (HPEN)SelectObject(hdc, cbPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cacheBtn.left, cacheBtn.top, cacheBtn.right, cacheBtn.bottom);
    SelectObject(hdc, ocbPen); DeleteObject(cbPen);
    // Cache size in gray on left
    __int64 cBytes = GetTotalCacheSize();
    wchar_t cbStr[32];
    if (cBytes >= 1024 * 1024) swprintf(cbStr, 32, L"%.1f MB", cBytes / (1024.0 * 1024.0));
    else if (cBytes >= 1024) swprintf(cbStr, 32, L"%.1f KB", cBytes / 1024.0);
    else swprintf(cbStr, 32, L"%lld B", cBytes);
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT_DIM);
    RECT szR = cacheBtn; szR.left += 8; szR.right -= 76;
    DrawTextW(hdc, cbStr, -1, &szR, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    // "Clear" in red on right
    SetTextColor(hdc, RGB(0xFF, 0x33, 0x33));
    RECT clrR = cacheBtn; clrR.right -= 8; clrR.left = clrR.right - 64;
    DrawTextW(hdc, L"Clear", -1, &clrR, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    // About section (pushed down)
    int aboutY = cbY + 40;
    HPEN abSep = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
    HPEN oabSep = (HPEN)SelectObject(hdc, abSep);
    MoveToEx(hdc, px + 12, aboutY, NULL); LineTo(hdc, px + pw - 12, aboutY);
    SelectObject(hdc, oabSep); DeleteObject(abSep);
    SelectObject(hdc, g_hFontBold);
    SetTextColor(hdc, COL_TEXT);
    RECT aboutL = {px + 12, aboutY + 6, px + pw - 12, aboutY + 26};
    DrawTextW(hdc, L"About", -1, &aboutL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, COL_TEXT_DIM);
    RECT userL = {px + 12, aboutY + 28, px + pw - 12, aboutY + 46};
    DrawTextW(hdc, L"SAIBORC", -1, &userL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
}

static void DrawSliderGDI(HDC hdc, int x, int y, int w, int h, float pct) {
    if(pct<0)pct=0;if(pct>1)pct=1;
    int th=4, ts=12, ty=y+(h-th)/2, tmy=y+(h-ts)/2, tmx=x+(int)(pct*(w-ts));
    RECT bg={x,ty,x+w,ty+th}; HBRUSH hbb=CreateSolidBrush(COL_SLIDER_BG); FillRect(hdc,&bg,hbb); DeleteObject(hbb);
    if(pct>0){RECT fl={x,ty,tmx+ts/2,ty+th};HBRUSH hbf=CreateSolidBrush(g_sliderFill);FillRect(hdc,&fl,hbf);DeleteObject(hbf);}
    HBRUSH hbt=CreateSolidBrush(COL_SLIDER_THUMB);
    RECT tr={tmx,tmy,tmx+ts,tmy+ts};
    RoundRect(hdc,tr.left,tr.top,tr.right,tr.bottom,4,4);
    FillRect(hdc,&tr,hbt); DeleteObject(hbt);
}

static void DrawSpeakerIcon(HDC hdc, int x, int y, int sz, int vol, bool muted) {
    HPEN hp=CreatePen(PS_SOLID, max(1,sz/10), COL_TEXT);
    HPEN old=(HPEN)SelectObject(hdc,hp);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    int cx=x+sz/2, cy=y+sz/2;
    int bw=sz/5, bh=sz*3/5, by=cy-bh/2, bx=x+sz/8;
    Rectangle(hdc,bx,by,bx+bw,by+bh);
    int cw=sz/4, ctx=bx+bw+cw+1;
    MoveToEx(hdc,bx+bw,by,NULL); LineTo(hdc,ctx,cy); LineTo(hdc,bx+bw,by+bh);
    if(muted||vol==0){
        int pad=sz/6;
        MoveToEx(hdc,bx-pad,by-pad,NULL); LineTo(hdc,bx+bw+pad,by+bh+pad);
        MoveToEx(hdc,bx+bw+pad,by-pad,NULL); LineTo(hdc,bx-pad,by+bh+pad);
    }else{
        int waves=(vol<=333)?1:(vol<=666)?2:3;
        for(int w=0;w<waves;w++){
            int r=cw+(w+1)*sz/5;
            Arc(hdc,ctx-r,cy-r,ctx+r,cy+r,ctx,cy-r,ctx,cy+r);
        }
    }
    SelectObject(hdc,old); DeleteObject(hp);
}

static void DrawBottomBar(HDC hdc, const RECT &rc) {
    int w=rc.right-rc.left;
    RECT br={rc.left,rc.bottom-BOTTOM_H,rc.right,rc.bottom};
    HBRUSH hb=CreateSolidBrush(COL_BG_DARK); FillRect(hdc,&br,hb); DeleteObject(hb);

    // Visualizer background for bottom controls
    int bv_y = br.top + 4;
    DrawVisualizer(hdc, PADDING, bv_y, w - PADDING*2, BOTTOM_H - 12);

    int sx,sy,sw,sh; SeekRect(rc,sx,sy,sw,sh);
    float sp=(g_maxSec>0)?(float)g_curSec/g_maxSec:0;
    DrawSliderGDI(hdc,sx,sy,sw,sh,sp);

    SetBkMode(hdc,TRANSPARENT); SelectObject(hdc,g_hFontBold);
    SetTextColor(hdc, g_useLightTheme ? COL_TEXT : RGB(0xFF,0xFF,0xFF));
    wchar_t cs[16],ms[16];
    int cs_sec=(int)g_curSec, hh=cs_sec/3600,mm=(cs_sec%3600)/60,ss=cs_sec%60; swprintf(cs,16,L"%02d:%02d:%02d",hh,mm,ss);
    hh=g_maxSec/3600;mm=(g_maxSec%3600)/60;ss=g_maxSec%60; swprintf(ms,16,L"%02d:%02d:%02d",hh,mm,ss);
    SIZE csz, msz; GetTextExtentPoint32W(hdc,cs,(int)wcslen(cs),&csz); GetTextExtentPoint32W(hdc,ms,(int)wcslen(ms),&msz);
    int yt=sy+sh+1, yb=sy+sh+16;
    RECT cR={sx+4,yt,sx+csz.cx+4,yb}, mR2={sx+sw-msz.cx-4,yt,sx+sw-4,yb};
    DrawTextW(hdc,cs,-1,&cR,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawTextW(hdc,ms,-1,&mR2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    // Bottom buttons — drawn as GDI shapes
    for(int i=BTN_SHUFFLE;i<=BTN_NEXT;i++){
        BtnID id=(BtnID)i; RECT&b=g_btnRects[id];
        COLORREF bg; if(id==g_pressBtn)bg=RGB(0x3A,0x3A,0x3A);else if(id==g_hoverBtn)bg=RGB(0x5A,0x5A,0x5A);else bg=COL_BG_DARK;
        HBRUSH hbb=CreateSolidBrush(bg); FillRect(hdc,&b,hbb); DeleteObject(hbb);
        if(id==g_hoverBtn||id==g_pressBtn){
            HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM);
            HPEN old=(HPEN)SelectObject(hdc,hp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            Rectangle(hdc,b.left,b.top,b.right,b.bottom);
            SelectObject(hdc,old);DeleteObject(hp);
        }
        // Draw button shapes
        switch(id){
            case BTN_PLAY: DrawBtnShape(hdc,b,(g_playing&&!g_paused)?1:0); break;
            case BTN_STOP: DrawBtnShape(hdc,b,2); break;
            case BTN_PREV: DrawBtnShape(hdc,b,3); break;
            case BTN_NEXT: DrawBtnShape(hdc,b,4); break;
            case BTN_SHUFFLE: {
                const wchar_t *rep = L">";
                if(g_repeatMode==1) rep = L"1";
                else if(g_repeatMode==2) rep = L"R";
                SetTextColor(hdc, g_repeatMode ? g_accentColor : COL_TEXT);
                SelectObject(hdc,g_hFont);
                SetBkMode(hdc,TRANSPARENT);
                DrawTextW(hdc,rep,-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                SetTextColor(hdc,COL_TEXT);
                } break;
        }
    }

    // Volume icon + slider  (click icon to toggle mute)
    RECT&vb=g_btnRects[BTN_VOLUME];
    { COLORREF bg; if(BTN_VOLUME==g_pressBtn)bg=RGB(0x3A,0x3A,0x3A);else if(BTN_VOLUME==g_hoverBtn)bg=RGB(0x5A,0x5A,0x5A);else bg=COL_BG_DARK;
      HBRUSH hbb=CreateSolidBrush(bg); FillRect(hdc,&vb,hbb); DeleteObject(hbb);
      if(BTN_VOLUME==g_hoverBtn||BTN_VOLUME==g_pressBtn){
          HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM); HPEN old=(HPEN)SelectObject(hdc,hp);
          SelectObject(hdc,GetStockObject(NULL_BRUSH)); Rectangle(hdc,vb.left,vb.top,vb.right,vb.bottom);
          SelectObject(hdc,old); DeleteObject(hp);
      }
      DrawSpeakerIcon(hdc,vb.left+2,vb.top+2,vb.right-vb.left-4,g_muted?0:g_volume,g_muted);
    }
    int vx,vy,vw,vh; VolRect(vx,vy,vw,vh);
    DrawSliderGDI(hdc,vx,vy,vw,vh,(float)g_volume/1000.0f);

    // Settings button
    RECT&sb=g_btnRects[BTN_SETTINGS];
    COLORREF sbg; if(BTN_SETTINGS==g_pressBtn)sbg=RGB(0x3A,0x3A,0x3A);else if(BTN_SETTINGS==g_hoverBtn)sbg=RGB(0x5A,0x5A,0x5A);else sbg=COL_BG_DARK;
    HBRUSH hsb=CreateSolidBrush(sbg); FillRect(hdc,&sb,hsb); DeleteObject(hsb);
    if(BTN_SETTINGS==g_hoverBtn||BTN_SETTINGS==g_pressBtn){
        HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM);
        HPEN old=(HPEN)SelectObject(hdc,hp);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,sb.left,sb.top,sb.right,sb.bottom);
        SelectObject(hdc,old);DeleteObject(hp);
    }
    SetTextColor(hdc,COL_TEXT);
    SelectObject(hdc,g_hFont);
    DrawTextW(hdc,L"\u2699",-1,&sb,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    // Locate button
    RECT&lb=g_btnRects[BTN_LOCATE];
    COLORREF lbg; if(BTN_LOCATE==g_pressBtn)lbg=RGB(0x3A,0x3A,0x3A);else if(BTN_LOCATE==g_hoverBtn)lbg=RGB(0x5A,0x5A,0x5A);else lbg=COL_BG_DARK;
    HBRUSH hlb=CreateSolidBrush(lbg); FillRect(hdc,&lb,hlb); DeleteObject(hlb);
    if(BTN_LOCATE==g_hoverBtn||BTN_LOCATE==g_pressBtn){
        HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM);
        HPEN old=(HPEN)SelectObject(hdc,hp);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,lb.left,lb.top,lb.right,lb.bottom);
        SelectObject(hdc,old);DeleteObject(hp);
    }
    SetTextColor(hdc,g_playing?(g_trackIdx>=0?COL_TITLE_TEXT:COL_TEXT):COL_TEXT_DIM);
    SelectObject(hdc,g_hFont);
    wchar_t locTxt[16]; swprintf(locTxt,16,L"\u2295");
    DrawTextW(hdc,locTxt,-1,&lb,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void DrawEventParticles(HDC hdc) {
    EventTheme ev = GetCurrentEvent();
    if (ev == EVENT_NONE || g_evParticles.empty()) return;
    RECT cr; GetClientRect(g_hWnd, &cr);
    SetBkMode(hdc, TRANSPARENT);
    if (ev == EVENT_CHRISTMAS) {
        SelectObject(hdc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(hdc, RGB(220, 230, 255));
        for (auto &p : g_evParticles) {
            int r = (int)p.size;
            Ellipse(hdc, (int)(p.x-r), (int)(p.y-r), (int)(p.x+r), (int)(p.y+r));
        }
    } else if (ev == EVENT_HALLOWEEN) {
        HPEN hp = CreatePen(PS_SOLID, 1, RGB(40, 30, 50));
        HPEN op = (HPEN)SelectObject(hdc, hp);
        SelectObject(hdc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(hdc, RGB(40, 30, 50));
        for (auto &p : g_evParticles) {
            int cx = (int)p.x, cy = (int)p.y, s = (int)p.size;
            float w = sinf(p.phase) * s * 0.4f;
            POINT wing[4] = {{cx,cy},{cx-s/2-(int)w,cy-s/2},{cx,cy-s/4},{cx+s/2+(int)w,cy-s/2}};
            Polygon(hdc, wing, 4);
            Ellipse(hdc, cx-2, cy-2, cx+2, cy+2);
        }
        SelectObject(hdc, op); DeleteObject(hp);
    } else if (ev == EVENT_EID || ev == EVENT_RAMADAN) {
        for (auto &p : g_evParticles) {
            int cx = (int)p.x, cy = (int)p.y, s = (int)p.size;
            float gl = 0.6f + 0.4f * sinf(p.phase);
            // glow ring
            HPEN gp = CreatePen(PS_SOLID, 2, RGB((int)(255*gl),(int)(200*gl),50));
            HPEN og = (HPEN)SelectObject(hdc, gp);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, cx-s-4, cy-s/2-4, cx+s+4, cy+s/2+4);
            // body
            HBRUSH lb = CreateSolidBrush(RGB((int)(220*gl),(int)(180*gl),40));
            HBRUSH ol = (HBRUSH)SelectObject(hdc, lb);
            Ellipse(hdc, cx-s, cy-s/2, cx+s, cy+s/2);
            SelectObject(hdc, ol); DeleteObject(lb);
            // top cap
            MoveToEx(hdc, cx-s/2, cy-s/2, NULL);
            LineTo(hdc, cx+s/2, cy-s/2);
            // bottom tassel
            MoveToEx(hdc, cx, cy+s/2, NULL);
            LineTo(hdc, cx, cy+s/2+4);
            SelectObject(hdc, og); DeleteObject(gp);
        }
    } else if (ev == EVENT_ASHURA) {
        HPEN fp = CreatePen(PS_SOLID, 1, RGB(200, 0, 0));
        HPEN of = (HPEN)SelectObject(hdc, fp);
        SelectObject(hdc, GetStockObject(DC_BRUSH));
        for (auto &p : g_evParticles) {
            int cx = (int)p.x, cy = (int)p.y, r = (int)p.size;
            SetDCBrushColor(hdc, RGB(200-(int)(p.phase*20)%100, 0, 0));
            Ellipse(hdc, cx-r, cy-r, cx+r, cy+r);
        }
        SelectObject(hdc, of); DeleteObject(fp);
    }
}

static void DrawGlassOverlay(HDC hdc, const RECT &rc) {
    if (g_useLightTheme || !g_acrylicBlur || g_blurIntensity <= 0) return;
    float gf = 1.0f - g_blurIntensity / 100.0f;
    if (gf < 0.01f) return;
    Gdiplus::Graphics gfx(hdc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    // Frosted glass white tint — stronger at glass end
    Gdiplus::SolidBrush tintBr(Gdiplus::Color((int)(20 * gf), 255, 255, 255));
    gfx.FillRectangle(&tintBr, rc.left, rc.top, w, h);
    // Radial glare spots — liquid glass lighting
    Gdiplus::SolidBrush spotBr(Gdiplus::Color((int)(15 * gf), 255, 255, 255));
    gfx.FillEllipse(&spotBr, (Gdiplus::REAL)(rc.left + w * 0.1f), (Gdiplus::REAL)(rc.top + h * 0.35f),
                    (Gdiplus::REAL)(w * 0.35f), (Gdiplus::REAL)(h * 0.2f));
    Gdiplus::SolidBrush spotBr2(Gdiplus::Color((int)(10 * gf), 255, 255, 255));
    gfx.FillEllipse(&spotBr2, (Gdiplus::REAL)(rc.left + w * 0.65f), (Gdiplus::REAL)(rc.top + h * 0.1f),
                    (Gdiplus::REAL)(w * 0.25f), (Gdiplus::REAL)(h * 0.15f));
    // Diagonal shine — soft white gradient from top-left
    Gdiplus::LinearGradientBrush shineBr(
        Gdiplus::PointF(0, 0),
        Gdiplus::PointF((Gdiplus::REAL)w * 0.6f, (Gdiplus::REAL)h * 0.4f),
        Gdiplus::Color((int)(35 * gf), 255, 255, 255),
        Gdiplus::Color(0, 255, 255, 255));
    Gdiplus::PointF shPts[4] = {
        Gdiplus::PointF(0, 0), Gdiplus::PointF((Gdiplus::REAL)w, 0),
        Gdiplus::PointF((Gdiplus::REAL)w * 0.5f, (Gdiplus::REAL)h * 0.3f), Gdiplus::PointF(0, (Gdiplus::REAL)h * 0.15f)
    };
    Gdiplus::GraphicsPath shinePath;
    shinePath.AddPolygon(shPts, 4);
    gfx.FillPath(&shineBr, &shinePath);
    // Subtle inner highlight on top edge
    Gdiplus::LinearGradientBrush edgeBr(
        Gdiplus::PointF(0, 0), Gdiplus::PointF(0, 4),
        Gdiplus::Color((int)(60 * gf), 255, 255, 255),
        Gdiplus::Color(0, 255, 255, 255));
    gfx.FillRectangle(&edgeBr, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)w, 4.0f);
}

static void DrawMainArea(HDC hdc, const RECT &rc) {
    // Blend background: lighter at glass end, dark at blur end (dark theme only)
    float blend = g_acrylicBlur ? (g_blurIntensity / 100.0f) : 1.0f;
    int bgR = GetRValue(COL_BG), bgG = GetGValue(COL_BG), bgB = GetBValue(COL_BG);
    if (!g_useLightTheme && g_acrylicBlur) {
        int bgComp = 0x12 + (int)((0x22 - 0x12) * (1.0f - blend));
        if (bgComp > 0x22) bgComp = 0x22;
        bgR = bgG = bgB = bgComp;
    }
    HBRUSH hb = CreateSolidBrush(RGB(bgR, bgG, bgB)); FillRect(hdc, &rc, hb); DeleteObject(hb);
    DrawGlassOverlay(hdc, rc);

    // Browser view: rows of [thumb] title - artist
    #define ROW_H 44
    #define THUMB_SZ 32
    int bw = (rc.right - rc.left) - PADDING*2;
    if (bw < 50) bw = 50;
    int iy = rc.top + PADDING;
    int browserArea = rc.bottom - rc.top - PADDING*2;
    int visRows = browserArea / ROW_H;
    int totalItems = g_searchBuf.empty() ? (int)g_playlist.size() : (int)g_filteredIndices.size();
    int maxSc = max(0, totalItems - visRows);
    SelectObject(hdc,g_hFont); SetBkMode(hdc,TRANSPARENT);

    // Browser scrollbar
    int sbLeft = PADDING;
    if (maxSc > 0 && totalItems > 0) {
        int sbTop = rc.top + PADDING, sbBot = rc.bottom - PADDING;
        int sbH = sbBot - sbTop;
        int thumbH = max(12, sbH * visRows / totalItems);
        int thumbY = sbTop + (sbH - thumbH) * g_browserScroll / maxSc;
        RECT sbTrack = {sbLeft, sbTop, sbLeft + 6, sbBot};
        HBRUSH hsb = CreateSolidBrush(COL_SLIDER_BG); FillRect(hdc, &sbTrack, hsb); DeleteObject(hsb);
        RECT sbThumb = {sbLeft, thumbY, sbLeft + 6, thumbY + thumbH};
        HBRUSH hst = CreateSolidBrush(COL_SLIDER_THUMB); FillRect(hdc, &sbThumb, hst); DeleteObject(hst);
    }
    int rowLeft = sbLeft + 10;

    // Search box
    const int searchH = 22;
    int searchTop = rc.top + PADDING;
    if (!g_searchBuf.empty() || g_searchFocused) {
        g_searchBox = {rowLeft, searchTop, rowLeft + bw, searchTop + searchH};
        HPEN hp=CreatePen(PS_SOLID,1,COL_SLIDER_THUMB);
        HPEN op=(HPEN)SelectObject(hdc,hp);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,g_searchBox.left,g_searchBox.top,g_searchBox.right,g_searchBox.bottom);
        SelectObject(hdc,op); DeleteObject(hp);
        RECT sr = g_searchBox; sr.left += 4;
        std::wstring disp = g_searchBuf.empty() ? L"Type to search\u2026" : g_searchBuf;
        SetTextColor(hdc, g_searchBuf.empty() ? COL_TEXT_DIM : COL_TEXT);
        DrawTextW(hdc, disp.c_str(), -1, &sr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        iy = searchTop + searchH + 4;
    } else {
        g_searchBox = {};
    }

    // Ensure metadata for visible range + buffer
    CacheMetaRange(g_browserScroll);

    // Browser loop
    for (int ri = g_browserScroll; ri < totalItems && ri < g_browserScroll + visRows + 1; ri++) {
        int i = g_searchBuf.empty() ? ri : g_filteredIndices[ri];
        int rowTop = iy, rowBot = iy + ROW_H;
        if (rowTop > rc.bottom) break;
        if (rowBot < rc.top) continue;

        // Highlight if this is the current track
        if (i == g_trackIdx) {
            RECT hr = {rowLeft, rowTop, rowLeft + bw, rowBot};
            HBRUSH hbs = CreateSolidBrush(g_accentColor); FillRect(hdc, &hr, hbs); DeleteObject(hbs);
        }

        // Thumbnail (only draw if already cached)
        RECT tr = {rowLeft, rowTop + (ROW_H - THUMB_SZ)/2, rowLeft + THUMB_SZ, rowTop + (ROW_H - THUMB_SZ)/2 + THUMB_SZ};
        if (i < (int)g_thumbs.size() && g_thumbs[i]) {
            Gdiplus::Graphics gfx(hdc);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.DrawImage(g_thumbs[i], tr.left, tr.top, THUMB_SZ, THUMB_SZ);
        } else {
            HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM);
            HPEN op=(HPEN)SelectObject(hdc,hp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            Rectangle(hdc,tr.left,tr.top,tr.right,tr.bottom);
            SelectObject(hdc,op); DeleteObject(hp);
            SetTextColor(hdc,COL_TEXT_DIM);
            RECT ntr = tr; DrawTextW(hdc, L"\u266B", -1, &ntr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }

        // Song name + artist
        std::wstring title = (i < (int)g_songTitles.size()) ? g_songTitles[i] : L"";
        if (title.empty()) {
            title = g_playlist[i];
            size_t bs = title.rfind(L'\\'); if (bs != std::wstring::npos) title = title.substr(bs + 1);
            size_t dot = title.rfind(L'.'); if (dot != std::wstring::npos) title = title.substr(0, dot);
        }
        std::wstring artist = (i < (int)g_songArtists.size()) ? g_songArtists[i] : L"";
        if (artist == L"Unknown artist") artist.clear();
        // Duration text on the right
        wchar_t durStr[16] = {};
        if (i < (int)g_songDurations.size() && g_songDurations[i] > 0) {
            int d = g_songDurations[i], dm = d / 60, ds = d % 60;
            swprintf(durStr, 16, L"%d:%02d", dm, ds);
        }
        int durW = 0;
        if (durStr[0]) { SIZE ds2; GetTextExtentPoint32W(hdc, durStr, (int)wcslen(durStr), &ds2); durW = ds2.cx + 8; }
        RECT tR = {tr.right + 8, rowTop, rowLeft + bw - durW, rowBot};
        // When blur is active, brighten text for readability
        COLORREF textCol = (g_acrylicBlur && g_blurIntensity > 20) ? RGB(0xFF,0xFF,0xFF) : (i == g_trackIdx ? COL_TITLE_TEXT : COL_TEXT);
        if (artist.empty()) {
            SetTextColor(hdc, textCol);
            SelectObject(hdc, g_hFont);
            DrawTextW(hdc, title.c_str(), -1, &tR, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        } else {
            RECT tt = tR; tt.bottom = rowTop + 24;
            SetTextColor(hdc, textCol);
            SelectObject(hdc, g_hFontBold);
            DrawTextW(hdc, title.c_str(), -1, &tt, DT_LEFT|DT_BOTTOM|DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT ta = tR; ta.top = rowTop + 24;
            SetTextColor(hdc, g_acrylicBlur && g_blurIntensity > 20 ? RGB(0xDD,0xDD,0xDD) : COL_TEXT_DIM);
            SelectObject(hdc, g_hFont);
            DrawTextW(hdc, artist.c_str(), -1, &ta, DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if (durStr[0]) {
            RECT dr = {rowLeft + bw - durW, rowTop, rowLeft + bw, rowBot};
            SetTextColor(hdc, COL_TEXT_DIM);
            SelectObject(hdc, g_hFont);
            DrawTextW(hdc, durStr, -1, &dr, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }

        iy += ROW_H;
    }
    #undef ROW_H
    #undef THUMB_SZ
}

// ---------------------------------------------------------------------------
// Settings save/load
// ---------------------------------------------------------------------------
static void SaveSettings() {
    HKEY hk = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        wchar_t dir[MAX_PATH] = {};
        if (!g_folderName.empty()) {
            wcscpy_s(dir, g_folderName.c_str());
            size_t bs = g_playlist.empty() ? std::wstring::npos : g_playlist[0].rfind(L'\\');
            if (bs != std::wstring::npos) {
                std::wstring fullDir = g_playlist[0].substr(0, bs);
                wcscpy_s(dir, fullDir.c_str());
            }
        }
        RegSetValueExW(hk, L"Folder", 0, REG_SZ, (BYTE*)dir, (DWORD)(wcslen(dir) + 1) * 2);
        DWORD v = g_volume; RegSetValueExW(hk, L"Volume", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_muted ? 1 : 0; RegSetValueExW(hk, L"Muted", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_prevVolume; RegSetValueExW(hk, L"PrevVolume", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_repeatMode; RegSetValueExW(hk, L"RepeatMode", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_trackIdx; RegSetValueExW(hk, L"TrackIdx", 0, REG_DWORD, (BYTE*)&v, 4);
        v = (DWORD)g_curSec; RegSetValueExW(hk, L"SeekPos", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_windowAlpha; RegSetValueExW(hk, L"WindowAlpha", 0, REG_DWORD, (BYTE*)&v, 4);
        v = (DWORD)g_accentColor; RegSetValueExW(hk, L"AccentColor", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_useLightTheme ? 1 : 0; RegSetValueExW(hk, L"LightTheme", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_rainbowEnabled ? 1 : 0; RegSetValueExW(hk, L"RainbowTitle", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_acrylicBlur ? 1 : 0; RegSetValueExW(hk, L"AcrylicBlur", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_blurIntensity; RegSetValueExW(hk, L"BlurIntensity", 0, REG_DWORD, (BYTE*)&v, 4);
        v = g_eqEnabled ? 1 : 0; RegSetValueExW(hk, L"EQEnabled", 0, REG_DWORD, (BYTE*)&v, 4);
        RegSetValueExW(hk, L"EQGains", 0, REG_BINARY, (BYTE*)g_eqGains, sizeof(g_eqGains));
        RegSetValueExW(hk, L"AudioDevice", 0, REG_SZ, (BYTE*)g_audioDeviceId.c_str(), (DWORD)(g_audioDeviceId.size() + 1) * 2);
        RECT wr; GetWindowRect(g_hWnd, &wr);
        DWORD data[4] = { (DWORD)wr.left, (DWORD)wr.top, (DWORD)(wr.right - wr.left), (DWORD)(wr.bottom - wr.top) };
        RegSetValueExW(hk, L"WindowRect", 0, REG_BINARY, (BYTE*)data, 16);
        RegCloseKey(hk);
    }
}

static void LoadSettings() {
    HKEY hk = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t dir[MAX_PATH] = {};
        DWORD sz = sizeof(dir), type = 0;
        if (RegQueryValueExW(hk, L"Folder", 0, &type, (BYTE*)dir, &sz) == ERROR_SUCCESS && dir[0]) {
            g_folderName = dir;
            size_t bs = std::wstring(dir).find_last_of(L'\\');
            if (bs != std::wstring::npos) {
                std::wstring fullDir(dir);
                std::wstring folderName = fullDir.substr(bs + 1);
                WIN32_FIND_DATAW fd;
                HANDLE hf = FindFirstFileW((fullDir + L"\\*").c_str(), &fd);
                if (hf != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            std::wstring ext = PathFindExtensionW(fd.cFileName);
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                            if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".m4a" || ext == L".aac" || ext == L".ogg" || ext == L".wma")
                                g_playlist.push_back(fullDir + L"\\" + fd.cFileName);
                        }
                    } while (FindNextFileW(hf, &fd));
                    FindClose(hf);
                }
                std::sort(g_playlist.begin(), g_playlist.end());
                if (!g_playlist.empty()) {
                    g_hasFolder = true;
                    g_folderName = folderName;
                    g_songTitles.assign(g_playlist.size(), L"");
                    g_songArtists.assign(g_playlist.size(), L"");
                    g_songDurations.assign(g_playlist.size(), 0);
                    g_metaCached.assign(g_playlist.size(), false);
                    if (!LoadMetaCache())
                        CacheMetaRange(0);
                    for (auto *t : g_thumbs) if (t) delete t;
                    g_thumbs.assign(g_playlist.size(), NULL);
                }
            }
        }
        DWORD v = 0;
        sz = 4;
        if (RegQueryValueExW(hk, L"Volume", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_volume = v;
        if (RegQueryValueExW(hk, L"Muted", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_muted = (v != 0);
        if (RegQueryValueExW(hk, L"PrevVolume", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_prevVolume = v;
        if (RegQueryValueExW(hk, L"RepeatMode", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_repeatMode = v;
        if (RegQueryValueExW(hk, L"TrackIdx", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_trackIdx = v;
        if (RegQueryValueExW(hk, L"SeekPos", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_curSec = v;
        DWORD data[4] = {};
        sz = 16;
        if (RegQueryValueExW(hk, L"WindowRect", 0, NULL, (BYTE*)data, &sz) == ERROR_SUCCESS && data[2] >= 500 && data[3] >= 400) {
            SetWindowPos(g_hWnd, NULL, (int)data[0], (int)data[1], (int)data[2], (int)data[3], SWP_NOZORDER);
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"WindowAlpha", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_windowAlpha = v; if(g_windowAlpha<20)g_windowAlpha=20; if(g_windowAlpha>255)g_windowAlpha=255;
            SetLayeredWindowAttributes(g_hWnd,0,g_windowAlpha,LWA_ALPHA);
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"AccentColor", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_accentColor = v; g_sliderFill = v;
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"LightTheme", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_useLightTheme = (v != 0);
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"RainbowTitle", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_rainbowEnabled = (v != 0);
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"AcrylicBlur", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_acrylicBlur = (v != 0);
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"BlurIntensity", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_blurIntensity = v;
            if (g_blurIntensity < 1 || g_blurIntensity > 100) g_blurIntensity = 50;
        }
        sz = 4;
        if (RegQueryValueExW(hk, L"EQEnabled", 0, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            g_eqEnabled = (v != 0);
        }
        DWORD eqSz = sizeof(g_eqGains);
        if (RegQueryValueExW(hk, L"EQGains", 0, NULL, (BYTE*)g_eqGains, &eqSz) != ERROR_SUCCESS) {
            ZeroMemory(g_eqGains, sizeof(g_eqGains));
        }
        wchar_t devName[256] = {};
        sz = sizeof(devName); type = 0;
        if (RegQueryValueExW(hk, L"AudioDevice", 0, &type, (BYTE*)devName, &sz) == ERROR_SUCCESS && type == REG_SZ) {
            g_audioDeviceId = devName;
        }
        RegCloseKey(hk);
    }
    ApplyTheme();
}

static void ScrollToCurrentTrack() {
    if (!g_hasFolder || g_trackIdx < 0 || g_trackIdx >= (int)g_playlist.size()) return;
    int totalItems;
    int scrollIdx;
    if (g_searchBuf.empty()) {
        totalItems = (int)g_playlist.size();
        scrollIdx = g_trackIdx;
    } else {
        totalItems = (int)g_filteredIndices.size();
        scrollIdx = -1;
        for (int i = 0; i < totalItems; i++) {
            if (g_filteredIndices[i] == g_trackIdx) { scrollIdx = i; break; }
        }
        if (scrollIdx < 0) return;
    }
    int visRows = (g_rcClient.bottom - g_rcClient.top - TITLE_H - BOTTOM_H - PADDING*2) / 44;
    int maxSc = max(0, totalItems - visRows);
    g_browserScroll = max(0, min(scrollIdx - visRows/3, maxSc));
    InvalidateRect(g_hWnd, NULL, TRUE);
}

static void UpdateFilter() {
    g_filteredIndices.clear();
    if (g_searchBuf.empty()) return;
    for (size_t i = 0; i < g_playlist.size(); i++) {
        std::wstring n = g_playlist[i];
        size_t bs = n.rfind(L'\\'); if (bs != std::wstring::npos) n = n.substr(bs + 1);
        size_t dot = n.rfind(L'.'); if (dot != std::wstring::npos) n = n.substr(0, dot);
        if (StrStrIW(n.c_str(), g_searchBuf.c_str()))
            g_filteredIndices.push_back((int)i);
    }
    g_browserScroll = 0;
}

// ---------------------------------------------------------------------------
// Audio device enumeration
// ---------------------------------------------------------------------------
static void EnumerateAudioDevices() {
    g_audioDeviceNames.clear();
    g_audioDeviceIds.clear();
    IMMDeviceEnumerator *pEnum = NULL;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (!pEnum) return;
    IMMDeviceCollection *pColl = NULL;
    if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pColl))) {
        UINT count = 0;
        pColl->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice *pDev = NULL;
            if (SUCCEEDED(pColl->Item(i, &pDev))) {
                LPWSTR devIdStr = NULL;
                if (SUCCEEDED(pDev->GetId(&devIdStr))) {
                    IPropertyStore *pProps = NULL;
                    if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProps))) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &pv))) {
                            g_audioDeviceNames.push_back(pv.pwszVal);
                            g_audioDeviceIds.push_back(devIdStr);
                        }
                        PropVariantClear(&pv);
                        pProps->Release();
                    }
                    CoTaskMemFree(devIdStr);
                }
                pDev->Release();
            }
        }
        pColl->Release();
    }
    pEnum->Release();
}

static IMMDevice *GetAudioDevice() {
    IMMDeviceEnumerator *pEnum = NULL;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (!pEnum) return NULL;
    IMMDevice *pDev = NULL;
    if (!g_audioDeviceId.empty()) {
        // Look up by persistent device ID (reliable, survives re-enumeration)
        HRESULT hr = pEnum->GetDevice(g_audioDeviceId.c_str(), &pDev);
        if (FAILED(hr)) pDev = NULL; // device may have been disconnected
    }
    if (!pDev) {
        pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev);
    }
    pEnum->Release();
    return pDev;
}

// ---------------------------------------------------------------------------
// Settings window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool s_dragging = false;
    static POINT s_dragPt = {};
    switch(msg){
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hWnd,&ps);
        RECT rc; GetClientRect(hWnd,&rc);
        HBRUSH hbb=CreateSolidBrush(COL_BG_DARK); FillRect(hdc,&rc,hbb); DeleteObject(hbb);
        DrawSettingsPanel(hdc);
        HPEN hp=CreatePen(PS_SOLID,1,COL_TEXT_DIM);
        HPEN op=(HPEN)SelectObject(hdc,hp);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,0,0,rc.right,rc.bottom);
        SelectObject(hdc,op); DeleteObject(hp);
        EndPaint(hWnd,&ps);
        return 0;
    }
    case WM_LBUTTONDOWN:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        // Title bar drag (y 0..36)
        if(pt.y<36 && !(pt.x>=272&&pt.x<292&&pt.y>=8&&pt.y<28)){
            s_dragging=true; s_dragPt=pt; SetCapture(hWnd); return 0;
        }
        // Close X
        RECT cR={272,8,292,28};
        if(PtInRect(&cR,pt)){ShowWindow(hWnd,SW_HIDE);return 0;}
        // Opacity slider
        int osx=85, osw=200, osh=18, osy=94;
        if(pt.x>=osx&&pt.x<osx+osw&&pt.y>=osy&&pt.y<osy+osh){
            g_opacityDragX=pt.x;
            float pct=(float)(pt.x-osx)/(osw-10); if(pct<0)pct=0; if(pct>1)pct=1;
            g_windowAlpha=(int)(pct*255); if(g_windowAlpha<20)g_windowAlpha=20; if(g_windowAlpha>255)g_windowAlpha=255;
            SetLayeredWindowAttributes(g_hWnd,0,g_windowAlpha,LWA_ALPHA);
            SetCapture(hWnd); InvalidateRect(hWnd,NULL,TRUE); return 0;
        }
        // Accent color swatches (4 rows x 4 cols)
        int swStartX=16, ay=108, swSize=22, swGap=6, swStartY=ay+24;
        for(int i=0;i<g_accentColorCount;i++){
            int sx=swStartX+(i%4)*(swSize+swGap);
            int sy=swStartY+(i/4)*(swSize+swGap);
            RECT swR={sx,sy,sx+swSize,sy+swSize};
            if(PtInRect(&swR,pt)){g_accentColor=g_accentColors[i];g_sliderFill=g_accentColors[i];InvalidateRect(hWnd,NULL,TRUE); InvalidateRect(g_hWnd,NULL,TRUE); break;}
        }
        // Custom color button
        if(PtInRect(&g_customBtnRect,pt)){
            CHOOSECOLORW cc={sizeof(cc),hWnd};
            cc.rgbResult = g_accentColor;
            cc.lpCustColors = g_custColors;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if(ChooseColorW(&cc)){
                g_accentColor=cc.rgbResult; g_sliderFill=cc.rgbResult;
                InvalidateRect(hWnd,NULL,TRUE); InvalidateRect(g_hWnd,NULL,TRUE);
            }
        }
        // Theme buttons (thY=254, btnY=278)
        int btnY=278, btnW=90, btnH=24, btnGap=8;
        RECT darkR={16,btnY,16+btnW,btnY+btnH};
        RECT lightR={16+btnW+btnGap,btnY,16+btnW*2+btnGap,btnY+btnH};
        if(PtInRect(&darkR,pt)&&g_useLightTheme){g_useLightTheme=false;ApplyTheme();InvalidateRect(hWnd,NULL,TRUE); InvalidateRect(g_hWnd,NULL,TRUE);return 0;}
        if(PtInRect(&lightR,pt)&&!g_useLightTheme){g_useLightTheme=true;ApplyTheme();InvalidateRect(hWnd,NULL,TRUE); InvalidateRect(g_hWnd,NULL,TRUE);return 0;}
        // Rainbow toggle (thY=254, rbY=308)
        RECT rbBox={16,308,30,322};
        if(PtInRect(&rbBox,pt)){g_rainbowEnabled=!g_rainbowEnabled;InvalidateRect(hWnd,NULL,TRUE);InvalidateRect(g_hWnd,NULL,TRUE);return 0;}
        // Acrylic blur toggle (rbY=332)
        RECT abBox={16,332,30,346};
        if(PtInRect(&abBox,pt)){
            g_acrylicBlur=!g_acrylicBlur;
            if (!g_acrylicBlur) g_blurIntensity = 0;
            else if (g_blurIntensity == 0) g_blurIntensity = 50;
            SetAcrylicBlur(g_hWnd, g_acrylicBlur ? g_blurIntensity : 0);
            InvalidateRect(hWnd,NULL,TRUE);InvalidateRect(g_hWnd,NULL,TRUE);return 0;
        }
        // Blur intensity slider drag
        if (g_acrylicBlur && PtInRect(&g_blurSliderRect, pt)) {
            g_blurSliding = true; SetCapture(hWnd);
            float pct = (float)(pt.x - g_blurSliderRect.left) / (g_blurSliderRect.right - g_blurSliderRect.left);
            if (pct < 0) pct = 0; if (pct > 1) pct = 1;
            g_blurIntensity = (int)(pct * 100);
            if (g_blurIntensity < 1) g_blurIntensity = 1;
            if (g_blurIntensity > 100) g_blurIntensity = 100;
            SetAcrylicBlur(g_hWnd, g_blurIntensity);
            InvalidateRect(hWnd, NULL, TRUE); return 0;
        }
        // Audio output dropdown button
        if(PtInRect(&g_audioDdBtn,pt)){
            HMENU hMenu=CreatePopupMenu();
            AppendMenuW(hMenu,MF_STRING,1000,L"Default");
            for(size_t i=0;i<g_audioDeviceNames.size();i++){
                AppendMenuW(hMenu,MF_STRING,1001+(int)i,g_audioDeviceNames[i].c_str());
            }
            int chkIdx = g_audioDeviceId.empty() ? 0 : -1;
            for (size_t i = 0; i < g_audioDeviceIds.size(); i++) {
                if (g_audioDeviceId == g_audioDeviceIds[i]) { chkIdx = (int)(i + 1); break; }
            }
            if (chkIdx >= 0) CheckMenuItem(hMenu, 1000 + chkIdx, MF_BYPOSITION | MF_CHECKED);
            POINT mPt=pt; ClientToScreen(hWnd,&mPt);
            int cmd=TrackPopupMenu(hMenu,TPM_RETURNCMD|TPM_NONOTIFY,mPt.x,mPt.y,0,hWnd,NULL);
            DestroyMenu(hMenu);
            if(cmd>=1000){
                std::wstring newId = (cmd==1000) ? L"" : g_audioDeviceIds[cmd-1001];
                if(newId != g_audioDeviceId){
                    g_audioDeviceId=newId;
                    InvalidateRect(hWnd,NULL,TRUE);InvalidateRect(g_hWnd,NULL,TRUE);
                    if(g_playing){
                        int ti=g_trackIdx;
                        bool wasPaused=g_paused;
                        StopAudio();
                        if(ti>=0&&ti<(int)g_playlist.size()){PlayTrack(ti,false);g_paused=wasPaused;}
                    }
                }
            }
            return 0;
        }
        // Load All button
        if(PtInRect(&g_loadAllBtnRect, pt)){
            QueueAllMeta();
            InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(g_hWnd, NULL, TRUE);
            return 0;
        }
        // Clear Cache button
        if(PtInRect(&g_cacheBtnRect, pt)){
            DeleteAllCacheFiles();
            InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(g_hWnd, NULL, TRUE);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if(s_dragging){
            RECT wr; GetWindowRect(hWnd,&wr);
            int nx=wr.left+pt.x-s_dragPt.x, ny=wr.top+pt.y-s_dragPt.y;
            SetWindowPos(hWnd,NULL,nx,ny,0,0,SWP_NOSIZE|SWP_NOZORDER);
            return 0;
        }
        if(g_blurSliding){
            float pct=(float)(pt.x-g_blurSliderRect.left)/(g_blurSliderRect.right-g_blurSliderRect.left);
            if(pct<0)pct=0; if(pct>1)pct=1;
            g_blurIntensity=(int)(pct*100);
            if(g_blurIntensity<1)g_blurIntensity=1;
            if(g_blurIntensity>100)g_blurIntensity=100;
            SetAcrylicBlur(g_hWnd, g_blurIntensity);
            InvalidateRect(hWnd,NULL,TRUE); return 0;
        }
        if(g_opacityDragX>=0){
            int osx=85, osw=200;
            float pct=(float)(pt.x-osx)/(osw-10); if(pct<0)pct=0; if(pct>1)pct=1;
            g_windowAlpha=(int)(pct*255); if(g_windowAlpha<20)g_windowAlpha=20; if(g_windowAlpha>255)g_windowAlpha=255;
            SetLayeredWindowAttributes(g_hWnd,0,g_windowAlpha,LWA_ALPHA);
        }
        return 0;
    }
    case WM_LBUTTONUP:{
        if(s_dragging){s_dragging=false;ReleaseCapture();}
        if(g_blurSliding){g_blurSliding=false;ReleaseCapture();InvalidateRect(g_hWnd,NULL,TRUE);}
        if(g_opacityDragX>=0){g_opacityDragX=-1;ReleaseCapture();InvalidateRect(hWnd,NULL,TRUE);}
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hWnd,SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hWnd,msg,wParam,lParam);
}

// ---------------------------------------------------------------------------
// Equalizer window procedure  — fully custom drawn, matches app theme
// ---------------------------------------------------------------------------
#define EQ_W 540
#define EQ_H 360

static void CalcEqLayout(const RECT &rc, int *bandX, int &bandW, int &trackL, int &trackT, int &trackH, int &thumbS,
                         int &freqY, int &gainY) {
    bandW = 48;
    int totalW = bandW * EQ_NUM_BANDS;
    int startX = (rc.right - totalW) / 2;
    if (startX < 8) startX = 8;
    for (int i = 0; i < EQ_NUM_BANDS; i++) bandX[i] = startX + i * bandW;
    trackL = 4; trackT = 80; trackH = 200; thumbS = 12;
    freqY = 54; gainY = trackT + trackH + 6;
}

static int EqThumbY(int band, int trackT, int trackH, int thumbS) {
    float pct = (g_eqGains[band] + 12) / 24.0f; // -12..+12 → 0..1
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    return trackT + (int)((1 - pct) * (trackH - thumbS));
}
static float EqGainFromY(int y, int trackT, int trackH, int thumbS) {
    float pct = 1.0f - (float)(y - trackT) / (trackH - thumbS);
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    return pct * 24 - 12;
}

static LRESULT CALLBACK EqWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool s_dragging = false;
    static POINT s_dragPt = {};
    static int s_dragBand = -1;
    static int s_dragOffY = 0;
    static int s_bandX[EQ_NUM_BANDS];
    static int s_bandW, s_trackL, s_trackT, s_trackH, s_thumbS, s_freqY, s_gainY;
    static bool s_layoutValid = false;

    switch(msg){
    case WM_SIZE:
        s_layoutValid = false;
        break;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        if (!s_layoutValid) {
            CalcEqLayout(rc, s_bandX, s_bandW, s_trackL, s_trackT, s_trackH, s_thumbS, s_freqY, s_gainY);
            s_layoutValid = true;
        }

        // Background
        HBRUSH hbb = CreateSolidBrush(COL_BG_DARK);
        FillRect(hdc, &rc, hbb); DeleteObject(hbb);

        // Border
        HPEN hp = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
        HPEN op = (HPEN)SelectObject(hdc, hp);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        SelectObject(hdc, op); DeleteObject(hp);

        // Title
        SelectObject(hdc, g_hFontBold); SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COL_TEXT);
        RECT tR = {12, 8, rc.right - 36, 32};
        DrawTextW(hdc, L"Equalizer", -1, &tR, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        // Close X
        RECT cR = {rc.right - 28, 8, rc.right - 8, 28};
        HPEN hpx = CreatePen(PS_SOLID, 1, COL_TEXT);
        HPEN opx = (HPEN)SelectObject(hdc, hpx);
        int ccx = (cR.left + cR.right)/2, ccy = (cR.top + cR.bottom)/2;
        MoveToEx(hdc, ccx-4, ccy-4, NULL); LineTo(hdc, ccx+4, ccy+4);
        MoveToEx(hdc, ccx+4, ccy-4, NULL); LineTo(hdc, ccx-4, ccy+4);
        SelectObject(hdc, opx); DeleteObject(hpx);

        // Separator
        HPEN hps = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
        HPEN ops = (HPEN)SelectObject(hdc, hps);
        MoveToEx(hdc, 12, 36, NULL); LineTo(hdc, rc.right - 12, 36);
        SelectObject(hdc, ops); DeleteObject(hps);

        // Enable EQ toggle
        RECT eqBox = {16, 44, 28, 56};
        HPEN eqPen = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
        HPEN oeq = (HPEN)SelectObject(hdc, eqPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, eqBox.left, eqBox.top, eqBox.right, eqBox.bottom);
        SelectObject(hdc, oeq); DeleteObject(eqPen);
        if (g_eqEnabled) {
            HPEN rChk = CreatePen(PS_SOLID, 2, g_accentColor);
            HPEN orChk = (HPEN)SelectObject(hdc, rChk);
            MoveToEx(hdc, eqBox.left+2, eqBox.top+6, NULL);
            LineTo(hdc, eqBox.left+5, eqBox.top+10);
            LineTo(hdc, eqBox.left+11, eqBox.top+3);
            SelectObject(hdc, orChk); DeleteObject(rChk);
        }
        SetTextColor(hdc, COL_TEXT); SelectObject(hdc, g_hFont);
        RECT eqL = {34, 44, rc.right - 12, 58};
        DrawTextW(hdc, L"Enable EQ", -1, &eqL, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        // Frequency labels
        SelectObject(hdc, g_hFont);
        SetTextColor(hdc, COL_TEXT_DIM);
        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            wchar_t buf[16];
            if (g_eqFreqs[i] >= 1000)
                swprintf(buf, 16, L"%.0fk", g_eqFreqs[i]/1000);
            else if (g_eqFreqs[i] == (int)g_eqFreqs[i])
                swprintf(buf, 16, L"%.0f", g_eqFreqs[i]);
            else
                swprintf(buf, 16, L"%.1f", g_eqFreqs[i]);
            RECT lr = {s_bandX[i], s_freqY, s_bandX[i] + s_bandW, s_freqY + 18};
            DrawTextW(hdc, buf, -1, &lr, DT_CENTER|DT_TOP|DT_SINGLELINE);
        }

        // Slider tracks + thumbs
        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            int cx = s_bandX[i] + s_bandW/2;
            int tx = cx - s_trackL/2;

            // Track background
            RECT tr = {tx, s_trackT, tx + s_trackL, s_trackT + s_trackH};
            HBRUSH tb = CreateSolidBrush(COL_SLIDER_BG);
            FillRect(hdc, &tr, tb); DeleteObject(tb);

            // Fill from bottom to thumb position (colored fill for gain)
            int thy = EqThumbY(i, s_trackT, s_trackH, s_thumbS);
            int fillH = (s_trackT + s_trackH) - (thy + s_thumbS/2);
            if (fillH > 0) {
                RECT fr = {tx, thy + s_thumbS/2, tx + s_trackL, s_trackT + s_trackH};
                HBRUSH fb = CreateSolidBrush(g_accentColor);
                FillRect(hdc, &fr, fb); DeleteObject(fb);
            }

            // Thumb
            RECT thR = {cx - s_thumbS/2, thy, cx + s_thumbS/2, thy + s_thumbS};
            HBRUSH thb = CreateSolidBrush(COL_SLIDER_THUMB);
            FillRect(hdc, &thR, thb); DeleteObject(thb);
            HPEN thp = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
            HPEN othp = (HPEN)SelectObject(hdc, thp);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, thR.left, thR.top, thR.right, thR.bottom);
            SelectObject(hdc, othp); DeleteObject(thp);

            // Gain value below track
            wchar_t gbuf[8];
            int gv = (int)g_eqGains[i];
            if (gv > 0) swprintf(gbuf, 8, L"+%d", gv);
            else swprintf(gbuf, 8, L"%d", gv);
            SetTextColor(hdc, gv == 0 ? COL_TEXT_DIM : (gv > 0 ? RGB(0x66,0xCC,0x66) : RGB(0xCC,0x66,0x66)));
            RECT gr = {s_bandX[i], s_gainY, s_bandX[i] + s_bandW, s_gainY + 18};
            DrawTextW(hdc, gbuf, -1, &gr, DT_CENTER|DT_TOP|DT_SINGLELINE);
        }

        // Preset buttons
        SelectObject(hdc, g_hFont);
        const wchar_t *presets[] = {L"Flat", L"Rock", L"Pop", L"Jazz", L"Classical", L"Bass", L"Voice"};
        int pCount = 7;
        int pBtnW = (rc.right - 24 - (pCount-1)*4) / pCount;
        if (pBtnW < 40) pBtnW = 40;
        int pTotal = pBtnW * pCount + (pCount-1)*4;
        int pStart = (rc.right - pTotal) / 2;
        if (pStart < 12) pStart = 12;
        int pY = rc.bottom - 34;
        for (int i = 0; i < pCount; i++) {
            RECT pR = {pStart + i*(pBtnW+4), pY, pStart + i*(pBtnW+4) + pBtnW, pY + 24};
            HBRUSH pb = CreateSolidBrush(COL_BG);
            FillRect(hdc, &pR, pb); DeleteObject(pb);
            HPEN pp = CreatePen(PS_SOLID, 1, COL_TEXT_DIM);
            HPEN opp = (HPEN)SelectObject(hdc, pp);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, pR.left, pR.top, pR.right, pR.bottom);
            SelectObject(hdc, opp); DeleteObject(pp);
            SetTextColor(hdc, COL_TEXT);
            RECT ptR = pR; ptR.left += 2;
            DrawTextW(hdc, presets[i], -1, &ptR, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:{
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rc; GetClientRect(hWnd, &rc);
        if (!s_layoutValid) {
            CalcEqLayout(rc, s_bandX, s_bandW, s_trackL, s_trackT, s_trackH, s_thumbS, s_freqY, s_gainY);
            s_layoutValid = true;
        }

        // Title bar drag
        RECT titleR = {0, 0, rc.right, 36};
        RECT cR = {rc.right - 28, 8, rc.right - 8, 28};
        if (pt.y < 36 && !PtInRect(&cR, pt)) {
            s_dragging = true; s_dragPt = pt; SetCapture(hWnd); return 0;
        }

        // Close X
        if (PtInRect(&cR, pt)) { ShowWindow(hWnd, SW_HIDE); return 0; }

        // Enable EQ toggle
        RECT eqBox = {16, 44, 28, 56};
        if (PtInRect(&eqBox, pt)) {
            g_eqEnabled = !g_eqEnabled;
            if (g_eqEnabled && g_playing) CalcEQCoeffs(g_eqSampleRate);
            InvalidateRect(hWnd, NULL, FALSE);
            InvalidateRect(g_hWnd, NULL, FALSE);
            return 0;
        }

        // Slider thumbs
        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            int cx = s_bandX[i] + s_bandW/2;
            int thy = EqThumbY(i, s_trackT, s_trackH, s_thumbS);
            RECT thR = {cx - s_thumbS/2 - 2, thy - 2, cx + s_thumbS/2 + 2, thy + s_thumbS + 2};
            if (PtInRect(&thR, pt)) {
                s_dragBand = i;
                s_dragOffY = pt.y - thy;
                SetCapture(hWnd);
                return 0;
            }
        }
        // Click on track (not on thumb) — jump to position
        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            int cx = s_bandX[i] + s_bandW/2;
            RECT bandR = {cx - s_bandW/2, s_trackT, cx + s_bandW/2, s_trackT + s_trackH};
            if (pt.x >= bandR.left && pt.x < bandR.right && pt.y >= bandR.top && pt.y < bandR.bottom) {
                g_eqGains[i] = EqGainFromY(pt.y - s_thumbS/2, s_trackT, s_trackH, s_thumbS);
                if (g_playing) CalcEQCoeffs(g_eqSampleRate);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // Preset buttons
        const wchar_t *presets[] = {L"Flat", L"Rock", L"Pop", L"Jazz", L"Classical", L"Bass", L"Voice"};
        int pCount = 7;
        int pBtnW = (rc.right - 24 - (pCount-1)*4) / pCount;
        if (pBtnW < 40) pBtnW = 40;
        int pTotal = pBtnW * pCount + (pCount-1)*4;
        int pStart = (rc.right - pTotal) / 2;
        if (pStart < 12) pStart = 12;
        int pY = rc.bottom - 34;
        for (int i = 0; i < pCount; i++) {
            RECT pR = {pStart + i*(pBtnW+4), pY, pStart + i*(pBtnW+4) + pBtnW, pY + 24};
            if (PtInRect(&pR, pt)) {
                static const float presetFlat[EQ_NUM_BANDS]={};
                static const float presetRock[EQ_NUM_BANDS]={4,4,2,0,0,0,1,2,3,3};
                static const float presetPop[EQ_NUM_BANDS]={2,3,4,3,0,-1,0,2,3,2};
                static const float presetJazz[EQ_NUM_BANDS]={3,3,2,1,0,0,1,2,3,4};
                static const float presetClassical[EQ_NUM_BANDS]={4,3,2,1,0,-1,0,1,3,4};
                static const float presetBass[EQ_NUM_BANDS]={6,5,4,2,0,0,0,0,0,0};
                static const float presetVoice[EQ_NUM_BANDS]={-2,-1,0,1,3,4,3,1,-1,-2};
                static const float*pd[]={presetFlat,presetRock,presetPop,presetJazz,presetClassical,presetBass,presetVoice};
                memcpy(g_eqGains, pd[i], sizeof(g_eqGains));
                if (g_playing) CalcEQCoeffs(g_eqSampleRate);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:{
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (s_dragging) {
            RECT wr; GetWindowRect(hWnd, &wr);
            int nx = wr.left + pt.x - s_dragPt.x, ny = wr.top + pt.y - s_dragPt.y;
            SetWindowPos(hWnd, NULL, nx, ny, 0, 0, SWP_NOSIZE|SWP_NOZORDER);
            return 0;
        }
        if (s_dragBand >= 0) {
            RECT rc; GetClientRect(hWnd, &rc);
            if (!s_layoutValid) {
                CalcEqLayout(rc, s_bandX, s_bandW, s_trackL, s_trackT, s_trackH, s_thumbS, s_freqY, s_gainY);
                s_layoutValid = true;
            }
            int ny = pt.y - s_dragOffY;
            if (ny < s_trackT) ny = s_trackT;
            if (ny > s_trackT + s_trackH - s_thumbS) ny = s_trackT + s_trackH - s_thumbS;
            g_eqGains[s_dragBand] = EqGainFromY(ny, s_trackT, s_trackH, s_thumbS);
            if (g_playing) CalcEQCoeffs(g_eqSampleRate);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP:{
        if (s_dragging) { s_dragging = false; ReleaseCapture(); }
        if (s_dragBand >= 0) { s_dragBand = -1; ReleaseCapture(); }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static void DrawCompactMode(HDC hdc, const RECT &rc);
extern RECT g_compBtns[5];
extern RECT g_compCatR;
static const int COMPACT_W = 300, COMPACT_H = 130;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg){
    case WM_CREATE:
        g_hWnd=hWnd;
        DragAcceptFiles(hWnd, TRUE);
        break;

    case WM_ERASEBKGND:
        return TRUE; // we draw everything in WM_PAINT

    case WM_DROPFILES:{
        HDROP hDrop = (HDROP)wParam;
        wchar_t buf[MAX_PATH];
        int count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for(int i=0;i<count;i++){
            DragQueryFileW(hDrop, i, buf, MAX_PATH);
            wchar_t *ext = wcsrchr(buf, L'.');
            if(ext){
                for(wchar_t *p=ext;*p;p++) *p=towlower(*p);
                if(wcscmp(ext,L".mp3")==0||wcscmp(ext,L".wav")==0||wcscmp(ext,L".flac")==0||
                   wcscmp(ext,L".ogg")==0||wcscmp(ext,L".aac")==0||wcscmp(ext,L".m4a")==0||
                   wcscmp(ext,L".wma")==0||wcscmp(ext,L".opus")==0){
                    g_playlist.push_back(buf);
                    g_songTitles.push_back(L"");
                    g_songArtists.push_back(L"");
                    g_songDurations.push_back(0);
                    g_metaCached.push_back(false);
                    g_thumbs.push_back(NULL);
                }
            }
        }
        DragFinish(hDrop);
        CacheMetaRange((int)g_playlist.size() - count / 2);
        if(count>0&&!g_playing) PlayTrack(0,false);
        else InvalidateRect(hWnd,NULL,TRUE);
        break;
    }

    case WM_DESTROY:
        g_metaThreadRun = false;
        if (g_hMetaWakeEvent) SetEvent(g_hMetaWakeEvent);
        if (g_hMetaThread) {
            if (WaitForSingleObject(g_hMetaThread, 500) != WAIT_OBJECT_0)
                TerminateThread(g_hMetaThread, 0);
            CloseHandle(g_hMetaThread); g_hMetaThread = NULL;
        }
        if (g_hMetaWakeEvent) { CloseHandle(g_hMetaWakeEvent); g_hMetaWakeEvent = NULL; }
        DeleteCriticalSection(&g_metaCS);
        RemoveTrayIcon();
        SaveSettings();
        if(g_hEqWnd){DestroyWindow(g_hEqWnd);g_hEqWnd=NULL;}
        if(g_hSettingsWnd){DestroyWindow(g_hSettingsWnd);g_hSettingsWnd=NULL;}
        if(g_hAudioThread){g_audioRun=false;
            if(WaitForSingleObject(g_hAudioThread,200)!=WAIT_OBJECT_0) TerminateThread(g_hAudioThread,0);
            CloseHandle(g_hAudioThread);g_hAudioThread=NULL;}
        if(g_pSimpleVol){g_pSimpleVol->Release();g_pSimpleVol=NULL;}
        if(g_pRenderClient){g_pRenderClient->Release();g_pRenderClient=NULL;}
        if(g_pAudioClient){g_pAudioClient->Stop();g_pAudioClient->Release();g_pAudioClient=NULL;}
        if(g_pReader){g_pReader->Release();g_pReader=NULL;}
        if(g_albumArt) delete g_albumArt;
        PostQuitMessage(0);
        break;

    case WM_SIZE:
        GetClientRect(hWnd,&g_rcClient);
        CalcLayout(g_rcClient);
        InvalidateRect(hWnd,NULL,TRUE);
        break;

    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hWnd,&ps);
        if(g_compactMode){
            HDC mDC=CreateCompatibleDC(hdc);
            HBITMAP mBM=CreateCompatibleBitmap(hdc,g_rcClient.right,g_rcClient.bottom);
            HBITMAP oBM=(HBITMAP)SelectObject(mDC,mBM);
            RECT fr={0,0,g_rcClient.right,g_rcClient.bottom};
            HBRUSH fbr=CreateSolidBrush(COL_BG); FillRect(mDC,&fr,fbr); DeleteObject(fbr);
            DrawCompactMode(mDC, fr);
            BitBlt(hdc,0,0,g_rcClient.right,g_rcClient.bottom,mDC,0,0,SRCCOPY);
            SelectObject(mDC,oBM); DeleteObject(mBM); DeleteDC(mDC);
            EndPaint(hWnd, &ps);
            break;
        }
        HDC mDC=CreateCompatibleDC(hdc);
        HBITMAP mBM=CreateCompatibleBitmap(hdc,g_rcClient.right,g_rcClient.bottom);
        HBITMAP oBM=(HBITMAP)SelectObject(mDC,mBM);

        // Fill background
        RECT fr={0,0,g_rcClient.right,g_rcClient.bottom};
        HBRUSH fbr=CreateSolidBrush(COL_BG); FillRect(mDC,&fr,fbr); DeleteObject(fbr);

        DrawTitleBar(mDC,g_rcClient);
        RECT mRc=g_rcClient; mRc.top+=TITLE_H; mRc.bottom-=BOTTOM_H;
        DrawMainArea(mDC,mRc);
        DrawBottomBar(mDC,g_rcClient);
        DrawEventParticles(mDC);

        // Tooltip for hovered button
        if (g_hoverBtn != BTN_NONE && g_btnTooltips[g_hoverBtn]) {
            SelectObject(mDC, g_hFont);
            RECT &br = g_btnRects[g_hoverBtn];
            int mx = (br.left + br.right) / 2, my = br.top;
            DrawTooltip(mDC, g_btnTooltips[g_hoverBtn], mx, my);
        }

        BitBlt(hdc,0,0,g_rcClient.right,g_rcClient.bottom,mDC,0,0,SRCCOPY);
        SelectObject(mDC,oBM); DeleteObject(mBM); DeleteDC(mDC);
        EndPaint(hWnd,&ps);
        break;
    }

    case WM_CHAR:{
        if (!g_hasFolder || g_playlist.empty()) break;
        DWORD now = GetTickCount();
        g_searchTick = now;
        g_searchFocused = true;
        if (wParam == VK_BACK) {
            if (!g_searchBuf.empty()) g_searchBuf.pop_back();
        } else if (wParam == VK_ESCAPE) {
            g_searchBuf.clear(); g_filteredIndices.clear(); g_searchFocused = false;
            InvalidateRect(hWnd,NULL,TRUE); return 0;
        } else if ((wchar_t)wParam >= 32 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            g_searchBuf += (wchar_t)wParam;
        } else break;
        UpdateFilter();
        if (!g_searchBuf.empty() && !g_filteredIndices.empty()) {
            InvalidateRect(hWnd, NULL, TRUE);
        } else if (g_searchBuf.empty()) {
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if (!PtInBtn(pt,BTN_SEARCH)) g_searchFocused = false;

        // Compact mode — handle everything first before title bar checks
        if(g_compactMode){
            if(PtInRect(&g_compCatR, pt)){
                g_compactMode = false;
                SetWindowPos(hWnd, NULL,
                    g_normalRect.left, g_normalRect.top,
                    g_normalRect.right - g_normalRect.left,
                    g_normalRect.bottom - g_normalRect.top, SWP_NOZORDER);
                InvalidateRect(hWnd, NULL, TRUE);
                break;
            }
            if(PtInBtn(pt,BTN_CLOSE)){PostMessage(hWnd,WM_CLOSE,0,0);break;}
            if(PtInBtn(pt,BTN_MAXIMIZE)){ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);break;}
            if(PtInBtn(pt,BTN_MINIMIZE)){ShowWindow(hWnd,SW_MINIMIZE);break;}
            if(PtInBtn(pt,BTN_WIDGET)){
                g_compactMode = false;
                SetWindowPos(hWnd, NULL,
                    g_normalRect.left, g_normalRect.top,
                    g_normalRect.right - g_normalRect.left,
                    g_normalRect.bottom - g_normalRect.top, SWP_NOZORDER);
                InvalidateRect(hWnd, NULL, TRUE);
                break;
            }
            bool cHit=false;
            for(int i=0;i<5;i++){
                if(PtInRect(&g_compBtns[i], pt)){
                    if(i==0 && g_trackIdx>0) PlayTrack(g_trackIdx-1,true);
                    else if(i==1){g_userStop=true; StopAudio();}
                    else if(i==2) TogglePause();
                    else if(i==3 && g_trackIdx<(int)g_playlist.size()-1) PlayTrack(g_trackIdx+1,true);
                    else if(i==4){
                            int ns=(int)g_curSec+10;
                        if(g_maxSec>0&&ns>g_maxSec) ns=g_maxSec;
                        if(g_pReader||g_useVorbis) SeekTo((double)ns);
                        g_curSec=ns;
                    }
                    cHit=true;
                    InvalidateRect(hWnd,NULL,TRUE);
                    break;
                }
            }
            if(!cHit) { g_dragging=true; g_dragPt=pt; SetCapture(hWnd); }
            break;
        }

        if(pt.y<TITLE_H && !PtInBtn(pt,BTN_CLOSE) && !PtInBtn(pt,BTN_MAXIMIZE) && !PtInBtn(pt,BTN_MINIMIZE) && !PtInBtn(pt,BTN_WIDGET) && !PtInBtn(pt,BTN_EQ) && !PtInBtn(pt,BTN_SEARCH)){
            g_dragging=true; g_dragPt=pt; SetCapture(hWnd); break;
        }
        if(PtInBtn(pt,BTN_CLOSE)){PostMessage(hWnd,WM_CLOSE,0,0);break;}
        if(PtInBtn(pt,BTN_MAXIMIZE)){ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);break;}
        if(PtInBtn(pt,BTN_MINIMIZE)){ShowWindow(hWnd,SW_MINIMIZE);break;}
        if(PtInBtn(pt,BTN_WIDGET)){
            g_compactMode = !g_compactMode;
            if(g_compactMode){
                GetWindowRect(hWnd, &g_normalRect);
                int x = g_normalRect.left + (g_normalRect.right-g_normalRect.left - COMPACT_W)/2;
                int y = g_normalRect.top + (g_normalRect.bottom-g_normalRect.top - COMPACT_H)/2;
                SetWindowPos(hWnd, NULL, x, y, COMPACT_W, COMPACT_H, SWP_NOZORDER);
            } else {
                SetWindowPos(hWnd, NULL,
                    g_normalRect.left, g_normalRect.top,
                    g_normalRect.right - g_normalRect.left,
                    g_normalRect.bottom - g_normalRect.top, SWP_NOZORDER);
            }
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }
        if(PtInBtn(pt,BTN_EQ)){
            if(g_hEqWnd&&IsWindowVisible(g_hEqWnd)){
                ShowWindow(g_hEqWnd,SW_HIDE);
            }else if(g_hEqWnd){
                RECT mw; GetWindowRect(hWnd,&mw);
                int ex=mw.left+(mw.right-mw.left-EQ_W)/2, ey=mw.top+(mw.bottom-mw.top-EQ_H)/2;
                SetWindowPos(g_hEqWnd,NULL,ex,ey,0,0,SWP_NOSIZE|SWP_NOZORDER);
                ShowWindow(g_hEqWnd,SW_SHOW);
                SetForegroundWindow(g_hEqWnd);
            }
            break;
        }
        if(PtInBtn(pt,BTN_SEARCH)){
            if (g_searchFocused) {
                g_searchFocused = false; g_searchBuf.clear(); g_filteredIndices.clear();
            } else {
                g_searchFocused = true; SetFocus(hWnd);
            }
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }

        // Resize grip
        if(pt.x>=g_rcClient.right-RESIZE_BORDER && pt.y>=g_rcClient.bottom-RESIZE_BORDER){
            g_resizing=true; g_dragPt=pt;
            RECT wr;GetWindowRect(hWnd,&wr);
            g_resizeSz={wr.right-wr.left,wr.bottom-wr.top};
            SetCapture(hWnd);break;
        }

        // Seek slider
        if(InSeek(pt,g_rcClient)){
            g_dragSeek=true; SetCapture(hWnd);
            if(g_maxSec>0){double sec=(double)SeekPct(pt,g_rcClient)/10000*g_maxSec;
                if(g_pReader||g_useVorbis) SeekTo(sec);
                g_curSec=sec;
                InvalidateRect(hWnd,NULL,TRUE);}
            break;
        }

        // Volume slider
        if(InVol(pt)){
            g_volDragX=pt.x; g_volume=(int)(VolPos(pt)*1000);
            if(g_volume<0)g_volume=0;if(g_volume>1000)g_volume=1000;
            if(g_muted){g_muted=false;g_prevVolume=g_volume;}
            if(g_pSimpleVol) g_pSimpleVol->SetMasterVolume(g_volume/1000.0f,NULL);
            InvalidateRect(hWnd,NULL,TRUE); SetCapture(hWnd);break;
        }

        // Bottom buttons
        g_pressBtn=BTN_NONE;
        for(int i=BTN_SHUFFLE;i<=BTN_VOLUME;i++) if(PtInBtn(pt,(BtnID)i)){g_pressBtn=(BtnID)i;InvalidateRect(hWnd,NULL,TRUE);break;}
        if(PtInBtn(pt,BTN_VOLUME)){g_pressBtn=BTN_VOLUME;InvalidateRect(hWnd,NULL,TRUE);}
        if(PtInBtn(pt,BTN_SETTINGS)){g_pressBtn=BTN_SETTINGS;InvalidateRect(hWnd,NULL,TRUE);}
        if(PtInBtn(pt,BTN_LOCATE)){g_pressBtn=BTN_LOCATE;InvalidateRect(hWnd,NULL,TRUE);}

        // Browser row click
        {
            int browseRight = g_rcClient.right;
            if (pt.y >= TITLE_H && pt.y < g_rcClient.bottom - BOTTOM_H && pt.x >= PADDING + 10 && pt.x < browseRight) {
                int idx = g_browserScroll + (pt.y - TITLE_H - PADDING) / 44;
                if (g_hasFolder) {
                    // Account for search box height
                    int searchH = (!g_searchBuf.empty() || g_searchFocused) ? 22 + 4 : 0;
                    int adjY = pt.y - TITLE_H - PADDING - searchH;
                    idx = g_browserScroll + adjY / 44;
                }
                if (g_searchBuf.empty()) {
                    if (idx >= 0 && idx < (int)g_playlist.size() && idx != g_trackIdx) PlayTrack(idx, true);
                } else {
                    if (idx >= 0 && idx < (int)g_filteredIndices.size()) {
                        int realIdx = g_filteredIndices[idx];
                        if (realIdx != g_trackIdx) PlayTrack(realIdx, true);
                    }
                }
                break;
            }
        }
        // Browser scrollbar click
        {
            if (pt.x >= PADDING && pt.x < PADDING + 6 && pt.y >= TITLE_H && pt.y < g_rcClient.bottom - BOTTOM_H) {
                int brH = g_rcClient.bottom - BOTTOM_H - TITLE_H - PADDING*2;
                int totalItems = g_searchBuf.empty() ? (int)g_playlist.size() : (int)g_filteredIndices.size();
                int visRows = brH / 44;
                int maxSc = max(0, totalItems - visRows);
                if (maxSc > 0) {
                    int sbTop = TITLE_H + PADDING, sbBot = g_rcClient.bottom - BOTTOM_H - PADDING;
                    float pct = (float)(pt.y - sbTop) / (sbBot - sbTop);
                    g_browserScroll = (int)(pct * maxSc);
                    if (g_browserScroll < 0) g_browserScroll = 0;
                    if (g_browserScroll > maxSc) g_browserScroll = maxSc;
                    CacheMetaRange(g_browserScroll);
                    g_bDragScroll = true;
                    SetCapture(hWnd);
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                break;
            }
        }
        break;
    }

    case WM_MOUSEMOVE:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if(g_dragging){
            RECT wr;GetWindowRect(hWnd,&wr);
            int nx=wr.left+pt.x-g_dragPt.x, ny=wr.top+pt.y-g_dragPt.y;
            RECT wa; SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);
            int w=wr.right-wr.left, h=wr.bottom-wr.top;
            nx=max(wa.left,min(nx,wa.right-w));
            ny=max(wa.top,min(ny,wa.bottom-h));
            SetWindowPos(hWnd,NULL,nx,ny,0,0,SWP_NOSIZE|SWP_NOZORDER);
            break;
        }
        if(g_resizing){
            RECT wr;GetWindowRect(hWnd,&wr);
            int nw=g_resizeSz.cx+pt.x-g_dragPt.x,nh=g_resizeSz.cy+pt.y-g_dragPt.y;
            if(nw<500)nw=500;if(nh<400)nh=400;
            SetWindowPos(hWnd,NULL,0,0,nw,nh,SWP_NOMOVE|SWP_NOZORDER);
            break;
        }
        if(g_dragSeek&&g_maxSec>0){
            double sec=(double)SeekPct(pt,g_rcClient)/10000*g_maxSec;
            if(sec>g_maxSec)sec=g_maxSec;
            g_curSec=sec;
            if(g_pReader||g_useVorbis) SeekTo(sec);
            InvalidateRect(hWnd,NULL,TRUE);break;}

        if(g_bDragScroll){
            int brH = g_rcClient.bottom - BOTTOM_H - TITLE_H - PADDING*2;
            int visRows = brH / 44;
            int totalItems = g_searchBuf.empty() ? (int)g_playlist.size() : (int)g_filteredIndices.size();
            int maxSc = max(0, totalItems - visRows);
            if (maxSc > 0) {
                int sbTop = TITLE_H + PADDING, sbBot = g_rcClient.bottom - BOTTOM_H - PADDING;
                float pct = (float)(pt.y - sbTop) / (sbBot - sbTop);
                g_browserScroll = (int)(pct * maxSc);
                if (g_browserScroll < 0) g_browserScroll = 0;
                if (g_browserScroll > maxSc) g_browserScroll = maxSc;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        }

        if(g_volDragX>=0){
            g_volume=(int)(VolPos(pt)*1000);if(g_volume<0)g_volume=0;if(g_volume>1000)g_volume=1000;
            if(g_muted){g_muted=false;g_prevVolume=g_volume;}
            if(g_pSimpleVol) g_pSimpleVol->SetMasterVolume(g_volume/1000.0f,NULL);
            InvalidateRect(hWnd,NULL,TRUE);break;
        }

        BtnID old=g_hoverBtn; g_hoverBtn=BTN_NONE;
        for(int i=BTN_SHUFFLE;i<BTN_LAST;i++) if(PtInBtn(pt,(BtnID)i)){g_hoverBtn=(BtnID)i;break;}
        if(old!=g_hoverBtn) InvalidateRect(hWnd,NULL,TRUE);
        g_mousePt = pt;
        bool onR=pt.x>=g_rcClient.right-RESIZE_BORDER && pt.y>=g_rcClient.bottom-RESIZE_BORDER;
        SetCursor(LoadCursor(NULL,onR?IDC_SIZENWSE:IDC_ARROW));
        break;
    }

    case WM_RBUTTONUP:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if (g_hasFolder && pt.y >= TITLE_H && pt.y < g_rcClient.bottom - BOTTOM_H) {
            int idx = g_browserScroll + (pt.y - TITLE_H - PADDING) / 44;
                if (idx >= 0 && idx < (int)g_playlist.size()) {
                HMENU hm=CreatePopupMenu();
                wchar_t buf[256];
                swprintf(buf,256,L"Play \"%s\"",(idx<(int)g_songTitles.size()&&!g_songTitles[idx].empty())?g_songTitles[idx].c_str():L"Track");
                AppendMenuW(hm,MF_STRING,100,buf);
                AppendMenuW(hm,MF_STRING,101,L"Play Next");
                AppendMenuW(hm,MF_STRING,102,L"Remove from Playlist");
                AppendMenuW(hm,MF_SEPARATOR,0,NULL);
                AppendMenuW(hm,MF_STRING,103,L"Show in Explorer");
                AppendMenuW(hm,MF_STRING,104,L"Properties");
                POINT mp; GetCursorPos(&mp);
                SetForegroundWindow(hWnd);
                int cmd=TrackPopupMenu(hm,TPM_RETURNCMD|TPM_NONOTIFY,mp.x,mp.y,0,hWnd,NULL);
                DestroyMenu(hm);
                if(cmd==100) PlayTrack(idx,true);
                else if(cmd==102){
                    g_playlist.erase(g_playlist.begin()+idx);
                    if(idx<g_trackIdx)g_trackIdx--;
                    else if(idx==g_trackIdx){StopAudio();g_trackIdx=-1;}
                    if(idx<(int)g_songTitles.size())g_songTitles.erase(g_songTitles.begin()+idx);
                    if(idx<(int)g_songArtists.size())g_songArtists.erase(g_songArtists.begin()+idx);
                    if(idx<(int)g_songDurations.size())g_songDurations.erase(g_songDurations.begin()+idx);
                    if(idx<(int)g_metaCached.size())g_metaCached.erase(g_metaCached.begin()+idx);
                    if(idx<(int)g_thumbs.size()){delete g_thumbs[idx];g_thumbs.erase(g_thumbs.begin()+idx);}
                    if(g_playlist.empty())g_trackIdx=-1;
                    if (!g_searchBuf.empty()) UpdateFilter();
                    int totalItems=(int)g_playlist.size();
                    int brH = g_rcClient.bottom - BOTTOM_H - TITLE_H - PADDING*2;
                    int visRows = brH / 44;
                    int maxSc = max(0, totalItems - visRows);
                    if(g_browserScroll>maxSc)g_browserScroll=maxSc;
                    InvalidateRect(hWnd,NULL,TRUE);
                }else if(cmd==103){
                    std::wstring params = L"/select,\"" + g_playlist[idx] + L"\"";
                    ShellExecuteW(NULL, NULL, L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
                }else if(cmd==104){
                    SHELLEXECUTEINFOW sei={sizeof(sei)};
                    sei.lpFile = g_playlist[idx].c_str();
                    sei.lpVerb = L"properties";
                    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NO_CONSOLE;
                    sei.nShow = SW_SHOW;
                    ShellExecuteExW(&sei);
                }
            }
        }
        break;
    }

    case WM_LBUTTONUP:{
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        bool wd=g_dragging||g_resizing||g_dragSeek||g_volDragX>=0||g_opacityDragX>=0||g_bDragScroll;
        if(g_dragging){g_dragging=false;ReleaseCapture();}
        if(g_resizing){g_resizing=false;ReleaseCapture();}
        if(g_dragSeek){g_dragSeek=false;ReleaseCapture();if((g_pReader||g_useVorbis)&&g_maxSec>0) SeekTo((double)g_curSec);}
        if(g_volDragX>=0){g_volDragX=-1;ReleaseCapture();}
        if(g_opacityDragX>=0){g_opacityDragX=-1;ReleaseCapture();}
        if(g_bDragScroll){g_bDragScroll=false;CacheMetaRange(g_browserScroll);ReleaseCapture();}
        if(wd)break;

        BtnID clicked=g_pressBtn; g_pressBtn=BTN_NONE;
        if(clicked!=BTN_NONE){
            InvalidateRect(hWnd,NULL,TRUE);
            switch(clicked){
            case BTN_PLAY: if(!g_playing)OpenFolder();else TogglePause(); break;
            case BTN_STOP: if(g_playing){g_userStop=true;StopAudio();} break;
            case BTN_PREV: if(g_trackIdx>0)PlayTrack(g_trackIdx-1,true); break;
            case BTN_NEXT: if(g_trackIdx<(int)g_playlist.size()-1)PlayTrack(g_trackIdx+1,true); break;
            case BTN_SHUFFLE: g_repeatMode = (g_repeatMode + 1) % 3; g_repeatOnce=false; InvalidateRect(hWnd,NULL,TRUE); break;
            case BTN_SETTINGS:
                if(g_hSettingsWnd&&IsWindowVisible(g_hSettingsWnd)){
                    ShowWindow(g_hSettingsWnd,SW_HIDE);
                }else if(g_hSettingsWnd){
                    RECT mw; GetWindowRect(hWnd,&mw);
                    int sx=mw.left+(mw.right-mw.left-300)/2, sy=mw.top+(mw.bottom-mw.top-500)/2;
                    SetWindowPos(g_hSettingsWnd,NULL,sx,sy,0,0,SWP_NOSIZE|SWP_NOZORDER);
                    EnumerateAudioDevices();
                    InvalidateRect(g_hSettingsWnd,NULL,TRUE);
                    ShowWindow(g_hSettingsWnd,SW_SHOW);
                    SetForegroundWindow(g_hSettingsWnd);
                } break;
            case BTN_VOLUME:
                g_muted=!g_muted;
                if(g_muted){g_prevVolume=g_volume;g_volume=0;if(g_pSimpleVol)g_pSimpleVol->SetMasterVolume(0,NULL);}
                else{g_volume=g_prevVolume;if(g_pSimpleVol)g_pSimpleVol->SetMasterVolume(g_volume/1000.0f,NULL);}
                InvalidateRect(hWnd,NULL,TRUE); break;
            case BTN_LOCATE:
                if(g_hasFolder){ScrollToCurrentTrack();}
                break;
            }
        }
        break;
    }

    case WM_APP+2:
        if(g_playlist.empty())break;
        if(g_repeatMode==1){
            if(g_repeatOnce){
                g_repeatOnce=false;
                if(g_trackIdx+1<(int)g_playlist.size()) PlayTrack(g_trackIdx+1,false);
            }else{
                g_repeatOnce=true;
                PlayTrack(g_trackIdx,false);
            }
        }else if(g_repeatMode==2) PlayTrack((g_trackIdx+1)%(int)g_playlist.size(),false);
        else if(g_trackIdx+1<(int)g_playlist.size()) PlayTrack(g_trackIdx+1,false);
        break;

    case WM_APP+3:
        if (g_inOpenFolder) { break; }
        switch(wParam){
        case SMTC_Play: if(g_paused) TogglePause(); break;
        case SMTC_Pause: if(!g_paused) TogglePause(); break;
        case SMTC_Stop: StopAudio(); break;
        case SMTC_Next: if(g_trackIdx<(int)g_playlist.size()-1) PlayTrack(g_trackIdx+1,true); break;
        case SMTC_Previous: if(g_trackIdx>0) PlayTrack(g_trackIdx-1,true); break;
        }
        break;

    case WM_TRAYICON:
        if(lParam==WM_LBUTTONUP){
            if(IsWindowVisible(g_hWnd)){ShowWindow(g_hWnd,SW_HIDE);}
            else{ShowWindow(g_hWnd,SW_SHOW);SetForegroundWindow(g_hWnd);}
        }else if(lParam==WM_RBUTTONUP){
            HMENU hm=CreatePopupMenu();
            AppendMenuW(hm,MF_STRING,1000,g_playing&&!g_paused?L"Pause":L"Play");
            AppendMenuW(hm,MF_STRING,1001,L"Next");
            AppendMenuW(hm,MF_STRING,1002,L"Previous");
            AppendMenuW(hm,MF_SEPARATOR,0,NULL);
            AppendMenuW(hm,MF_STRING,1003,IsWindowVisible(g_hWnd)?L"Hide Window":L"Show Window");
            AppendMenuW(hm,MF_SEPARATOR,0,NULL);
            AppendMenuW(hm,MF_STRING,1004,L"Exit");
            POINT mp;GetCursorPos(&mp);
            SetForegroundWindow(g_hWnd);
            int cmd=TrackPopupMenu(hm,TPM_RETURNCMD|TPM_NONOTIFY,mp.x,mp.y,0,g_hWnd,NULL);
            DestroyMenu(hm);
            if(cmd==1000)TogglePause();
            else if(cmd==1001&&g_trackIdx<(int)g_playlist.size()-1)PlayTrack(g_trackIdx+1,true);
            else if(cmd==1002&&g_trackIdx>0)PlayTrack(g_trackIdx-1,true);
            else if(cmd==1003){
                if(IsWindowVisible(g_hWnd))ShowWindow(g_hWnd,SW_HIDE);
                else{ShowWindow(g_hWnd,SW_SHOW);SetForegroundWindow(g_hWnd);}
            }else if(cmd==1004)PostMessageW(g_hWnd,WM_CLOSE,0,0);
        }
        break;

    case WM_APPCOMMAND:
        if (g_inOpenFolder) { return TRUE; }
        switch(GET_APPCOMMAND_LPARAM(lParam)){
        case APPCOMMAND_MEDIA_PLAY_PAUSE: TogglePause(); break;
        case APPCOMMAND_MEDIA_NEXT: if(g_trackIdx<(int)g_playlist.size()-1) PlayTrack(g_trackIdx+1,true); break;
        case APPCOMMAND_MEDIA_PREV: if(g_trackIdx>0) PlayTrack(g_trackIdx-1,true); break;
        case APPCOMMAND_MEDIA_STOP: StopAudio(); break;
        }
        return TRUE;

    case WM_MOUSEWHEEL:
        {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd, &pt);
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int scr = delta / WHEEL_DELTA * 3;
            g_browserScroll -= scr;
            if (g_browserScroll < 0) g_browserScroll = 0;
            int brH = g_rcClient.bottom - BOTTOM_H - TITLE_H - PADDING*2;
            int totalItems = g_searchBuf.empty() ? (int)g_playlist.size() : (int)g_filteredIndices.size();
            int maxSc = max(0, totalItems - (brH / 44));
            if (g_browserScroll > maxSc) g_browserScroll = maxSc;
            CacheMetaRange(g_browserScroll);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;

    case WM_TIMER:{
        static DWORD s_lastThumbs = 0;
        DWORD now = GetTickCount();
        if(g_playing && !g_paused) {
            g_rainbowTick = (g_rainbowTick + 1) % 360;
            if(g_useVorbis) g_curSec = (double)g_decPCM.readPos / g_decPCM.channels / g_decPCM.sampleRate;
            else g_curSec = g_lastSamplePos / 10000000.0;
        }
        if(!g_playing) g_rainbowTick = 0;
        UpdateEventParticles();
        if(now - s_lastThumbs >= 250) {
            s_lastThumbs = now;
            LoadVisibleThumbs();
        }
        InvalidateRect(g_hWnd,NULL,TRUE);
        break;
    }

    case WM_APP:
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    case WM_APP + 1:
        SaveMetaCache();
        return 0;

    default: return DefWindowProcW(hWnd,msg,wParam,lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Compact mode helpers
// ---------------------------------------------------------------------------
static const wchar_t *g_compBtnTxt[5] = {L"\u23EE", L"\u23F9", L"\u23EF", L"\u23ED", L"\u23E9"};
RECT g_compBtns[5];
RECT g_compCatR;

static void DrawCompactMode(HDC hdc, const RECT &rc) {
    SetBkMode(hdc, TRANSPARENT);
    // Rainbow bar at top
    RECT rbR = {rc.left, rc.top, rc.right, rc.top + 6};
    if(g_playing && !g_paused && g_rainbowEnabled){
        Gdiplus::Graphics g(hdc);
        Gdiplus::Rect gr(0, 0, rc.right, 6);
        Gdiplus::LinearGradientBrush br(gr, Gdiplus::Color(255,255,0,0), Gdiplus::Color(255,255,0,0), Gdiplus::LinearGradientModeHorizontal);
        Gdiplus::Color cols[7];
        float pos[7] = {0,1.0f/6,2.0f/6,3.0f/6,4.0f/6,5.0f/6,1};
        cols[0].SetFromCOLORREF(RGB(255,0,0));
        cols[1].SetFromCOLORREF(RGB(255,255,0));
        cols[2].SetFromCOLORREF(RGB(0,255,0));
        cols[3].SetFromCOLORREF(RGB(0,255,255));
        cols[4].SetFromCOLORREF(RGB(0,0,255));
        cols[5].SetFromCOLORREF(RGB(255,0,255));
        cols[6].SetFromCOLORREF(RGB(255,0,0));
        br.SetInterpolationColors(cols, pos, 7);
        g.TranslateTransform((float)g_rainbowTick, 0);
        g.FillRectangle(&br, gr);
    } else {
        HBRUSH hb = CreateSolidBrush(COL_TITLE_BG);
        FillRect(hdc, &rbR, hb); DeleteObject(hb);
    }

    // Cat hand button (clickable to return to full mode) + track title
    g_compCatR = {rc.right-56, 8, rc.right-8, 48};
    SelectObject(hdc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, RGB(0x3A,0x2A,0x4A));
    HPEN cp = CreatePen(PS_SOLID, 1, RGB(0xBB,0x88,0xFF));
    HPEN oc = (HPEN)SelectObject(hdc, cp);
    RoundRect(hdc, g_compCatR.left, g_compCatR.top, g_compCatR.right, g_compCatR.bottom, 10, 10);
    SelectObject(hdc, oc); DeleteObject(cp);
    SetTextColor(hdc, RGB(0xDD,0xBB,0xFF));
    SelectObject(hdc, g_hFontBold);
    DrawTextW(hdc, L"\U0001F431", -1, &g_compCatR, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    std::wstring title;
    if(g_trackIdx >= 0 && g_trackIdx < (int)g_playlist.size()){
        title = g_playlist[g_trackIdx];
        size_t dot = title.rfind(L'.');
        if(dot != std::wstring::npos) title = title.substr(0, dot);
        if(!g_artistName.empty()) title = title + L" - " + g_artistName;
    } else title = L"No track";
    SetTextColor(hdc, RGB(0xE0,0xE0,0xF0));
    SelectObject(hdc, g_hFont);
    RECT tiR = {8, 10, rc.right-64, 36};
    DrawTextW(hdc, title.c_str(), -1, &tiR, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    // Time
    wchar_t tbuf[32];
    int gcs=(int)g_curSec, min = gcs/60, sec = gcs%60;
    int tmin = g_maxSec/60, tsec = g_maxSec%60;
    swprintf(tbuf, 32, L"%d:%02d / %d:%02d", min, sec, tmin, tsec);
    SetTextColor(hdc, g_useLightTheme ? COL_TEXT : RGB(0xFF,0xFF,0xFF));
    SelectObject(hdc,g_hFontBold);
    SIZE tsz; GetTextExtentPoint32W(hdc,tbuf,(int)wcslen(tbuf),&tsz);
    RECT tmR={(rc.right-tsz.cx)/2,44,(rc.right+tsz.cx)/2,64};
    DrawTextW(hdc,tbuf,-1,&tmR,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    // Control buttons
    int bw = 40, bh = 32, gap = 8, total = bw * 5 + gap * 4;
    int sx = (rc.right - total) / 2, sy = 72;
    for(int i=0;i<5;i++){
        SetRect(&g_compBtns[i], sx+i*(bw+gap), sy, sx+i*(bw+gap)+bw, sy+bh);
        HBRUSH hbb = CreateSolidBrush(RGB(0x2A,0x2A,0x3A));
        FillRect(hdc, &g_compBtns[i], hbb); DeleteObject(hbb);
        HPEN hp = CreatePen(PS_SOLID, 1, RGB(0x5A,0x4A,0x6A));
        HPEN op = (HPEN)SelectObject(hdc, hp);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, g_compBtns[i].left, g_compBtns[i].top, g_compBtns[i].right, g_compBtns[i].bottom);
        SelectObject(hdc, op); DeleteObject(hp);
        SetTextColor(hdc, RGB(0xD0,0xD0,0xE0));
        DrawTextW(hdc, g_compBtnTxt[i], -1, &g_compBtns[i], DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);
    g_hInst=hInstance;
    Gdiplus::GdiplusStartupInput gsi; GdiplusStartup(&g_gdiplusToken,&gsi,NULL);
    INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);

    g_hFont=CreateFontW(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,L"Segoe UI");
    g_hFontBold=CreateFontW(14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,L"Segoe UI");
    InitTooltips();

    WNDCLASSW wc={};
    wc.lpfnWndProc=WndProc; wc.hInstance=hInstance;
    wc.hIcon=LoadIconW(hInstance,MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=NULL;
    wc.lpszClassName=L"LitePlayerCPP";
    RegisterClassW(&wc);

    WNDCLASSW swc={};
    swc.lpfnWndProc=SettingsWndProc; swc.hInstance=hInstance;
    swc.hCursor=LoadCursor(NULL,IDC_ARROW);
    swc.hbrBackground=NULL;
    swc.lpszClassName=L"LiteSettings";
    RegisterClassW(&swc);

    WNDCLASSW eqc={};
    eqc.lpfnWndProc=EqWndProc; eqc.hInstance=hInstance;
    eqc.hCursor=LoadCursor(NULL,IDC_ARROW);
    eqc.hbrBackground=NULL;
    eqc.lpszClassName=L"LiteEQ";
    RegisterClassW(&eqc);

    HWND hWnd=CreateWindowExW(WS_EX_LAYERED,L"LitePlayerCPP",L"LITE Music Player",
        WS_VISIBLE|WS_POPUP|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,700,520,
        NULL,NULL,hInstance,NULL);
    if(!hWnd) return 1;
    SetLayeredWindowAttributes(hWnd, 0, g_windowAlpha, LWA_ALPHA);

    g_hSettingsWnd=CreateWindowExW(0,L"LiteSettings",L"Settings",
        WS_POPUP,
        CW_USEDEFAULT,CW_USEDEFAULT,300,560,
        hWnd,NULL,hInstance,NULL);

    g_hEqWnd=CreateWindowExW(0,L"LiteEQ",L"Equalizer",
        WS_POPUP,
        CW_USEDEFAULT,CW_USEDEFAULT,EQ_W,EQ_H,
        hWnd,NULL,hInstance,NULL);

    LoadSettings();
    EnumerateAudioDevices();
    SetAcrylicBlur(hWnd, g_acrylicBlur ? g_blurIntensity : 0);
    InitSMTC(hWnd);
    InitTrayIcon(hWnd);

    // Start background metadata scanning thread
    InitializeCriticalSection(&g_metaCS);
    g_hMetaWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_metaThreadRun = true;
    g_hMetaThread = CreateThread(NULL, 0, MetaThreadProc, NULL, 0, NULL);

    ShowWindow(hWnd,nCmdShow);
    UpdateWindow(hWnd);
    SetTimer(hWnd,1,16,NULL);

    if (g_hasFolder && g_trackIdx >= 0 && g_trackIdx < (int)g_playlist.size()) {
        PlayTrack(g_trackIdx, false);
        ScrollToCurrentTrack();
    }

    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}

    KillTimer(hWnd,1);
    if(g_hAudioThread){g_audioRun=false;
        if(WaitForSingleObject(g_hAudioThread,200)!=WAIT_OBJECT_0) TerminateThread(g_hAudioThread,0);
        CloseHandle(g_hAudioThread);g_hAudioThread=NULL;}
    if(g_pSimpleVol){g_pSimpleVol->Release();g_pSimpleVol=NULL;}
    if(g_pRenderClient){g_pRenderClient->Release();g_pRenderClient=NULL;}
    if(g_pAudioClient){g_pAudioClient->Stop();g_pAudioClient->Release();g_pAudioClient=NULL;}
    if(g_pReader){g_pReader->Release();g_pReader=NULL;}
    DeleteObject(g_hFont);DeleteObject(g_hFontBold);
    if(g_albumArt) delete g_albumArt;
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    MFShutdown();
    UninitSMTC();
    CoUninitialize();
    return (int)msg.wParam;
}


