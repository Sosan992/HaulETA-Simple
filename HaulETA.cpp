#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#define scs_telemetry_init scs_telemetry_init_sdk_declaration
#define scs_telemetry_shutdown scs_telemetry_shutdown_sdk_declaration
#include <scssdk_telemetry.h>
#undef scs_telemetry_init
#undef scs_telemetry_shutdown
#include <common/scssdk_telemetry_truck_common_channels.h>
#include <common/scssdk_telemetry_common_channels.h>
#include <common/scssdk_telemetry_common_configs.h>

namespace {
constexpr wchar_t kVersionTitle[] = L"HaulETA Simple 1.7";

// ---------------------------------------------------------------------------
// Panel layout (pixels). The panel is one layered window; everything is drawn
// into it. Column dividers separate: speed limit | route | fuel | fatigue | clock | G-meter.
// ---------------------------------------------------------------------------
constexpr int kPanelWidth = 788;
constexpr int kPanelHeight = 104;
constexpr double kPanelRadius = 18.0;
constexpr int kDividerTop = 12;
constexpr int kDividerBottom = kPanelHeight - 12;
constexpr double kProgressY = 67.5;  // route progress bar under the times
constexpr int kDividerSpeed = 72;
constexpr int kDividerRoute = 368;
constexpr int kDividerFuel = 454;
constexpr int kDividerFatigue = 536;

constexpr int kSpeedSignX = 12;
constexpr int kSpeedSignY = 16;
constexpr int kSpeedSignSize = 48;

constexpr int kRouteLeft = 82;
constexpr int kRouteRight = 364;
constexpr int kArrivalLeft = 206;

constexpr int kFuelFrameX = 375;
constexpr int kFuelFrameY = 30;
constexpr int kFuelFrameWidth = 76;
constexpr int kFuelFrameHeight = 64;
constexpr double kFuelGaugeCenterY = 48.0;  // relative to kFuelFrameY
constexpr double kFuelGaugeRadius = 26.0;

constexpr int kFatigueFigureWidth = 22;
constexpr int kFatigueFigureHeight = 42;
constexpr int kFatigueX = (kDividerFuel + kDividerFatigue) / 2 - kFatigueFigureWidth / 2;
constexpr int kFatigueY = 31;
constexpr int kRestTextTop = 75;
constexpr int kRestTextBottom = 97;
// Below this many in-game minutes the rest countdown turns red.
constexpr int kRestWarningMinutes = 60;

constexpr int kClockX = 544;
constexpr int kClockY = 36;
constexpr int kClockWidth = 120;
constexpr int kClockHeight = 42;

// G-meter column (right end): round metal-bezel dial with a dot for the force the passengers
// feel (sideways and forward/back), the current value and a short-term peak next to it.
constexpr int kDividerClock = 672;
constexpr int kGBezelRadius = 31;                       // metal_62x62.bmp
constexpr double kGFaceRadius = 28.0;
constexpr int kGCenterX = kDividerClock + 6 + kGBezelRadius;  // 709
constexpr int kGCenterY = 61;
constexpr int kGTextLeft = kGCenterX + kGBezelRadius + 3;     // 743
constexpr double kGFullScale = 0.5;      // g at the edge of the dial
constexpr double kGComfortGood = 0.15;   // below: green (comfortable for passengers)
constexpr double kGComfortCaution = 0.30;  // below: amber, above: red
constexpr double kGSmoothingSeconds = 0.12;
constexpr double kGPeakHoldSeconds = 5.0;
constexpr int kGTrailLength = 24;        // dial trail: ~1.2 s at 20 updates per second
constexpr UINT_PTR kGMeterTimerId = 3;
constexpr UINT kGMeterIntervalMs = 50;
constexpr double kGravity = 9.80665;

// Detachable elements (Ctrl+F12 checkboxes). When detached they leave the panel, which gets shorter,
// and appear as their own small panels that can be placed anywhere (Ctrl+F11, then drag).
constexpr int kGCardLeft = kDividerClock + 2;                 // G-meter card = panel columns 674..788
constexpr int kGCardWidth = kPanelWidth - kGCardLeft;         // 114
constexpr int kRouteCardWidth = 400;                          // "from -> to" card with the time left
constexpr int kRouteCardDivider = 314;
constexpr int kCardGap = 24;                                  // default distance above the main panel

// Texture resources compiled into the DLL from assets\ (see HaulETA.rc).
constexpr int kCarbonTextureId = 101;      // carbon_788x104.bmp, panel background
constexpr int kMetalGaugeTextureId = 102;  // metal_76x64.bmp, fuel gauge bezel
constexpr int kMetalClockTextureId = 103;  // metal_120x42.bmp, clock bezel
constexpr int kDiffuserTextureId = 104;    // diffuser_816x132.bmp, frosted plastic for the edge glow
constexpr int kMetalGMeterTextureId = 105; // metal_62x62.bmp, G-meter bezel

// Edge glow: every window is larger than its panel by this margin on every side,
// so the amber glow can spill out around the panel edge.
constexpr int kGlowMargin = 14;
constexpr double kGlowRiseSeconds = 0.06;  // lamp on: quick warm-up
constexpr double kGlowFallSeconds = 0.22;  // lamp off: softer fade, like a bulb
constexpr UINT kGlowMessage = WM_APP + 1;
constexpr UINT_PTR kGlowTimerId = 2;

// Headlight frame glow (whole edge, same back-lit style as the blinker glow):
// parking lights -> faint white, low beam -> stronger white, high beam -> blue, no lights -> none.
constexpr double kParkingGlow = 0.32;
constexpr double kLowBeamGlow = 0.78;
constexpr double kHighBeamGlow = 0.90;
constexpr double kLightRiseSeconds = 0.18;  // time for a full 0 -> 1 change
constexpr double kLightFallSeconds = 0.35;

struct Rgb { int r, g, b; };

// Background themes (Ctrl+F12 drop-down). Dark: carbon fibre and brushed metal. Light: white paper
// card with a soft shadow, near-black text, violet accents, violet gauge frames and a black clock pill.
struct Palette {
    bool light;
    Rgb textLabel;    // headings
    Rgb textSoft;     // footer
    Rgb textValue;    // values
    Rgb accent;       // arrival time, progress, fuel level
    Rgb accentDeep;
    Rgb tick;         // gauge ticks and G-meter rings
    double shadowAlpha;              // soft shadows under the sign and the gauges
    double dividerDark, dividerLight;
    Rgb track; double trackAlpha;    // empty part of progress bars and the fuel scale
    Rgb rim; double rimAlpha;        // thin panel rim
    Rgb figureOutline, figureHollow;
};
constexpr Palette kDarkPalette{false, {140, 156, 168}, {178, 190, 199}, {240, 244, 247}, {64, 196, 255}, {36, 122, 214},
    {206, 214, 220}, 0.55, 0.35, 0.10, {255, 255, 255}, 0.11, {255, 255, 255}, 0.16, {124, 138, 149}, {20, 24, 28}};
constexpr Palette kLightPalette{true, {126, 126, 140}, {92, 92, 106}, {22, 22, 28}, {108, 76, 245}, {78, 48, 206},
    {72, 72, 88}, 0.20, 0.10, 0.75, {0, 0, 0}, 0.08, {0, 0, 0}, 0.07, {118, 118, 134}, {232, 232, 240}};
Palette g_pal = kDarkPalette;  // the theme in use
constexpr int kPaperTextureId = 106;  // paper_788x104.bmp, light theme background

// ---------------------------------------------------------------------------
// User interface languages (Ctrl+F12 drop-down). English is the default for a new install; the list
// holds the five languages most used by ETS2 players according to Steam review languages (English,
// Russian, Turkish, German, Polish) plus Hungarian.
// ---------------------------------------------------------------------------
struct UiText {
    const wchar_t* languageName;   // shown in the language drop-down, in its own language
    // Panel
    const wchar_t* departure;
    const wchar_t* estimatedArrival;
    const wchar_t* timeLeftPrefix;
    const wchar_t* fuel;
    const wchar_t* fatigue;
    const wchar_t* restNow;
    const wchar_t* localTime;
    const wchar_t* gForce;
    const wchar_t* peak;
    const wchar_t* kmh;
    const wchar_t* waitingForRoute;
    const wchar_t* hotkeysBothBusy;
    const wchar_t* hotkeyMoveBusy;
    const wchar_t* hotkeySettingsBusy;
    // Route card
    const wchar_t* from;
    const wchar_t* to;
    const wchar_t* noJob;
    const wchar_t* arrivalPrefix;
    const wchar_t* timeLeft;
    const wchar_t* hoursMinutes;
    // Settings window
    const wchar_t* transparencyTitle;
    const wchar_t* allPanels;
    const wchar_t* background;
    const wchar_t* language;
    const wchar_t* themeDark;
    const wchar_t* themeLight;
    const wchar_t* separateTitle;
    const wchar_t* gMeterSeparate;
    const wchar_t* routeSeparate;
    const wchar_t* moveHint;
    const wchar_t* settingsTitle;  // caption of the settings window
};

constexpr UiText kTexts[] = {
    {L"English",
        L"DEPARTURE", L"EST. ARRIVAL", L"Time left: ", L"FUEL", L"FATIGUE", L"REST!", L"LOCAL TIME",
        L"G-FORCE", L"MAX", L"km/h", L"Waiting for a GPS route",
        L"Ctrl+F11 and Ctrl+F12 in use (other program)", L"Ctrl+F11 in use (other program)", L"Ctrl+F12 in use (other program)",
        L"FROM", L"TO", L"No active job", L"arrival ", L"TIME LEFT", L"HRS:MIN",
        L"Window transparency", L"All panels", L"Background", L"Language", L"Dark (carbon)", L"Light",
        L"Separate elements", L"G-meter as a separate element", L"Route separate: from → to, time left",
        L"Move: Ctrl+F11, then drag with the mouse", L"Settings"},
    {L"Magyar",
        L"INDULÁS", L"BECSÜLT ÉRKEZÉS", L"Hátralévő idő: ", L"ÜZEMANYAG", L"FÁRADTSÁG", L"PIHENJ!", L"HELYI IDŐ",
        L"G-ERŐ", L"MAX", L"km/h", L"Várakozás GPS-útvonalra",
        L"Ctrl+F11 és Ctrl+F12 foglalt (másik program)", L"Ctrl+F11 foglalt (másik program)", L"Ctrl+F12 foglalt (másik program)",
        L"HONNAN", L"HOVÁ", L"Nincs aktív fuvar", L"érkezés ", L"HÁTRALÉVŐ", L"ÓRA:PERC",
        L"Ablak átlátszósága", L"Minden panel", L"Háttér", L"Nyelv", L"Sötét (karbon)", L"Világos",
        L"Külön elemek", L"G-méter külön elemként", L"Útvonal külön: honnan → hová, hátralévő idő",
        L"Áthelyezés: Ctrl+F11, majd húzd az egérrel", L"Beállítások"},
    {L"Русский",
        L"ВЫЕЗД", L"ОЖИД. ПРИБЫТИЕ", L"Осталось: ", L"ТОПЛИВО", L"УСТАЛОСТЬ", L"ОТДЫХ!", L"МЕСТНОЕ ВРЕМЯ",
        L"ПЕРЕГРУЗКА", L"МАКС", L"км/ч", L"Ожидание GPS-маршрута",
        L"Ctrl+F11 и Ctrl+F12 заняты (другая программа)", L"Ctrl+F11 занят (другая программа)", L"Ctrl+F12 занят (другая программа)",
        L"ОТКУДА", L"КУДА", L"Нет активного заказа", L"прибытие ", L"ОСТАЛОСЬ", L"Ч:МИН",
        L"Прозрачность окна", L"Все панели", L"Фон", L"Язык", L"Тёмный (карбон)", L"Светлый",
        L"Отдельные элементы", L"G-метр отдельным элементом", L"Маршрут отдельно: откуда → куда, остаток времени",
        L"Перемещение: Ctrl+F11, затем перетащите мышью", L"Настройки"},
    {L"Türkçe",
        L"KALKIŞ", L"TAHMİNİ VARIŞ", L"Kalan süre: ", L"YAKIT", L"YORGUNLUK", L"MOLA VER!", L"YEREL SAAT",
        L"G-KUVVETİ", L"MAKS", L"km/s", L"GPS rotası bekleniyor",
        L"Ctrl+F11 ve Ctrl+F12 kullanımda (başka program)", L"Ctrl+F11 kullanımda (başka program)", L"Ctrl+F12 kullanımda (başka program)",
        L"NEREDEN", L"NEREYE", L"Aktif iş yok", L"varış ", L"KALAN", L"SA:DK",
        L"Pencere saydamlığı", L"Tüm paneller", L"Arka plan", L"Dil", L"Koyu (karbon)", L"Açık",
        L"Ayrı öğeler", L"G-metre ayrı öğe olarak", L"Rota ayrı: nereden → nereye, kalan süre",
        L"Taşıma: Ctrl+F11, ardından fareyle sürükleyin", L"Ayarlar"},
    {L"Deutsch",
        L"ABFAHRT", L"ANKUNFT (CA.)", L"Restzeit: ", L"KRAFTSTOFF", L"MÜDIGKEIT", L"PAUSE!", L"ORTSZEIT",
        L"G-KRAFT", L"MAX", L"km/h", L"Warte auf GPS-Route",
        L"Strg+F11 und Strg+F12 belegt (anderes Programm)", L"Strg+F11 belegt (anderes Programm)", L"Strg+F12 belegt (anderes Programm)",
        L"VON", L"NACH", L"Kein aktiver Auftrag", L"Ankunft ", L"RESTZEIT", L"STD:MIN",
        L"Fenstertransparenz", L"Alle Panels", L"Hintergrund", L"Sprache", L"Dunkel (Carbon)", L"Hell",
        L"Separate Elemente", L"G-Meter als separates Element", L"Route separat: von → nach, Restzeit",
        L"Verschieben: Strg+F11, dann mit der Maus ziehen", L"Einstellungen"},
    {L"Polski",
        L"WYJAZD", L"SZAC. PRZYJAZD", L"Pozostały czas: ", L"PALIWO", L"ZMĘCZENIE", L"ODPOCZNIJ!", L"CZAS LOKALNY",
        L"SIŁA G", L"MAKS", L"km/h", L"Oczekiwanie na trasę GPS",
        L"Ctrl+F11 i Ctrl+F12 zajęte (inny program)", L"Ctrl+F11 zajęty (inny program)", L"Ctrl+F12 zajęty (inny program)",
        L"SKĄD", L"DOKĄD", L"Brak aktywnego zlecenia", L"przyjazd ", L"POZOSTAŁO", L"GODZ:MIN",
        L"Przezroczystość okna", L"Wszystkie panele", L"Tło", L"Język", L"Ciemny (karbon)", L"Jasny",
        L"Osobne elementy", L"G-metr jako osobny element", L"Trasa osobno: skąd → dokąd, pozostały czas",
        L"Przenoszenie: Ctrl+F11, potem przeciągnij myszą", L"Ustawienia"},
};
constexpr int kLanguageCount = static_cast<int>(sizeof(kTexts) / sizeof(kTexts[0]));
constexpr const wchar_t* kLanguageCodes[] = {L"en", L"hu", L"ru", L"tr", L"de", L"pl"};  // settings.ini values
static_assert(sizeof(kLanguageCodes) / sizeof(kLanguageCodes[0]) == sizeof(kTexts) / sizeof(kTexts[0]), "one code per language");
int g_language = 0;                    // index into kTexts (0 = English, the default for a new install)
const UiText* g_txt = &kTexts[0];
constexpr Rgb kGood{88, 214, 140};
constexpr Rgb kCaution{255, 184, 64};
constexpr Rgb kWarning{255, 84, 84};
constexpr Rgb kNeedle{255, 112, 64};

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kMoveHotkeyId = 1;
constexpr UINT kOpacityHotkeyId = 2;
constexpr int kMaxTransparency = 80;
constexpr int kDefaultTransparency = 4;
constexpr int kOpacityMenuWidth = 380;
constexpr int kOpacityMenuHeight = 318;
// Default number of in-game minutes treated as "fully rested". If the game
// reports a larger value it is used instead; settings.ini can override it.
constexpr int kRestStateFullMinutes = 600;
constexpr double kPi = 3.14159265358979323846;

constexpr int kHotkeyMoveFailed = 1;
constexpr int kHotkeyOpacityFailed = 2;

// ---------------------------------------------------------------------------
// Telemetry state (written on the game thread, read on the window thread).
// ---------------------------------------------------------------------------
std::atomic<float> g_distanceMeters{0.0f};
std::atomic<float> g_speedMps{0.0f};
std::atomic<float> g_speedLimitMps{0.0f};
std::atomic<float> g_fuelLiters{0.0f};
std::atomic<float> g_fuelCapacityLiters{0.0f};
std::atomic<float> g_navigationTimeSeconds{0.0f};
std::atomic<float> g_localScale{0.0f};
std::atomic<scs_u32_t> g_gameTimeMinutes{0};
std::atomic<scs_s32_t> g_restStopMinutes{0};
std::atomic<scs_s32_t> g_restStopMaxMinutes{0};
std::atomic<bool> g_hasDistance{false};
std::atomic<bool> g_hasNavigationTime{false};
std::atomic<bool> g_hasLocalScale{false};
std::atomic<bool> g_hasSpeedLimit{false};
std::atomic<bool> g_hasFuel{false};
std::atomic<bool> g_hasRestStop{false};
std::atomic<bool> g_hasGameTime{false};
std::atomic<bool> g_leftBlinkerLit{false};   // left blinker lamp currently on (follows the flashing)
std::atomic<bool> g_rightBlinkerLit{false};  // right blinker lamp currently on
std::atomic<bool> g_parkingLights{false};    // truck.light.parking
std::atomic<bool> g_lowBeam{false};          // truck.light.beam.low
std::atomic<bool> g_highBeam{false};         // truck.light.beam.high
std::atomic<float> g_accelX{0.0f};           // truck.local.acceleration.linear, x (right), m/s^2
std::atomic<float> g_accelZ{0.0f};           // truck.local.acceleration.linear, z (backwards), m/s^2
std::atomic<bool> g_hasAccel{false};

// Current job (telemetry "job" configuration): where from and where to.
struct JobInfo {
    bool active = false;
    std::wstring sourceCity, sourceCompany, destinationCity, destinationCompany;
};
std::mutex g_jobMutex;
JobInfo g_job;  // guarded by g_jobMutex

JobInfo ReadJob() {
    std::lock_guard<std::mutex> lock(g_jobMutex);
    return g_job;
}
std::atomic<bool> g_running{false};
std::atomic<bool> g_driverActive{false};
std::atomic<double> g_initialDistance{0.0};
std::atomic<double> g_previousDistance{0.0};
std::atomic<long long> g_routeStartedMs{0};
std::atomic<long long> g_routeStartedEpoch{0};
std::atomic<bool> g_hasRoute{false};
std::atomic<bool> g_tripStarted{false};
std::atomic<bool> g_tripCompleted{false};
std::atomic<long long> g_tripCompletedEpoch{0};
std::atomic<double> g_tripElapsedAtCompletion{0.0};

// Hotkey registration result (set on the window thread, logged on the game thread).
std::atomic<int> g_hotkeyFailures{0};
std::atomic<bool> g_hotkeyWarningPending{false};
scs_log_t g_log = nullptr; // only used on the game thread

// ---------------------------------------------------------------------------
// Window state (window thread only, except g_window).
// ---------------------------------------------------------------------------
std::thread g_windowThread;
std::atomic<HWND> g_window{nullptr};
HWND g_opacityMenu = nullptr;
HWND g_opacitySlider = nullptr;
HINSTANCE g_module = nullptr;
HWND g_gameWindow = nullptr;
bool g_dragMode = false;
bool g_overlayVisible = false;
long long g_lastWindowRefreshMs = 0;
int g_transparency = kDefaultTransparency;
int g_fatigueFullMinutesOverride = 0;
struct WindowPosition { int x; int y; bool saved; };
WindowPosition g_position{0, 0, false};        // main panel
WindowPosition g_gPosition{0, 0, false};       // detached G-meter
WindowPosition g_routePosition{0, 0, false};   // detached route card
bool g_gmeterSeparate = false;
bool g_routeSeparate = false;
HWND g_gWindow = nullptr;
HWND g_routeWindow = nullptr;
HWND g_gCheck = nullptr;
HWND g_routeCheck = nullptr;
HWND g_themeCombo = nullptr;
HWND g_languageCombo = nullptr;
HBRUSH g_menuBrush = nullptr;
constexpr COLORREF kMenuBackground = RGB(20, 24, 28);
constexpr int kGCheckId = 11;
constexpr int kRouteCheckId = 12;
constexpr int kThemeComboId = 13;
constexpr int kLanguageComboId = 14;

struct Fonts {
    HFONT label = nullptr;       // column headings
    HFONT value = nullptr;       // departure / arrival times
    HFONT footer = nullptr;      // remaining time and distance
    HFONT smallLabel = nullptr;  // fuel / fatigue headings
    HFONT unit = nullptr;        // km/h
    HFONT gauge = nullptr;       // E / F
    HFONT gaugeHalf = nullptr;   // 1/2
    HFONT limit = nullptr;       // speed limit, two digits
    HFONT limitWide = nullptr;   // speed limit, three digits
    HFONT restTime = nullptr;    // time to next rest
    HFONT clock = nullptr;       // local time
    HFONT routeCity = nullptr;   // route card: city names
    HFONT routeCompany = nullptr; // route card: company names
    HFONT routeTime = nullptr;   // route card: time left
    HFONT menuTitle = nullptr;
    HFONT menuLabel = nullptr;
    HFONT menuValue = nullptr;
} g_fonts;

long long SteadyMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
std::wstring SettingsPath() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return L"";
    std::wstring folder = std::wstring(base) + L"\\HaulETA-Simple";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\settings.ini";
}

void LoadSettings() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    const wchar_t* file = path.c_str();
    // [simple] is the current section; [window] is read as a fallback for older settings files.
    const int legacyX = GetPrivateProfileIntW(L"window", L"x", 0, file);
    const int legacyY = GetPrivateProfileIntW(L"window", L"y", 0, file);
    const int legacySaved = GetPrivateProfileIntW(L"window", L"saved", 0, file);
    g_position.x = GetPrivateProfileIntW(L"simple", L"x", legacyX, file);
    g_position.y = GetPrivateProfileIntW(L"simple", L"y", legacyY, file);
    g_position.saved = GetPrivateProfileIntW(L"simple", L"saved", legacySaved, file) != 0;
    g_transparency = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"transparency", L"eta",
        kDefaultTransparency, file)), 0, kMaxTransparency);
    g_fatigueFullMinutesOverride = std::max(0, static_cast<int>(
        GetPrivateProfileIntW(L"fatigue", L"full_minutes", 0, file)));
    auto readPosition = [file](const wchar_t* section) {
        return WindowPosition{static_cast<int>(GetPrivateProfileIntW(section, L"x", 0, file)),
            static_cast<int>(GetPrivateProfileIntW(section, L"y", 0, file)),
            GetPrivateProfileIntW(section, L"saved", 0, file) != 0};
    };
    g_gPosition = readPosition(L"gmeter");
    g_routePosition = readPosition(L"routecard");
    g_gmeterSeparate = GetPrivateProfileIntW(L"layout", L"gmeter_separate", 0, file) != 0;
    g_routeSeparate = GetPrivateProfileIntW(L"layout", L"route_separate", 0, file) != 0;
    g_pal = GetPrivateProfileIntW(L"appearance", L"theme", 0, file) == 1 ? kLightPalette : kDarkPalette;
    wchar_t code[16]{};
    // No language saved yet (first install) or an unknown code: English.
    GetPrivateProfileStringW(L"appearance", L"language", L"en", code, _countof(code), file);
    g_language = 0;
    for (int i = 0; i < kLanguageCount; ++i)
        if (_wcsicmp(code, kLanguageCodes[i]) == 0) g_language = i;
    g_txt = &kTexts[g_language];
}

// Saves a window's position as the top-left corner of its panel, inside the glow margin.
void SavePosition(HWND window, const wchar_t* section, WindowPosition& position) {
    if (!window) return;
    RECT r{};
    if (!GetWindowRect(window, &r)) return;
    const auto path = SettingsPath();
    if (path.empty()) return;
    wchar_t value[32]{};
    const int x = r.left + kGlowMargin;
    const int y = r.top + kGlowMargin;
    _itow_s(x, value, 10); WritePrivateProfileStringW(section, L"x", value, path.c_str());
    _itow_s(y, value, 10); WritePrivateProfileStringW(section, L"y", value, path.c_str());
    WritePrivateProfileStringW(section, L"saved", L"1", path.c_str());
    position = {x, y, true};
}

// Saves the positions of all windows that are currently shown (after moving them with Ctrl+F11).
void SaveAllPositions() {
    if (HWND main = g_window.load()) SavePosition(main, L"simple", g_position);
    if (g_gmeterSeparate && g_gWindow && IsWindowVisible(g_gWindow)) SavePosition(g_gWindow, L"gmeter", g_gPosition);
    if (g_routeSeparate && g_routeWindow && IsWindowVisible(g_routeWindow)) SavePosition(g_routeWindow, L"routecard", g_routePosition);
}

void SaveLayout() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    WritePrivateProfileStringW(L"layout", L"gmeter_separate", g_gmeterSeparate ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"route_separate", g_routeSeparate ? L"1" : L"0", path.c_str());
}

void SaveTheme() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    WritePrivateProfileStringW(L"appearance", L"theme", g_pal.light ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"appearance", L"language", kLanguageCodes[g_language], path.c_str());
}

void SaveTransparency() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    wchar_t value[16]{};
    _itow_s(g_transparency, value, 10);
    WritePrivateProfileStringW(L"transparency", L"eta", value, path.c_str());
}

void PresentAll(bool rebuild = true, bool gMeterOnly = false);

// The transparency is applied as the constant alpha of UpdateLayeredWindow, so re-present all windows.
void ApplyWindowTransparency() {
    PresentAll();
}

// ---------------------------------------------------------------------------
// Telemetry callbacks (game thread)
// ---------------------------------------------------------------------------
void FlushPendingWarnings() {
    if (!g_log || !g_hotkeyWarningPending.exchange(false)) return;
    const int failures = g_hotkeyFailures.load();
    if (failures & kHotkeyMoveFailed)
        g_log(SCS_LOG_TYPE_warning, "HaulETA: Ctrl+F11 is already used by another program; panel moving is unavailable.");
    if (failures & kHotkeyOpacityFailed)
        g_log(SCS_LOG_TYPE_warning, "HaulETA: Ctrl+F12 is already used by another program; the transparency slider is unavailable.");
}

void StartTripIfMoving(float speed) {
    if (speed <= 1.0f || !g_hasRoute.load() || g_tripStarted.load()) return;
    const auto epoch = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    g_initialDistance = g_distanceMeters.load();
    g_routeStartedMs = SteadyMillis();
    g_routeStartedEpoch = static_cast<long long>(epoch);
    g_tripStarted = true;
}

void StoreSpeed(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    FlushPendingWarnings();
    if (!value || value->type != SCS_VALUE_TYPE_float) return;
    const float speed = value->value_float.value;
    g_speedMps = speed;
    StartTripIfMoving(std::abs(speed));
}

void StoreNavigationTime(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_float) {
        g_hasNavigationTime = false;
        return;
    }
    g_navigationTimeSeconds = value->value_float.value;
    g_hasNavigationTime = true;
}

void StoreLocalScale(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_float) {
        g_hasLocalScale = false;
        return;
    }
    g_localScale = value->value_float.value;
    g_hasLocalScale = true;
}

void StoreSpeedLimit(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_float) {
        g_hasSpeedLimit = false;
        return;
    }
    g_speedLimitMps = value->value_float.value;
    g_hasSpeedLimit = true;
}

void StoreFuel(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_float) {
        g_hasFuel = false;
        return;
    }
    g_fuelLiters = std::max(0.0f, value->value_float.value);
    g_hasFuel = true;
}

void StoreRestStop(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_s32) {
        g_hasRestStop = false;
        return;
    }
    const scs_s32_t minutes = value->value_s32.value;
    g_restStopMinutes = minutes;
    if (minutes > g_restStopMaxMinutes.load()) g_restStopMaxMinutes = minutes;
    g_hasRestStop = true;
}

void StoreGameTime(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_u32) {
        g_hasGameTime = false;
        return;
    }
    g_gameTimeMinutes = value->value_u32.value;
    g_hasGameTime = true;
}

// Blinker lamps (truck.light.lblinker / rblinker): they switch on and off with the actual flashing,
// so the panel glow pulses in step with the indicator. The window thread is woken to animate it.
// Head lamps (parking / low beam / high beam): the context points at the matching flag.
// Vehicle-space linear acceleration (X right, Y up, Z backwards) for the G-meter.
void StoreAcceleration(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_fvector) {
        g_hasAccel = false;
        return;
    }
    g_accelX = value->value_fvector.x;
    g_accelZ = value->value_fvector.z;
    g_hasAccel = true;
}

void StoreLamp(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t context) {
    auto* state = static_cast<std::atomic<bool>*>(context);
    if (!state) return;
    const bool on = value && value->type == SCS_VALUE_TYPE_bool && value->value_bool.value != 0;
    if (state->exchange(on) == on) return;
    if (HWND window = g_window.load()) PostMessageW(window, kGlowMessage, 0, 0);
}

void StoreBlinker(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t context) {
    const bool lit = value && value->type == SCS_VALUE_TYPE_bool && value->value_bool.value != 0;
    auto& state = context ? g_rightBlinkerLit : g_leftBlinkerLit;  // context: nullptr = left, non-null = right
    if (state.exchange(lit) == lit) return;
    if (HWND window = g_window.load()) PostMessageW(window, kGlowMessage, 0, 0);
}

void StoreDistance(const scs_string_t, const scs_u32_t, const scs_value_t* value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_float) {
        g_hasDistance = false;
        g_hasRoute = false;
        return;
    }
    const double distance = std::max(0.0, static_cast<double>(value->value_float.value));
    const double previous = g_previousDistance.load();
    const bool newRoute = !g_hasRoute.load() || g_tripCompleted.load() ||
        distance > previous + std::max(3000.0, previous * 0.10);
    if (distance <= 5.0) {
        if (g_hasRoute.load() && g_tripStarted.load()) {
            const long long started = g_routeStartedMs.load();
            g_tripElapsedAtCompletion = std::max<long long>(0, SteadyMillis() - started) / 1000.0;
            g_tripCompletedEpoch = static_cast<long long>(
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
            g_tripCompleted = true;
        }
        g_hasRoute = false;
        g_distanceMeters = static_cast<float>(distance);
        g_hasDistance = true;
        return;
    }
    if (newRoute) {
        g_initialDistance = distance;
        g_routeStartedMs = 0;
        g_routeStartedEpoch = 0;
        g_tripStarted = false;
        g_tripCompleted = false;
        g_tripCompletedEpoch = 0;
        g_tripElapsedAtCompletion = 0.0;
    }
    g_previousDistance = distance;
    g_hasRoute = true;
    g_distanceMeters = static_cast<float>(distance);
    g_hasDistance = true;
    StartTripIfMoving(std::abs(g_speedMps.load()));
}

void SCSAPIFUNC OnTelemetryEvent(const scs_event_t event, const void* eventInfo, const scs_context_t) {
    FlushPendingWarnings();
    if (event == SCS_TELEMETRY_EVENT_started) g_driverActive = true;
    else if (event == SCS_TELEMETRY_EVENT_paused) g_driverActive = false;
    else if (event == SCS_TELEMETRY_EVENT_configuration && eventInfo) {
        const auto* configuration = static_cast<const scs_telemetry_configuration_t*>(eventInfo);
        if (configuration->id && std::strcmp(configuration->id, SCS_TELEMETRY_CONFIG_job) == 0) {
            // An empty attribute set means there is no job.
            auto widen = [](const char* text) {
                std::wstring result;
                if (!text || !*text) return result;
                const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
                if (length <= 1) return result;
                result.resize(static_cast<size_t>(length) - 1);
                MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
                return result;
            };
            JobInfo job;
            for (const scs_named_value_t* attribute = configuration->attributes; attribute && attribute->name; ++attribute) {
                job.active = true;
                if (attribute->value.type != SCS_VALUE_TYPE_string) continue;
                const char* text = attribute->value.value_string.value;
                if (std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city) == 0) job.sourceCity = widen(text);
                else if (std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company) == 0) job.sourceCompany = widen(text);
                else if (std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city) == 0) job.destinationCity = widen(text);
                else if (std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company) == 0) job.destinationCompany = widen(text);
            }
            std::lock_guard<std::mutex> lock(g_jobMutex);
            g_job = std::move(job);
            return;
        }
        if (!configuration->id || std::strcmp(configuration->id, SCS_TELEMETRY_CONFIG_truck) != 0) return;
        float capacity = 0.0f;
        for (const scs_named_value_t* attribute = configuration->attributes;
            attribute && attribute->name; ++attribute) {
            if (std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_fuel_capacity) == 0 &&
                attribute->value.type == SCS_VALUE_TYPE_float) {
                capacity = std::max(0.0f, attribute->value.value_float.value);
                break;
            }
        }
        g_fuelCapacityLiters = capacity;
    }
}

// ---------------------------------------------------------------------------
// Estimates
// ---------------------------------------------------------------------------
double EstimateRemainingRealSeconds() {
    const double distance = g_distanceMeters.load();
    if (!g_hasDistance.load() || !g_hasRoute.load() || distance <= 5.0) return -1.0;

    const double navigationTime = g_navigationTimeSeconds.load();
    const double localScale = g_localScale.load();
    if (g_hasNavigationTime.load() && g_hasLocalScale.load() && navigationTime > 0.0 && localScale > 0.0) {
        // ETS2/ATS report route time in simulated seconds. local.scale is simulated seconds per real second.
        return std::clamp(navigationTime / localScale, 0.0, 7.0 * 24.0 * 60.0 * 60.0);
    }

    // Fallback: derive actual route progress in metres per real second after a short warm-up.
    if (g_tripStarted.load()) {
        const long long elapsedMs = std::max<long long>(0, SteadyMillis() - g_routeStartedMs.load());
        const double travelled = std::max(0.0, g_initialDistance.load() - distance);
        if (elapsedMs >= 10000 && travelled >= 120.0) {
            const double observedMetresPerSecond = travelled / (static_cast<double>(elapsedMs) / 1000.0);
            return distance / std::clamp(observedMetresPerSecond, 0.5, 80.0);
        }
    }

    const double speed = std::abs(static_cast<double>(g_speedMps.load()));
    if (g_hasLocalScale.load() && localScale > 0.0 && speed > 1.0)
        return distance / (speed * localScale);
    return -1.0;
}

double RestedFraction() {
    const int observedMax = g_restStopMaxMinutes.load();
    const int fullMinutes = g_fatigueFullMinutesOverride > 0 ?
        g_fatigueFullMinutesOverride : std::max(kRestStateFullMinutes, observedMax);
    return std::clamp(static_cast<double>(g_restStopMinutes.load()) / fullMinutes, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Fonts (Bahnschrift when installed, Segoe UI otherwise)
// ---------------------------------------------------------------------------
bool FontInstalled(const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(query.lfFaceName, face, _TRUNCATE);
    bool found = false;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    EnumFontFamiliesExW(screen, &query,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) -> int {
            *reinterpret_cast<bool*>(found) = true;
            return 0;
        }, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, screen);
    return found;
}

HFONT MakeFont(int height, bool semibold) {
    static const bool hasBahnschrift = FontInstalled(L"Bahnschrift");
    static const bool hasBahnschriftSemiBold = FontInstalled(L"Bahnschrift SemiBold");
    const wchar_t* face = L"Segoe UI";
    int weight = semibold ? FW_SEMIBOLD : FW_NORMAL;
    if (semibold && hasBahnschriftSemiBold) {
        face = L"Bahnschrift SemiBold";
        weight = FW_NORMAL;
    } else if (hasBahnschrift) {
        face = L"Bahnschrift";
    }
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, face);
}

void CreateFonts() {
    g_fonts.label = MakeFont(14, true);
    g_fonts.value = MakeFont(36, true);
    g_fonts.footer = MakeFont(16, false);
    g_fonts.smallLabel = MakeFont(13, true);
    g_fonts.unit = MakeFont(14, true);
    g_fonts.gauge = MakeFont(13, true);
    g_fonts.gaugeHalf = MakeFont(11, true);
    g_fonts.limit = MakeFont(23, true);
    g_fonts.limitWide = MakeFont(18, true);
    g_fonts.restTime = MakeFont(20, true);
    g_fonts.clock = MakeFont(34, true);
    g_fonts.routeCity = MakeFont(21, true);
    g_fonts.routeCompany = MakeFont(14, false);
    g_fonts.routeTime = MakeFont(31, true);
    g_fonts.menuTitle = MakeFont(20, true);
    g_fonts.menuLabel = MakeFont(16, false);
    g_fonts.menuValue = MakeFont(16, true);
}

void DestroyFonts() {
    HFONT* all[] = {&g_fonts.label, &g_fonts.value, &g_fonts.footer, &g_fonts.smallLabel,
        &g_fonts.unit, &g_fonts.gauge, &g_fonts.gaugeHalf, &g_fonts.limit, &g_fonts.limitWide, &g_fonts.restTime,
        &g_fonts.clock, &g_fonts.routeCity, &g_fonts.routeCompany, &g_fonts.routeTime, &g_fonts.menuTitle, &g_fonts.menuLabel, &g_fonts.menuValue};
    for (HFONT* font : all) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Canvas: the panel is rendered into a 32-bit top-down DIB. Shapes are filled
// with anti-aliased signed-distance functions straight into the pixel buffer;
// text is drawn with GDI into the same DIB. The finished image is shown with
// per-pixel alpha (UpdateLayeredWindow), so the rounded corners are smooth.
// ---------------------------------------------------------------------------
COLORREF Ref(Rgb c) { return RGB(c.r, c.g, c.b); }

Rgb Mix(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {static_cast<int>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<int>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<int>(std::lround(a.b + (b.b - a.b) * t))};
}

Rgb Scale(Rgb c, double factor) {
    return {std::clamp(static_cast<int>(c.r * factor), 0, 255),
        std::clamp(static_cast<int>(c.g * factor), 0, 255),
        std::clamp(static_cast<int>(c.b * factor), 0, 255)};
}

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;  // 0x00RRGGBB, top-down
    bool Valid() const { return width > 0 && height > 0; }
    Rgb At(int x, int y) const {
        const uint32_t p = pixels[static_cast<size_t>(std::clamp(y, 0, height - 1)) * width +
            std::clamp(x, 0, width - 1)];
        return {static_cast<int>((p >> 16) & 255), static_cast<int>((p >> 8) & 255), static_cast<int>(p & 255)};
    }
};

struct Canvas {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    uint32_t* pixels = nullptr;
};
Canvas g_canvas;       // the full panel with every column (kPanelWidth x kPanelHeight)
Canvas g_routeCanvas;  // the detached route card (kRouteCardWidth x kPanelHeight)

// Blinker glow (window thread). Levels run 0..1 and follow the lamps with a short fade.
double g_lightWhite = 0.0;  // headlight frame glow, white part (parking / low beam)
double g_lightBlue = 0.0;   // headlight frame glow, blue part (high beam)
double g_glowLeft = 0.0;
double g_glowRight = 0.0;
long long g_glowLastMs = 0;
bool g_glowTimerRunning = false;
std::vector<uint32_t> g_panelBase;  // last fully rendered panel, before glow and alpha mask
std::vector<uint32_t> g_routeBase;  // last rendered route card
constexpr Rgb kGlowHalo{255, 166, 34};
constexpr Rgb kGlowRim{255, 190, 72};
constexpr Rgb kLightWhiteHalo{226, 236, 255};  // slightly cool white, like LED headlights
constexpr Rgb kLightWhiteRim{238, 244, 255};
constexpr Rgb kLightBlueHalo{52, 132, 255};
constexpr Rgb kLightBlueRim{96, 166, 255};

// Colour and strength of the edge glow for one window column, combined from the headlight
// glow (whole frame) and the blinker glow (its side), filled per frame by PrepareGlowColumns.
struct GlowColumn { double level; double r, g, b; double rimR, rimG, rimB; };

// One on-screen panel (the main panel, or a detached card): its assembled pixels, the tables for
// its rounded alpha mask and edge glow, and the layered-window image (panel plus glow margin).
struct Surface {
    int width = 0, height = 0;              // panel size
    int windowWidth = 0, windowHeight = 0;  // panel plus kGlowMargin on every side
    std::vector<uint32_t> panel;            // assembled panel pixels (opaque)
    std::vector<uint8_t> mask;              // anti-aliased rounded-corner coverage
    std::vector<float> inner;               // brightness of the lit rim inside the edge
    std::vector<float> halo;                // brightness of the glow outside the edge (window-sized)
    std::vector<float> shadow;              // light theme drop shadow (window-sized alpha)
    std::vector<float> leftSide, rightSide; // per window column: how much each blinker lights it
    std::vector<GlowColumn> columns;        // per window column glow colour, filled per frame
    bool glowActive = false;
    Canvas frame;                           // DIB for UpdateLayeredWindow
};
Surface g_mainSurface;
Surface g_gSurface;
Surface g_routeSurface;

Texture g_carbon;
Texture g_metalGauge;
Texture g_metalClock;
Texture g_metalGMeter;
Texture g_paper;  // light theme background

// G-meter state (window thread). Values are the force felt by the passengers, in g:
// right = sideways to the right, forward = pushed forward (braking).
struct GSample { double right, forward; };
struct GMeterState {
    double right = 0.0;
    double forward = 0.0;
    double peak = 0.0;
    long long peakMs = 0;
    long long lastMs = 0;
    GSample trail[kGTrailLength]{};
    int trailCount = 0;
    int trailHead = 0;  // index of the newest sample
} g_gmeter;
std::vector<uint32_t> g_background;       // prepared background of the full panel
std::vector<uint32_t> g_routeBackground;  // prepared background of the route card
Texture g_diffuser;                       // frosted plastic that breaks up the edge glow
double g_diffuserMean = 0.0;

Texture LoadTexture(int resourceId) {
    Texture texture;
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(g_module, MAKEINTRESOURCEW(resourceId),
        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!bitmap) return texture;
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth > 0 && info.bmHeight > 0) {
        BITMAPINFO request{};
        request.bmiHeader.biSize = sizeof(request.bmiHeader);
        request.bmiHeader.biWidth = info.bmWidth;
        request.bmiHeader.biHeight = -info.bmHeight;  // top-down
        request.bmiHeader.biPlanes = 1;
        request.bmiHeader.biBitCount = 32;
        request.bmiHeader.biCompression = BI_RGB;
        texture.pixels.resize(static_cast<size_t>(info.bmWidth) * info.bmHeight);
        HDC screen = GetDC(nullptr);
        const int lines = screen ? GetDIBits(screen, bitmap, 0, info.bmHeight, texture.pixels.data(),
            &request, DIB_RGB_COLORS) : 0;
        if (screen) ReleaseDC(nullptr, screen);
        if (lines == info.bmHeight) {
            texture.width = info.bmWidth;
            texture.height = info.bmHeight;
        } else {
            texture.pixels.clear();
        }
    }
    DeleteObject(bitmap);
    return texture;
}

uint32_t Pack(Rgb c) {
    return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// The pixel buffer that BlendPixel / FillShape draw into (the full panel, a detached card, ...).
struct PixelTarget { uint32_t* pixels = nullptr; int width = 0; int height = 0; };
PixelTarget g_target;
struct TargetScope {
    PixelTarget saved;
    TargetScope(uint32_t* pixels, int width, int height) : saved(g_target) { g_target = {pixels, width, height}; }
    ~TargetScope() { g_target = saved; }
};

void BlendPixel(int x, int y, Rgb c, double alpha) {
    if (alpha <= 0.0 || !g_target.pixels || x < 0 || y < 0 || x >= g_target.width || y >= g_target.height) return;
    uint32_t& p = g_target.pixels[static_cast<size_t>(y) * g_target.width + x];
    const Rgb under{static_cast<int>((p >> 16) & 255), static_cast<int>((p >> 8) & 255), static_cast<int>(p & 255)};
    p = Pack(Mix(under, c, std::min(alpha, 1.0)));
}

// Fills every pixel of the box whose centre lies inside the shape (signed distance < 0),
// with a soft edge of `soft` pixels for anti-aliasing (or larger values for shadows).
template <typename Sdf, typename Color>
void FillShape(double left, double top, double right, double bottom, Sdf sdf, Color color,
    double opacity = 1.0, double soft = 1.0) {
    const int x0 = std::max(0, static_cast<int>(std::floor(left)));
    const int y0 = std::max(0, static_cast<int>(std::floor(top)));
    const int x1 = std::min(g_target.width, static_cast<int>(std::ceil(right)));
    const int y1 = std::min(g_target.height, static_cast<int>(std::ceil(bottom)));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double coverage = std::clamp(0.5 - sdf(px, py) / soft, 0.0, 1.0);
            if (coverage > 0.0) BlendPixel(x, y, color(px, py), coverage * opacity);
        }
    }
}

auto Solid(Rgb c) { return [c](double, double) { return c; }; }

double SdRoundRect(double px, double py, double left, double top, double right, double bottom, double radius) {
    const double hx = (right - left) / 2.0 - radius;
    const double hy = (bottom - top) / 2.0 - radius;
    const double qx = std::abs(px - (left + right) / 2.0) - hx;
    const double qy = std::abs(py - (top + bottom) / 2.0) - hy;
    const double ox = std::max(qx, 0.0);
    const double oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - radius;
}

double SdCircle(double px, double py, double cx, double cy, double radius) {
    return std::hypot(px - cx, py - cy) - radius;
}

// Capsule: a line from (ax, ay) to (bx, by) with round ends, `radius` = half the line width.
double SdSegment(double px, double py, double ax, double ay, double bx, double by, double radius) {
    const double pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const double length2 = bax * bax + bay * bay;
    const double h = length2 > 0.0 ? std::clamp((pax * bax + pay * bay) / length2, 0.0, 1.0) : 0.0;
    return std::hypot(pax - bax * h, pay - bay * h) - radius;
}

// Ring arc from angle a0 to a1 (radians, a0 <= a1, y axis down like PointOnCircle) with round caps.
double SdArc(double px, double py, double cx, double cy, double radius, double halfWidth, double a0, double a1) {
    double angle = std::atan2(py - cy, px - cx);
    while (angle < a0) angle += 2.0 * kPi;
    while (angle >= a0 + 2.0 * kPi) angle -= 2.0 * kPi;
    if (angle <= a1) return std::abs(std::hypot(px - cx, py - cy) - radius) - halfWidth;
    const double end0 = std::hypot(px - (cx + std::cos(a0) * radius), py - (cy + std::sin(a0) * radius));
    const double end1 = std::hypot(px - (cx + std::cos(a1) * radius), py - (cy + std::sin(a1) * radius));
    return std::min(end0, end1) - halfWidth;
}

// Convex polygon: the largest signed distance to its edge lines (exact on the edges).
template <size_t N>
double SdConvex(double px, double py, const double (&points)[N][2]) {
    double centroidX = 0.0, centroidY = 0.0;
    for (const auto& p : points) { centroidX += p[0] / N; centroidY += p[1] / N; }
    double distance = -1e9;
    for (size_t i = 0; i < N; ++i) {
        const double* a = points[i];
        const double* b = points[(i + 1) % N];
        double nx = b[1] - a[1], ny = a[0] - b[0];
        const double length = std::hypot(nx, ny);
        if (length <= 0.0) continue;
        nx /= length; ny /= length;
        if ((centroidX - a[0]) * nx + (centroidY - a[1]) * ny > 0.0) { nx = -nx; ny = -ny; }
        distance = std::max(distance, (px - a[0]) * nx + (py - a[1]) * ny);
    }
    return distance;
}

// Background of a panel of the given width. Dark theme: the carbon weave darkened, slightly
// cool-tinted, lit from the top and darkened towards the left and right ends. Light theme: white
// paper with its grain slightly strengthened and a gentle top-to-bottom light falloff.
std::vector<uint32_t> BuildBackground(int width) {
    std::vector<uint32_t> background(static_cast<size_t>(width) * kPanelHeight, 0);
    for (int y = 0; y < kPanelHeight; ++y) {
        const double t = static_cast<double>(y) / (kPanelHeight - 1);
        for (int x = 0; x < width; ++x) {
            if (g_pal.light) {
                const Rgb paper = g_paper.Valid() ? g_paper.At(x % g_paper.width, y % g_paper.height) : Rgb{243, 243, 243};
                const double grain = ((paper.r + paper.g + paper.b) / 3.0 - 243.0) * 0.9;
                const double v = 249.0 + grain + 3.0 * (1.0 - t) - 5.0 * t;
                background[static_cast<size_t>(y) * width + x] = Pack(Rgb{std::clamp(static_cast<int>(v - 2), 0, 255),
                    std::clamp(static_cast<int>(v - 1), 0, 255), std::clamp(static_cast<int>(v + 1), 0, 255)});
                continue;
            }
            const Rgb weave = g_carbon.Valid() ? g_carbon.At(x % g_carbon.width, y % g_carbon.height) : Rgb{52, 56, 60};
            const double dx = std::abs(x - width / 2.0) / (width / 2.0);
            const double light = (0.50 + 0.24 * (1.0 - t)) * (1.0 - 0.16 * dx * dx);
            const Rgb tinted{static_cast<int>(weave.r * light) + 5, static_cast<int>(weave.g * light) + 8,
                static_cast<int>(weave.b * light) + 12};
            background[static_cast<size_t>(y) * width + x] = Pack(Scale(tinted, 1.0));
        }
    }
    return background;
}

bool CreateCanvas(Canvas& canvas, int width, int height) {
    if (canvas.dc) return true;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return false;
    }
    canvas.dc = dc;
    canvas.bitmap = bitmap;
    canvas.oldBitmap = SelectObject(dc, bitmap);
    canvas.pixels = static_cast<uint32_t*>(bits);
    SetBkMode(dc, TRANSPARENT);
    return true;
}

void DestroyCanvas(Canvas& canvas) {
    if (!canvas.dc) return;
    SelectObject(canvas.dc, canvas.oldBitmap);
    DeleteObject(canvas.bitmap);
    DeleteDC(canvas.dc);
    canvas = Canvas{};
}

// Prepares a surface for a panel of width x height: alpha mask, glow tables and window image.
// Nothing is rebuilt if the size has not changed.
void BuildSurface(Surface& s, int width, int height) {
    if (s.width == width && s.height == height && !s.mask.empty()) return;
    s.width = width;
    s.height = height;
    s.windowWidth = width + 2 * kGlowMargin;
    s.windowHeight = height + 2 * kGlowMargin;
    s.panel.assign(static_cast<size_t>(width) * height, 0);
    DestroyCanvas(s.frame);
    CreateCanvas(s.frame, s.windowWidth, s.windowHeight);

    // The frosted-plastic texture breaks the light up slightly, as if it shone through a
    // diffuser behind the panel: its brightness is normalised around 1.
    auto diffuse = [](int wx, int wy) {
        if (!g_diffuser.Valid() || g_diffuserMean <= 0.0) return 1.0;
        const Rgb c = g_diffuser.At(wx % g_diffuser.width, wy % g_diffuser.height);
        return 0.55 + 0.45 * std::clamp((c.r + c.g + c.b) / (3.0 * g_diffuserMean), 0.4, 1.8);
    };
    s.mask.assign(static_cast<size_t>(width) * height, 0);
    s.inner.assign(static_cast<size_t>(width) * height, 0.0f);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            const double d = SdRoundRect(x + 0.5, y + 0.5, 0, 0, width, height, kPanelRadius);
            s.mask[i] = static_cast<uint8_t>(std::lround(std::clamp(0.5 - d, 0.0, 1.0) * 255.0));
            const double inside = std::max(0.0, -d);
            if (inside <= 24.0) {
                const double light = 0.85 * std::exp(-inside / 1.8) + 0.30 * std::exp(-inside / 6.0);
                s.inner[i] = static_cast<float>(light * diffuse(x + kGlowMargin, y + kGlowMargin));
            }
        }
    s.halo.assign(static_cast<size_t>(s.windowWidth) * s.windowHeight, 0.0f);
    for (int wy = 0; wy < s.windowHeight; ++wy)
        for (int wx = 0; wx < s.windowWidth; ++wx) {
            const double d = SdRoundRect(wx + 0.5, wy + 0.5, kGlowMargin, kGlowMargin,
                kGlowMargin + width, kGlowMargin + height, kPanelRadius);
            if (d <= -1.0) continue;  // under the opaque panel
            const double outside = std::max(d, 0.0);
            // A bright core right at the edge plus a wider, softer halo.
            const double light = 1.0 * std::exp(-outside / 3.0) + 0.6 * std::exp(-outside / 7.0);
            const double edgeFade = std::clamp(std::min({wx, wy, s.windowWidth - 1 - wx, s.windowHeight - 1 - wy}) / 4.0, 0.0, 1.0);
            s.halo[static_cast<size_t>(wy) * s.windowWidth + wx] = static_cast<float>(light * edgeFade * diffuse(wx, wy));
        }
    // Light theme: soft drop shadow under the floating white card (slightly offset downwards).
    s.shadow.assign(static_cast<size_t>(s.windowWidth) * s.windowHeight, 0.0f);
    for (int wy = 0; wy < s.windowHeight; ++wy)
        for (int wx = 0; wx < s.windowWidth; ++wx) {
            const double d = SdRoundRect(wx + 0.5, wy + 0.5, kGlowMargin, kGlowMargin + 3.0,
                kGlowMargin + width, kGlowMargin + height + 3.0, kPanelRadius);
            const double edgeFade = std::clamp(std::min({wx, wy, s.windowWidth - 1 - wx, s.windowHeight - 1 - wy}) / 3.0, 0.0, 1.0);
            s.shadow[static_cast<size_t>(wy) * s.windowWidth + wx] =
                static_cast<float>(0.26 * std::exp(-std::max(d, 0.0) / 4.5) * edgeFade);
        }
    // The left blinker lights the left part of the frame and fades out towards the middle; the right one mirrors it.
    s.leftSide.assign(s.windowWidth, 0.0f);
    s.rightSide.assign(s.windowWidth, 0.0f);
    for (int wx = 0; wx < s.windowWidth; ++wx) {
        const double t = std::clamp(((wx - kGlowMargin) - 0.28 * width) / (0.30 * width), 0.0, 1.0);
        const double fromLeft = 1.0 - t * t * (3.0 - 2.0 * t);  // smoothstep
        s.leftSide[wx] = static_cast<float>(fromLeft);
        s.rightSide[s.windowWidth - 1 - wx] = static_cast<float>(fromLeft);
    }
    s.columns.assign(s.windowWidth, GlowColumn{});
}

// Width of the main panel: the full panel minus the columns that are shown as separate cards.
int MainPanelWidth() {
    const int right = g_gmeterSeparate ? kDividerClock : kPanelWidth;
    return g_routeSeparate ? kDividerSpeed + (right - kDividerRoute) : right;
}

void LoadPanelAssets() {
    g_carbon = LoadTexture(kCarbonTextureId);
    g_metalGauge = LoadTexture(kMetalGaugeTextureId);
    g_metalClock = LoadTexture(kMetalClockTextureId);
    g_metalGMeter = LoadTexture(kMetalGMeterTextureId);
    g_paper = LoadTexture(kPaperTextureId);
    g_diffuser = LoadTexture(kDiffuserTextureId);
    g_diffuserMean = 0.0;
    if (g_diffuser.Valid()) {
        for (uint32_t p : g_diffuser.pixels) g_diffuserMean += ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255);
        g_diffuserMean /= 3.0 * g_diffuser.pixels.size();
    }
    g_background = BuildBackground(kPanelWidth);
    g_routeBackground = BuildBackground(kRouteCardWidth);
    BuildSurface(g_mainSurface, MainPanelWidth(), kPanelHeight);
    BuildSurface(g_gSurface, kGCardWidth, kPanelHeight);
    BuildSurface(g_routeSurface, kRouteCardWidth, kPanelHeight);
}

bool EnsureCanvas() {
    return CreateCanvas(g_canvas, kPanelWidth, kPanelHeight) && CreateCanvas(g_routeCanvas, kRouteCardWidth, kPanelHeight);
}

void DestroyCanvas() {
    DestroyCanvas(g_canvas);
    DestroyCanvas(g_routeCanvas);
    for (Surface* s : {&g_mainSurface, &g_gSurface, &g_routeSurface}) {
        DestroyCanvas(s->frame);
        *s = Surface{};
    }
}

// How strongly the blinker glow lights a window column of a surface (0..1).
double GlowLevelAt(const Surface& s, int windowX) {
    return std::max(g_glowLeft * s.leftSide[windowX], g_glowRight * s.rightSide[windowX]);
}

// Headlight glow targets from the lamp switches: high beam wins (blue), then low beam, then parking lights.
void LightTargets(double& white, double& blue) {
    white = 0.0;
    blue = 0.0;
    if (g_highBeam.load()) blue = kHighBeamGlow;
    else if (g_lowBeam.load()) white = kLowBeamGlow;
    else if (g_parkingLights.load()) white = kParkingGlow;
}

// True when every glow level already equals its target (no animation needed).
bool GlowSettled() {
    double white = 0.0, blue = 0.0;
    LightTargets(white, blue);
    return g_glowLeft == (g_leftBlinkerLit.load() ? 1.0 : 0.0) &&
        g_glowRight == (g_rightBlinkerLit.load() ? 1.0 : 0.0) &&
        g_lightWhite == white && g_lightBlue == blue;
}

// Moves the glow levels towards the lamp states; returns true once all have settled.
bool UpdateGlow() {
    const long long now = SteadyMillis();
    const double dt = std::clamp((now - g_glowLastMs) / 1000.0, 0.0, 0.1);
    g_glowLastMs = now;
    auto step = [dt](double& level, double target, double rise, double fall) {
        if (level < target) level = std::min(target, level + dt / rise);
        else if (level > target) level = std::max(target, level - dt / fall);
        return level == target;
    };
    double white = 0.0, blue = 0.0;
    LightTargets(white, blue);
    // Changing between white and blue cross-fades: the outgoing colour fades while the new one rises.
    const bool whiteSettled = step(g_lightWhite, white, kLightRiseSeconds, kLightFallSeconds);
    const bool blueSettled = step(g_lightBlue, blue, kLightRiseSeconds, kLightFallSeconds);
    const bool leftSettled = step(g_glowLeft, g_leftBlinkerLit.load() ? 1.0 : 0.0, kGlowRiseSeconds, kGlowFallSeconds);
    const bool rightSettled = step(g_glowRight, g_rightBlinkerLit.load() ? 1.0 : 0.0, kGlowRiseSeconds, kGlowFallSeconds);
    return whiteSettled && blueSettled && leftSettled && rightSettled;
}

// Combines the headlight glow (all around the frame) with the blinker glow (its side) per column.
// Where the blinker shines it takes over from the headlight colour, so the amber flash stays clear.
void PrepareGlowColumns(Surface& s) {
    s.glowActive = g_lightWhite > 0.0 || g_lightBlue > 0.0 || g_glowLeft > 0.0 || g_glowRight > 0.0;
    if (!s.glowActive) return;
    const double headlight = g_lightWhite + g_lightBlue;
    for (int wx = 0; wx < s.windowWidth; ++wx) {
        const double blink = GlowLevelAt(s, wx);
        const double base = (1.0 - blink);
        const double level = blink + base * headlight;
        GlowColumn& c = s.columns[wx];
        c.level = level;
        if (level <= 0.0) { c = GlowColumn{}; continue; }
        // Light-weighted average colour of the parts that shine here.
        const double wAmber = blink / level, wWhite = base * g_lightWhite / level, wBlue = base * g_lightBlue / level;
        c.r = wAmber * kGlowHalo.r + wWhite * kLightWhiteHalo.r + wBlue * kLightBlueHalo.r;
        c.g = wAmber * kGlowHalo.g + wWhite * kLightWhiteHalo.g + wBlue * kLightBlueHalo.g;
        c.b = wAmber * kGlowHalo.b + wWhite * kLightWhiteHalo.b + wBlue * kLightBlueHalo.b;
        c.rimR = wAmber * kGlowRim.r + wWhite * kLightWhiteRim.r + wBlue * kLightBlueRim.r;
        c.rimG = wAmber * kGlowRim.g + wWhite * kLightWhiteRim.g + wBlue * kLightBlueRim.g;
        c.rimB = wAmber * kGlowRim.b + wWhite * kLightWhiteRim.b + wBlue * kLightBlueRim.b;
    }
}


// ---------------------------------------------------------------------------
// Text and time helpers
// ---------------------------------------------------------------------------
void DrawTextIn(HDC dc, HFONT font, COLORREF color, const wchar_t* text, RECT rect, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

// Column headings: small capitals with a little letter spacing.
void DrawHeading(HDC dc, HFONT font, const wchar_t* text, RECT rect, UINT format) {
    const int previous = SetTextCharacterExtra(dc, 1);
    DrawTextIn(dc, font, Ref(g_pal.textLabel), text, rect, format | DT_END_ELLIPSIS);
    SetTextCharacterExtra(dc, previous);
}

POINT PointOnCircle(int cx, int cy, double radius, double angle) {
    return POINT{static_cast<LONG>(std::lround(cx + std::cos(angle) * radius)),
        static_cast<LONG>(std::lround(cy + std::sin(angle) * radius))};
}

std::wstring ClockTimeText(std::time_t time) {
    std::tm local{};
    localtime_s(&local, &time);
    wchar_t text[16]{};
    std::wcsftime(text, _countof(text), L"%H:%M", &local);
    return text;
}

std::wstring DurationText(double seconds) {
    const long long minutes = std::max<long long>(0, static_cast<long long>(seconds / 60.0));
    wchar_t text[24]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%02lld:%02lld", minutes / 60, minutes % 60);
    return text;
}

// ---------------------------------------------------------------------------
// Panel parts
// ---------------------------------------------------------------------------
void DrawSoftShadow(double left, double top, double right, double bottom, double radius) {
    FillShape(left - 4, top - 2, right + 4, bottom + 6,
        [=](double px, double py) { return SdRoundRect(px, py, left, top + 1.5, right, bottom + 1.5, radius); },
        Solid({0, 0, 0}), g_pal.shadowAlpha, 3.0);
}

// Light theme gauge faces and the violet frames around them (instead of brushed metal).
constexpr Rgb kLightFaceTop{250, 250, 253};
constexpr Rgb kLightFaceBottom{234, 233, 243};
Rgb VioletFrame(double t) { return Mix(Rgb{136, 108, 255}, Rgb{84, 54, 214}, t); }

// Bezel with an inset face; the caller draws on the face. Dark theme: brushed metal and the given
// dark face colours. Light theme: a violet frame and a light face.
void DrawBezel(const Texture& metal, int x, int y, int width, int height, double radius, double inset,
    Rgb faceTop, Rgb faceBottom) {
    DrawSoftShadow(x, y, x + width, y + height, radius);
    if (g_pal.light) { faceTop = kLightFaceTop; faceBottom = kLightFaceBottom; }
    FillShape(x, y, x + width, y + height,
        [=](double px, double py) { return SdRoundRect(px, py, x, y, x + width, y + height, radius); },
        [&, x, y, height](double px, double py) {
            if (g_pal.light) return VioletFrame((py - y) / height);
            // Lit from above: the top edge of the metal is brighter than the bottom.
            const double shade = 1.12 - 0.38 * (py - y) / height;
            const Rgb base = metal.Valid() ? metal.At(static_cast<int>(px) - x, static_cast<int>(py) - y) : Rgb{150, 156, 162};
            return Scale(base, shade);
        });
    const double l = x + inset, t = y + inset, r = x + width - inset, b = y + height - inset;
    FillShape(l, t, r, b,
        [=](double px, double py) { return SdRoundRect(px, py, l, t, r, b, radius - inset); },
        [=](double, double py) { return Mix(faceTop, faceBottom, (py - t) / (b - t)); });
    // Thin dark inner rim so the face looks recessed.
    FillShape(l, t, r, b,
        [=](double px, double py) {
            const double d = SdRoundRect(px, py, l, t, r, b, radius - inset);
            return std::max(d, -(d + 1.0));
        }, Solid({0, 0, 0}), g_pal.light ? 0.18 : 0.6);
}

void DrawSpeedLimitSign(HDC dc, int x, int y, int size) {
    const double cx = x + size / 2.0;
    const double cy = y + size / 2.0;
    const double r = size / 2.0;
    FillShape(cx - r - 5, cy - r - 3, cx + r + 5, cy + r + 7,
        [=](double px, double py) { return SdCircle(px, py, cx, cy + 1.5, r); }, Solid({0, 0, 0}), std::min(1.0, g_pal.shadowAlpha * 1.1), 3.0);
    FillShape(cx - r, cy - r, cx + r, cy + r,
        [=](double px, double py) { return SdCircle(px, py, cx, cy, r); },
        [=](double, double py) { return Mix(Rgb{240, 62, 62}, Rgb{196, 28, 36}, (py - (cy - r)) / size); });
    const double inner = r - 5.0;
    FillShape(cx - inner, cy - inner, cx + inner, cy + inner,
        [=](double px, double py) { return SdCircle(px, py, cx, cy, inner); },
        [=](double, double py) { return Mix(Rgb{252, 252, 250}, Rgb{226, 230, 232}, (py - (cy - inner)) / (2 * inner)); });

    const long limitKmh = g_hasSpeedLimit.load() ?
        std::lround(static_cast<double>(g_speedLimitMps.load()) * 3.6) : 0;
    wchar_t limitText[16] = L"--";
    if (limitKmh > 0) _snwprintf_s(limitText, _countof(limitText), _TRUNCATE, L"%ld", limitKmh);
    const int icx = static_cast<int>(cx), icy = static_cast<int>(cy);
    DrawTextIn(dc, limitKmh >= 100 ? g_fonts.limitWide : g_fonts.limit, RGB(22, 26, 30), limitText,
        RECT{icx - 20, icy - 13, icx + 20, icy + 14}, DT_CENTER);
}

// Analog fuel gauge, part 1: bezel, dial, coloured level arc, ticks and labels.
void DrawFuelGaugeDial(HDC dc, int x, int y, double level, bool available) {
    DrawBezel(g_metalGauge, x, y, kFuelFrameWidth, kFuelFrameHeight, 12.0, 3.0, Rgb{36, 41, 46}, Rgb{13, 15, 18});

    const double cx = x + kFuelFrameWidth / 2.0;
    const double cy = y + kFuelGaugeCenterY;
    const double radius = kFuelGaugeRadius;
    const double box[4] = {cx - radius - 4, cy - radius - 4, cx + radius + 4, cy + 4};

    // Track (whole scale) with the reserve zone (lowest eighth) tinted red.
    FillShape(box[0], box[1], box[2], box[3],
        [=](double px, double py) { return SdArc(px, py, cx, cy, radius, 2.2, kPi, 2.0 * kPi); },
        Solid(g_pal.track), g_pal.trackAlpha * 1.1);
    FillShape(box[0], box[1], box[2], box[3],
        [=](double px, double py) { return SdArc(px, py, cx, cy, radius, 2.2, kPi, kPi * 1.125); },
        Solid(kWarning), 0.45);

    // Current level as a coloured arc: red in reserve, amber under a quarter, accent otherwise.
    if (available && level > 0.0) {
        const Rgb color = level < 0.125 ? kWarning : (level < 0.25 ? kCaution : g_pal.accent);
        const double end = kPi + kPi * std::clamp(level, 0.0, 1.0);
        FillShape(box[0] - 3, box[1] - 3, box[2] + 3, box[3] + 3,
            [=](double px, double py) { return SdArc(px, py, cx, cy, radius, 2.2, kPi, end); },
            Solid(color), 0.30, 3.0);  // glow
        FillShape(box[0], box[1], box[2], box[3],
            [=](double px, double py) { return SdArc(px, py, cx, cy, radius, 2.2, kPi, end); },
            Solid(color));
    }

    // Ticks: long at E, 1/2 and F; short at 1/4 and 3/4.
    for (int i = 0; i <= 4; ++i) {
        const bool major = (i % 2) == 0;
        const double angle = kPi + kPi * i / 4.0;
        const double inner = radius - (major ? 11.0 : 8.0);
        const double outer = radius - 4.5;
        const double ax = cx + std::cos(angle) * inner, ay = cy + std::sin(angle) * inner;
        const double bx = cx + std::cos(angle) * outer, by = cy + std::sin(angle) * outer;
        FillShape(std::min(ax, bx) - 2, std::min(ay, by) - 2, std::max(ax, bx) + 2, std::max(ay, by) + 2,
            [=](double px, double py) { return SdSegment(px, py, ax, ay, bx, by, major ? 0.9 : 0.6); },
            Solid(g_pal.tick), 0.9);
    }

    const int icx = static_cast<int>(std::lround(cx)), icy = static_cast<int>(std::lround(cy));
    const int ir = static_cast<int>(radius);
    DrawTextIn(dc, g_fonts.gauge, Ref(kWarning), L"E", RECT{icx - ir - 8, icy + 1, icx - ir + 8, icy + 13}, DT_CENTER);
    DrawTextIn(dc, g_fonts.gauge, Ref(g_pal.textValue), L"F", RECT{icx + ir - 8, icy + 1, icx + ir + 8, icy + 13}, DT_CENTER);
    DrawTextIn(dc, g_fonts.gaugeHalf, Ref(g_pal.textLabel), L"1/2",
        RECT{icx - 12, icy - ir - 15, icx + 12, icy - ir - 3}, DT_CENTER);
}

// Analog fuel gauge, part 2: needle and metal pivot (drawn after the text so it stays on top).
void DrawFuelGaugeNeedle(int x, int y, double level, bool available) {
    if (!available) return;
    const double cx = x + kFuelFrameWidth / 2.0;
    const double cy = y + kFuelGaugeCenterY;
    const double angle = kPi + kPi * std::clamp(level, 0.0, 1.0);
    const double tipX = cx + std::cos(angle) * (kFuelGaugeRadius - 3.0);
    const double tipY = cy + std::sin(angle) * (kFuelGaugeRadius - 3.0);
    const double tailX = cx - std::cos(angle) * 5.0;
    const double tailY = cy - std::sin(angle) * 5.0;
    const double l = std::min(tipX, tailX) - 6, t = std::min(tipY, tailY) - 6;
    const double r = std::max(tipX, tailX) + 6, b = std::max(tipY, tailY) + 6;
    FillShape(l, t, r, b, [=](double px, double py) { return SdSegment(px, py, tailX, tailY, tipX, tipY, 2.6); },
        Solid(kNeedle), 0.30, 3.0);
    FillShape(l, t, r, b, [=](double px, double py) { return SdSegment(px, py, tailX, tailY, tipX, tipY, 1.3); },
        Solid(kNeedle));
    FillShape(cx - 6, cy - 6, cx + 6, cy + 6, [=](double px, double py) { return SdCircle(px, py, cx, cy, 4.8); },
        [=](double, double py) { return Mix(Rgb{214, 220, 226}, Rgb{98, 106, 114}, (py - cy + 4.8) / 9.6); });
    FillShape(cx - 3, cy - 3, cx + 3, cy + 3, [=](double px, double py) { return SdCircle(px, py, cx, cy, 1.8); },
        Solid({28, 32, 36}));
}

// Fatigue figure: head, torso and four limbs. The coloured "energy" fill drains like a level
// falling from the top of the head downwards: first the head empties, then the torso and arms,
// and finally the legs down to the feet.
void DrawFatigueFigure(int x, int y, double rested, bool available) {
    rested = std::clamp(rested, 0.0, 1.0);
    const double ox = x, oy = y;
    const double body[4][2] = {{ox + 5.5, oy + 10}, {ox + 16.5, oy + 10}, {ox + 15.5, oy + 23}, {ox + 6.5, oy + 23}};
    // Limbs run from the torso to the hand / foot: two arms, then two legs.
    const double limbs[4][4] = {
        {6, 11, 2, 23}, {16, 11, 20, 23},
        {8.5, 23, 5.5, 39.5}, {13.5, 23, 16.5, 39.5},
    };
    auto figure = [=](double px, double py) {
        double d = SdCircle(px, py, ox + 11.0, oy + 4.5, 4.5);
        d = std::min(d, SdConvex(px, py, body));
        for (const auto& limb : limbs)
            d = std::min(d, SdSegment(px, py, ox + limb[0], oy + limb[1], ox + limb[2], oy + limb[3], 1.7));
        return d;
    };
    const double l = ox - 4, t = oy - 3, r = ox + kFatigueFigureWidth + 4, b = oy + kFatigueFigureHeight + 3;

    // Outline and the empty (hollow) figure, always visible.
    FillShape(l, t, r, b, [=](double px, double py) { return figure(px, py) - 1.4; }, Solid(g_pal.figureOutline));
    FillShape(l, t, r, b, figure, Solid(g_pal.figureHollow));
    if (!available || rested <= 0.0) return;

    // Remaining energy, coloured by the time left until the next rest.
    const int minutes = g_restStopMinutes.load();
    const Rgb color = minutes > 2 * kRestWarningMinutes ? kGood : (minutes > kRestWarningMinutes ? kCaution : kWarning);
    constexpr double kFillTop = -1.0;
    const double kFillBottom = kFatigueFigureHeight + 1.0;
    const double level = oy + kFillBottom - (kFillBottom - kFillTop) * rested;
    FillShape(l, t, r, b,
        [=](double px, double py) { return std::max(figure(px, py), level - py); },
        [=](double, double py) { return Mix(Scale(color, 1.15), Scale(color, 0.8), (py - oy) / kFatigueFigureHeight); });
}

// Draws centred text with the largest of the given fonts that fits the rectangle's width
// (translations differ in length, e.g. "PIHENJ!" vs "ODPOCZNIJ!").
void DrawFittedText(HDC dc, std::initializer_list<HFONT> fonts, COLORREF color, const wchar_t* text, const RECT& rect) {
    HFONT chosen = *(fonts.end() - 1);
    for (HFONT font : fonts) {
        HGDIOBJ old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
        SelectObject(dc, old);
        if (size.cx <= rect.right - rect.left) { chosen = font; break; }
    }
    DrawTextIn(dc, chosen, color, text, rect, DT_CENTER | DT_END_ELLIPSIS);
}

// Time left until the next mandatory rest, in in-game hours:minutes (as the game shows it).
void DrawRestCountdown(HDC dc, const RECT& rect) {
    if (!g_hasRestStop.load()) {
        DrawTextIn(dc, g_fonts.restTime, Ref(g_pal.textLabel), L"--:--", rect, DT_CENTER);
        return;
    }
    const int minutes = g_restStopMinutes.load();
    if (minutes <= 0) {
        DrawFittedText(dc, {g_fonts.restTime, g_fonts.footer, g_fonts.smallLabel}, Ref(kWarning), g_txt->restNow, rect);
        return;
    }
    wchar_t text[16]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%d:%02d", minutes / 60, minutes % 60);
    const Rgb color = minutes <= kRestWarningMinutes ? kWarning : g_pal.textValue;
    DrawTextIn(dc, g_fonts.restTime, Ref(color), text, rect, DT_CENTER);
}

// Local time on a dark glass face in a brushed-metal bezel. Blue by day, green
// from in-game 17:30 until 06:00.
void DrawClock(HDC dc, int x, int y) {
    if (g_pal.light) {
        // Light theme: a black pill (like the selected tab of the reference design) with a soft shadow.
        const double pill = kClockHeight / 2.0;
        DrawSoftShadow(x, y, x + kClockWidth, y + kClockHeight, pill);
        FillShape(x, y, x + kClockWidth, y + kClockHeight,
            [=](double px, double py) { return SdRoundRect(px, py, x, y, x + kClockWidth, y + kClockHeight, pill); },
            [=](double, double py) { return Mix(Rgb{34, 34, 40}, Rgb{12, 12, 16}, (py - y) / kClockHeight); });
    } else {
        DrawBezel(g_metalClock, x, y, kClockWidth, kClockHeight, 10.0, 2.5, Rgb{20, 24, 29}, Rgb{8, 10, 12});
        const double l = x + 2.5, t = y + 2.5, r = x + kClockWidth - 2.5, b = y + kClockHeight - 2.5;
        // Glass reflection on the upper half of the face.
        FillShape(l, t, r, b,
            [=](double px, double py) { return std::max(SdRoundRect(px, py, l, t, r, b, 7.5), py - (t + 15.0)); },
            Solid({255, 255, 255}), 0.05);
    }

    const scs_u32_t gameMinuteOfDay = g_gameTimeMinutes.load() % 1440;
    const bool evening = g_hasGameTime.load() && (gameMinuteOfDay >= 17 * 60 + 30 || gameMinuteOfDay < 6 * 60);
    const Rgb digits = evening ? Rgb{108, 236, 148} : Rgb{120, 206, 255};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[8]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%02u:%02u", now.wHour, now.wMinute);
    DrawTextIn(dc, g_fonts.clock, Ref(digits), text, RECT{x, y + 1, x + kClockWidth, y + kClockHeight}, DT_CENTER);
}

// ---------------------------------------------------------------------------
// G-meter
// ---------------------------------------------------------------------------
Rgb GComfortColor(double g) {
    return g < kGComfortGood ? kGood : (g < kGComfortCaution ? kCaution : kWarning);
}

// Reads the acceleration, smooths it and keeps the trail and the held peak. Called about 20 times a second.
void UpdateGMeter() {
    const long long now = SteadyMillis();
    const double dt = g_gmeter.lastMs ? std::clamp((now - g_gmeter.lastMs) / 1000.0, 0.0, 0.5) : 0.0;
    g_gmeter.lastMs = now;
    if (!g_hasAccel.load()) {
        g_gmeter = GMeterState{};
        g_gmeter.lastMs = now;
        return;
    }
    // Felt force is opposite to the acceleration: X points right, Z backwards in vehicle space,
    // so braking (acceleration towards +Z) pushes the passengers forward.
    const double right = -g_accelX.load() / kGravity;
    const double forward = g_accelZ.load() / kGravity;
    const double k = dt > 0.0 ? 1.0 - std::exp(-dt / kGSmoothingSeconds) : 1.0;
    g_gmeter.right += (right - g_gmeter.right) * k;
    g_gmeter.forward += (forward - g_gmeter.forward) * k;

    g_gmeter.trailHead = (g_gmeter.trailHead + 1) % kGTrailLength;
    g_gmeter.trail[g_gmeter.trailHead] = {g_gmeter.right, g_gmeter.forward};
    g_gmeter.trailCount = std::min(g_gmeter.trailCount + 1, kGTrailLength);

    // Peak: held for a few seconds, then it sinks slowly towards the current value.
    const double magnitude = std::hypot(g_gmeter.right, g_gmeter.forward);
    if (magnitude >= g_gmeter.peak) {
        g_gmeter.peak = magnitude;
        g_gmeter.peakMs = now;
    } else if (now - g_gmeter.peakMs > static_cast<long long>(kGPeakHoldSeconds * 1000)) {
        g_gmeter.peak = std::max(magnitude, g_gmeter.peak - dt * 0.1);
    }
}

// Dial position of a felt force (right / forward in g), clamped to the dial.
void GDialPoint(double right, double forward, double& x, double& y) {
    const double scale = (kGFaceRadius - 4.0) / kGFullScale;
    double dx = right * scale, dy = -forward * scale;  // forward = up on the dial
    const double length = std::hypot(dx, dy);
    const double limit = kGFaceRadius - 4.0;
    if (length > limit) { dx *= limit / length; dy *= limit / length; }
    x = kGCenterX + dx;
    y = kGCenterY + dy;
}

// Static part of the G-meter (drawn with the rest of the panel): bezel, face, rings, cross and labels.
void DrawGMeterDial(HDC dc) {
    GdiFlush();  // finish earlier GDI text before writing pixels directly
    const double cx = kGCenterX, cy = kGCenterY, R = kGBezelRadius, F = kGFaceRadius;
    FillShape(cx - R - 5, cy - R - 3, cx + R + 5, cy + R + 7,
        [=](double px, double py) { return SdCircle(px, py, cx, cy + 1.5, R); }, Solid({0, 0, 0}), g_pal.shadowAlpha, 3.0);
    FillShape(cx - R, cy - R, cx + R, cy + R,
        [=](double px, double py) { return SdCircle(px, py, cx, cy, R); },
        [=](double px, double py) {
            if (g_pal.light) return VioletFrame((py - (cy - R)) / (2 * R));
            const Rgb base = g_metalGMeter.Valid() ?
                g_metalGMeter.At(static_cast<int>(px - (cx - R)), static_cast<int>(py - (cy - R))) : Rgb{150, 156, 162};
            return Scale(base, 1.12 - 0.38 * (py - (cy - R)) / (2 * R));
        });
    const Rgb faceTop = g_pal.light ? kLightFaceTop : Rgb{34, 39, 45};
    const Rgb faceBottom = g_pal.light ? kLightFaceBottom : Rgb{12, 14, 17};
    FillShape(cx - F, cy - F, cx + F, cy + F,
        [=](double px, double py) { return SdCircle(px, py, cx, cy, F); },
        [=](double, double py) { return Mix(faceTop, faceBottom, (py - (cy - F)) / (2 * F)); });
    FillShape(cx - F, cy - F, cx + F, cy + F,
        [=](double px, double py) { const double d = SdCircle(px, py, cx, cy, F); return std::max(d, -(d + 1.0)); },
        Solid({0, 0, 0}), g_pal.light ? 0.18 : 0.6);
    // Comfort zone (green tint inside the "good" ring), scale rings at half and full scale, and a cross.
    const double scale = (F - 4.0) / kGFullScale;
    FillShape(cx - F, cy - F, cx + F, cy + F,
        [=](double px, double py) { return SdCircle(px, py, cx, cy, kGComfortGood * scale); }, Solid(kGood), 0.10);
    for (double g : {kGFullScale / 2.0, kGFullScale}) {
        const double r = g * scale;
        FillShape(cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2,
            [=](double px, double py) { return std::abs(SdCircle(px, py, cx, cy, r)) - 0.5; },
            Solid(g_pal.tick), g == kGFullScale ? 0.35 : 0.22);
    }
    FillShape(cx - F, cy - 1, cx + F, cy + 1,
        [=](double px, double py) { return SdSegment(px, py, cx - F + 3, cy, cx + F - 3, cy, 0.5); }, Solid(g_pal.tick), 0.22);
    FillShape(cx - 1, cy - F, cx + 1, cy + F,
        [=](double px, double py) { return SdSegment(px, py, cx, cy - F + 3, cx, cy + F - 3, 0.5); }, Solid(g_pal.tick), 0.22);

    DrawHeading(dc, g_fonts.label, g_txt->gForce, RECT{kDividerClock + 2, 6, kPanelWidth - 6, 24}, DT_CENTER);
    DrawHeading(dc, g_fonts.smallLabel, g_txt->peak, RECT{kGTextLeft, 62, kPanelWidth - 8, 76}, DT_LEFT);
}

// Moving part of the G-meter (redrawn about 20 times a second on top of the cached panel):
// the fading trail, the dot and the current / peak values.
void DrawGMeterDynamic(HDC dc) {
    const bool available = g_hasAccel.load();
    if (available) {
        // Trail, oldest first, fading out.
        for (int i = g_gmeter.trailCount - 1; i >= 1; --i) {
            const GSample& s = g_gmeter.trail[(g_gmeter.trailHead - i + kGTrailLength) % kGTrailLength];
            double x = 0.0, y = 0.0;
            GDialPoint(s.right, s.forward, x, y);
            const double age = static_cast<double>(i) / kGTrailLength;
            FillShape(x - 3, y - 3, x + 3, y + 3, [=](double px, double py) { return SdCircle(px, py, x, y, 1.6); },
                Solid(g_pal.accent), 0.45 * (1.0 - age));
        }
        double x = 0.0, y = 0.0;
        GDialPoint(g_gmeter.right, g_gmeter.forward, x, y);
        const Rgb color = GComfortColor(std::hypot(g_gmeter.right, g_gmeter.forward));
        FillShape(x - 9, y - 9, x + 9, y + 9, [=](double px, double py) { return SdCircle(px, py, x, y, 4.5); },
            Solid(color), 0.35, 4.0);  // glow
        FillShape(x - 6, y - 6, x + 6, y + 6, [=](double px, double py) { return SdCircle(px, py, x, y, 4.2); },
            [=](double, double py) { return Mix(Scale(color, 1.2), Scale(color, 0.8), (py - (y - 4.2)) / 8.4); });
    }

    wchar_t current[16] = L"--";
    wchar_t peak[16] = L"--";
    Rgb currentColor = g_pal.textLabel;
    if (available) {
        const double magnitude = std::hypot(g_gmeter.right, g_gmeter.forward);
        _snwprintf_s(current, _countof(current), _TRUNCATE, L"%.2f", magnitude);
        _snwprintf_s(peak, _countof(peak), _TRUNCATE, L"%.2f", g_gmeter.peak);
        currentColor = GComfortColor(magnitude);
    }
    DrawTextIn(dc, g_fonts.restTime, Ref(currentColor), current, RECT{kGTextLeft, 34, kPanelWidth - 6, 58}, DT_LEFT);
    DrawTextIn(dc, g_fonts.footer, Ref(available ? GComfortColor(g_gmeter.peak) : g_pal.textLabel), peak,
        RECT{kGTextLeft, 75, kPanelWidth - 6, 93}, DT_LEFT);
    GdiFlush();
}

// Etched divider line (dark line next to a light one), fading out towards both ends.
void DrawDividerAt(int x) {
    for (int y = kDividerTop; y < kDividerBottom; ++y) {
        const double fade = std::sin(kPi * (y - kDividerTop + 0.5) / (kDividerBottom - kDividerTop));
        BlendPixel(x, y, {0, 0, 0}, g_pal.dividerDark * fade);
        BlendPixel(x + 1, y, {255, 255, 255}, g_pal.dividerLight * fade);
    }
}

void DrawDividers() {
    for (int x : {kDividerSpeed, kDividerRoute, kDividerFuel, kDividerFatigue, kDividerClock}) DrawDividerAt(x);
}

void DrawProgressBar(double left, double right, double cy, double fraction) {
    FillShape(left - 3, cy - 4, right + 3, cy + 4,
        [=](double px, double py) { return SdSegment(px, py, left, cy, right, cy, 2.0); },
        Solid(g_pal.track), g_pal.trackAlpha);
    if (fraction <= 0.0) return;
    const double end = left + (right - left) * std::clamp(fraction, 0.0, 1.0);
    FillShape(left - 3, cy - 4, end + 3, cy + 4,
        [=](double px, double py) { return SdSegment(px, py, left, cy, end, cy, 2.0); },
        [=](double px, double) { return Mix(g_pal.accentDeep, g_pal.accent, (px - left) / (right - left)); });
}

std::wstring IdleFooterText() {
    const int failures = g_hotkeyFailures.load();
    if ((failures & kHotkeyMoveFailed) && (failures & kHotkeyOpacityFailed))
        return g_txt->hotkeysBothBusy;
    if (failures & kHotkeyMoveFailed) return g_txt->hotkeyMoveBusy;
    if (failures & kHotkeyOpacityFailed) return g_txt->hotkeySettingsBusy;
    return g_txt->waitingForRoute;
}

// Departure, estimated arrival, time and distance left and route progress, shared by the panel and the route card.
struct RouteValues {
    std::wstring departure = L"--:--";
    std::wstring arrival = L"--:--";
    std::wstring remaining = L"--:--";
    std::wstring distance = L"-- km";
    double progress = 0.0;
    bool active = false;     // a GPS route is being followed
    bool completed = false;  // the last route has been finished
};

RouteValues ComputeRouteValues() {
    RouteValues v;
    v.active = g_hasDistance.load() && g_hasRoute.load();
    v.completed = g_tripCompleted.load();
    if (g_tripStarted.load() || v.completed)
        v.departure = ClockTimeText(static_cast<std::time_t>(g_routeStartedEpoch.load()));
    wchar_t distanceText[32] = L"-- km";
    if (v.completed) {
        v.arrival = ClockTimeText(static_cast<std::time_t>(g_tripCompletedEpoch.load()));
        v.remaining = L"00:00";
        wcscpy_s(distanceText, L"0.0 km");
        v.progress = 1.0;
    } else if (v.active) {
        const double eta = EstimateRemainingRealSeconds();
        if (eta >= 0.0) {
            v.arrival = ClockTimeText(std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now() + std::chrono::seconds(static_cast<long long>(std::ceil(eta)))));
            v.remaining = DurationText(eta);
        }
        const double distance = g_distanceMeters.load();
        _snwprintf_s(distanceText, _countof(distanceText), _TRUNCATE, L"%.1f km", distance / 1000.0);
        const double initial = g_initialDistance.load();
        if (initial > 0.0) v.progress = std::clamp(1.0 - distance / initial, 0.0, 1.0);
    }
    v.distance = distanceText;
    return v;
}

// Renders every column of the panel into g_canvas (the detached ones are cut out later).
void RenderPanel() {
    TargetScope scope(g_canvas.pixels, kPanelWidth, kPanelHeight);
    HDC dc = g_canvas.dc;
    std::copy(g_background.begin(), g_background.end(), g_canvas.pixels);
    const RouteValues route = ComputeRouteValues();

    const float fuelCapacity = g_fuelCapacityLiters.load();
    const bool fuelAvailable = g_hasFuel.load() && fuelCapacity > 0.0f;
    const double fuelLevel = fuelAvailable ?
        std::clamp(static_cast<double>(g_fuelLiters.load() / fuelCapacity), 0.0, 1.0) : 0.0;

    // Shapes first (written straight into the pixels) ...
    DrawDividers();
    DrawProgressBar(kRouteLeft + 1.5, kRouteRight - 1.5, kProgressY, route.progress);
    DrawSpeedLimitSign(dc, kSpeedSignX, kSpeedSignY, kSpeedSignSize);
    DrawFuelGaugeDial(dc, kFuelFrameX, kFuelFrameY, fuelLevel, fuelAvailable);
    DrawFatigueFigure(kFatigueX, kFatigueY, g_hasRestStop.load() ? RestedFraction() : 0.0, g_hasRestStop.load());
    DrawClock(dc, kClockX, kClockY);
    DrawGMeterDial(dc);

    // ... then the GDI text on top ...
    DrawTextIn(dc, g_fonts.unit, Ref(g_pal.textLabel), g_txt->kmh, RECT{4, 68, kDividerSpeed - 4, 90}, DT_CENTER);

    DrawHeading(dc, g_fonts.label, g_txt->departure, RECT{kRouteLeft, 6, kArrivalLeft - 6, 24}, DT_LEFT);
    DrawHeading(dc, g_fonts.label, g_txt->estimatedArrival, RECT{kArrivalLeft, 6, kRouteRight, 24}, DT_LEFT);
    DrawTextIn(dc, g_fonts.value, Ref(g_pal.textValue), route.departure.c_str(), RECT{kRouteLeft, 22, kArrivalLeft - 6, 62}, DT_LEFT);
    DrawTextIn(dc, g_fonts.value, Ref(g_pal.accent), route.arrival.c_str(), RECT{kArrivalLeft, 22, kRouteRight, 62}, DT_LEFT);
    std::wstring footer = route.active || route.completed ?
        std::wstring(g_txt->timeLeftPrefix) + route.remaining + L"  ·  " + route.distance : IdleFooterText();
    DrawTextIn(dc, g_fonts.footer, Ref(g_pal.textSoft), footer.c_str(), RECT{kRouteLeft, 72, kRouteRight, 96}, DT_LEFT | DT_END_ELLIPSIS);

    DrawHeading(dc, g_fonts.smallLabel, g_txt->fuel, RECT{kDividerRoute + 2, 6, kDividerFuel, 24}, DT_CENTER | DT_END_ELLIPSIS);
    DrawHeading(dc, g_fonts.smallLabel, g_txt->fatigue, RECT{kDividerFuel + 2, 6, kDividerFatigue, 24}, DT_CENTER | DT_END_ELLIPSIS);
    DrawRestCountdown(dc, RECT{kDividerFuel + 2, kRestTextTop, kDividerFatigue - 2, kRestTextBottom});
    DrawHeading(dc, g_fonts.label, g_txt->localTime, RECT{kDividerFatigue + 2, 6, kDividerClock - 6, 24}, DT_CENTER);
    GdiFlush();

    // ... and finally the parts that must sit above the text.
    DrawFuelGaugeNeedle(kFuelFrameX, kFuelFrameY, fuelLevel, fuelAvailable);

    // Keep this panel so animation and G-meter frames only redo what moves.
    g_panelBase.assign(g_canvas.pixels, g_canvas.pixels + static_cast<size_t>(kPanelWidth) * kPanelHeight);
}

// Detached route card: where from, where to, and how much time is left.
void RenderRouteCard() {
    TargetScope scope(g_routeCanvas.pixels, kRouteCardWidth, kPanelHeight);
    HDC dc = g_routeCanvas.dc;
    std::copy(g_routeBackground.begin(), g_routeBackground.end(), g_routeCanvas.pixels);
    const RouteValues route = ComputeRouteValues();
    const JobInfo job = ReadJob();
    constexpr int left = 16, middle = 160, right = kRouteCardDivider - 12;

    DrawDividerAt(kRouteCardDivider);
    DrawProgressBar(left + 1.5, right - 1.5, kProgressY, route.progress);

    DrawHeading(dc, g_fonts.label, g_txt->from, RECT{left, 6, middle - 8, 24}, DT_LEFT);
    DrawHeading(dc, g_fonts.label, g_txt->to, RECT{middle, 6, right, 24}, DT_LEFT);
    if (job.active) {
        const std::wstring from = job.sourceCity.empty() ? L"—" : job.sourceCity;
        const std::wstring to = job.destinationCity.empty() ? L"—" : job.destinationCity;
        DrawTextIn(dc, g_fonts.routeCity, Ref(g_pal.textValue), from.c_str(), RECT{left, 22, middle - 18, 48}, DT_LEFT | DT_END_ELLIPSIS);
        DrawTextIn(dc, g_fonts.routeCity, Ref(g_pal.textLabel), L"›", RECT{middle - 18, 22, middle - 2, 48}, DT_CENTER);
        DrawTextIn(dc, g_fonts.routeCity, Ref(g_pal.accent), to.c_str(), RECT{middle, 22, right, 48}, DT_LEFT | DT_END_ELLIPSIS);
        DrawTextIn(dc, g_fonts.routeCompany, Ref(g_pal.textSoft), job.sourceCompany.c_str(), RECT{left, 47, middle - 18, 63}, DT_LEFT | DT_END_ELLIPSIS);
        DrawTextIn(dc, g_fonts.routeCompany, Ref(g_pal.textSoft), job.destinationCompany.c_str(), RECT{middle, 47, right, 63}, DT_LEFT | DT_END_ELLIPSIS);
    } else {
        DrawTextIn(dc, g_fonts.routeCity, Ref(g_pal.textLabel), L"—", RECT{left, 22, middle - 18, 48}, DT_LEFT);
        DrawTextIn(dc, g_fonts.routeCity, Ref(g_pal.textLabel), L"—", RECT{middle, 22, right, 48}, DT_LEFT);
        DrawTextIn(dc, g_fonts.routeCompany, Ref(g_pal.textSoft), g_txt->noJob, RECT{left, 47, right, 63}, DT_LEFT | DT_END_ELLIPSIS);
    }
    std::wstring footer = route.active || route.completed ?
        route.distance + L"  ·  " + g_txt->arrivalPrefix + route.arrival : IdleFooterText();
    DrawTextIn(dc, g_fonts.footer, Ref(g_pal.textSoft), footer.c_str(), RECT{left, 72, right, 96}, DT_LEFT | DT_END_ELLIPSIS);

    // Time left, large, on the right.
    DrawHeading(dc, g_fonts.smallLabel, g_txt->timeLeft, RECT{kRouteCardDivider + 4, 6, kRouteCardWidth - 6, 24}, DT_CENTER);
    DrawTextIn(dc, g_fonts.routeTime, Ref(route.active || route.completed ? g_pal.textValue : g_pal.textLabel), route.remaining.c_str(),
        RECT{kRouteCardDivider + 2, 26, kRouteCardWidth - 4, 64}, DT_CENTER);
    DrawHeading(dc, g_fonts.smallLabel, g_txt->hoursMinutes, RECT{kRouteCardDivider + 4, 66, kRouteCardWidth - 6, 82}, DT_CENTER);
    GdiFlush();
    g_routeBase.assign(g_routeCanvas.pixels, g_routeCanvas.pixels + static_cast<size_t>(kRouteCardWidth) * kPanelHeight);
}

// Copies the columns [x0, x1) of the full panel into a surface, starting at column `to`.
void CopyColumns(Surface& s, int x0, int x1, int to) {
    for (int y = 0; y < kPanelHeight; ++y)
        std::copy(g_canvas.pixels + static_cast<size_t>(y) * kPanelWidth + x0,
            g_canvas.pixels + static_cast<size_t>(y) * kPanelWidth + x1,
            s.panel.begin() + static_cast<size_t>(y) * s.width + to);
}

// Main panel = the full panel without the detached columns (route: 72..368, G-meter: 672..788).
void AssembleMainPanel() {
    const int right = g_gmeterSeparate ? kDividerClock : kPanelWidth;
    if (g_routeSeparate) {
        CopyColumns(g_mainSurface, 0, kDividerSpeed, 0);
        CopyColumns(g_mainSurface, kDividerRoute, right, kDividerSpeed);
    } else {
        CopyColumns(g_mainSurface, 0, right, 0);
    }
}

// Rim, edge glow and the rounded alpha mask of a surface (premultiplied, as UpdateLayeredWindow expects).
void FinishSurface(Surface& s) {
    TargetScope scope(s.panel.data(), s.width, s.height);
    // Rim: thin and light normally, accent-coloured while the panels can be moved (Ctrl+F11).
    const double rimWidth = g_dragMode ? 2.0 : 1.0;
    const double w = s.width, h = s.height;
    FillShape(0, 0, w, h,
        [=](double px, double py) {
            const double d = SdRoundRect(px, py, 0, 0, w, h, kPanelRadius);
            return std::max(d, -(d + rimWidth));
        }, Solid(g_dragMode ? g_pal.accent : g_pal.rim), g_dragMode ? 0.95 : g_pal.rimAlpha);

    // Edge glow on the rim (headlights all around, blinker amber on its side), as if lit from behind.
    PrepareGlowColumns(s);
    if (s.glowActive) {
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x) {
                const GlowColumn& c = s.columns[x + kGlowMargin];
                const double light = s.inner[static_cast<size_t>(y) * s.width + x] * c.level;
                if (light > 0.002)
                    BlendPixel(x, y, Rgb{static_cast<int>(c.rimR), static_cast<int>(c.rimG), static_cast<int>(c.rimB)},
                        std::min(1.0, light * 0.9));
            }
    }

    for (size_t i = 0; i < s.mask.size(); ++i) {
        const uint32_t a = s.mask[i];
        const uint32_t p = s.panel[i];
        const uint32_t r = ((p >> 16) & 255) * a / 255;
        const uint32_t g = ((p >> 8) & 255) * a / 255;
        const uint32_t b = (p & 255) * a / 255;
        s.panel[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

// Builds the window image of a surface: the glow in the margin (only while a light is on or
// fading) with the finished, premultiplied panel composited on top of it.
void ComposeSurface(Surface& s) {
    if (!s.frame.pixels) return;
    for (int wy = 0; wy < s.windowHeight; ++wy) {
        uint32_t* row = s.frame.pixels + static_cast<size_t>(wy) * s.windowWidth;
        const int py = wy - kGlowMargin;
        for (int wx = 0; wx < s.windowWidth; ++wx) {
            uint32_t a = 0, r = 0, g = 0, b = 0;
            // Light theme: drop shadow (black, premultiplied) at the bottom of the stack.
            if (g_pal.light) a = static_cast<uint32_t>(std::lround(s.shadow[static_cast<size_t>(wy) * s.windowWidth + wx] * 255.0));
            if (s.glowActive) {
                const GlowColumn& c = s.columns[wx];
                const double light = s.halo[static_cast<size_t>(wy) * s.windowWidth + wx] * c.level;
                if (light > 0.002) {
                    // Glow over the shadow (both premultiplied).
                    const double alpha = std::min(1.0, light) * 0.92;
                    const uint32_t ga = static_cast<uint32_t>(std::lround(alpha * 255.0));
                    const uint32_t keep = 255 - ga;
                    a = ga + a * keep / 255;
                    r = static_cast<uint32_t>(std::lround(c.r * alpha)) + r * keep / 255;
                    g = static_cast<uint32_t>(std::lround(c.g * alpha)) + g * keep / 255;
                    b = static_cast<uint32_t>(std::lround(c.b * alpha)) + b * keep / 255;
                }
            }
            const int px = wx - kGlowMargin;
            if (px >= 0 && py >= 0 && px < s.width && py < s.height) {
                const uint32_t p = s.panel[static_cast<size_t>(py) * s.width + px];
                const uint32_t pa = p >> 24;
                const uint32_t keep = 255 - pa;
                a = pa + a * keep / 255;
                r = ((p >> 16) & 255) + r * keep / 255;
                g = ((p >> 8) & 255) + g * keep / 255;
                b = (p & 255) + b * keep / 255;
            }
            row[wx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

void ShowSurface(Surface& s, HWND window) {
    if (!window || !s.frame.dc) return;
    FinishSurface(s);
    ComposeSurface(s);
    POINT source{0, 0};
    SIZE size{s.windowWidth, s.windowHeight};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>((100 - g_transparency) * 255 / 100), AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, nullptr, &size, s.frame.dc, &source, 0, &blend, ULW_ALPHA);
}

// Renders and shows the main panel and the detached cards with the user's transparency.
// rebuild = false reuses the cached panel (animation / G-meter frames); gMeterOnly updates only the
// detached G-meter card (when the G-meter lives in its own window, its updates need nothing else).
void PresentAll(bool rebuild, bool gMeterOnly) {
    if (!EnsureCanvas() || g_mainSurface.mask.empty()) return;
    if (rebuild || g_panelBase.empty()) RenderPanel();
    else std::copy(g_panelBase.begin(), g_panelBase.end(), g_canvas.pixels);
    {
        TargetScope scope(g_canvas.pixels, kPanelWidth, kPanelHeight);
        DrawGMeterDynamic(g_canvas.dc);
    }
    if (!gMeterOnly) {
        AssembleMainPanel();
        ShowSurface(g_mainSurface, g_window.load());
    }
    if (g_gmeterSeparate) {
        CopyColumns(g_gSurface, kGCardLeft, kPanelWidth, 0);
        ShowSurface(g_gSurface, g_gWindow);
    }
    if (g_routeSeparate && !gMeterOnly) {
        if (rebuild || g_routeBase.empty()) RenderRouteCard();
        std::copy(g_routeBase.begin(), g_routeBase.end(), g_routeSurface.panel.begin());
        ShowSurface(g_routeSurface, g_routeWindow);
    }
}

// Starts the fast glow animation (about 60 fps) until the glow has settled again.
void StartGlowAnimation(HWND window) {
    if (g_glowTimerRunning || !window) return;
    g_glowLastMs = SteadyMillis();
    g_glowTimerRunning = SetTimer(window, kGlowTimerId, 16, nullptr) != 0;
}

void OnGlowTimer(HWND window) {
    const bool settled = UpdateGlow();
    if (g_overlayVisible || g_dragMode) PresentAll(false);
    if (settled) {
        KillTimer(window, kGlowTimerId);
        g_glowTimerRunning = false;
    }
}

// ---------------------------------------------------------------------------
// Game window tracking
// ---------------------------------------------------------------------------
struct GameWindowSearch {
    HWND window = nullptr;
    long long area = 0;
};

bool IsGameExecutable(const wchar_t* executable) {
    return _wcsicmp(executable, L"eurotrucks2.exe") == 0 || _wcsicmp(executable, L"amtrucks.exe") == 0;
}

BOOL CALLBACK FindGameWindowCallback(HWND hwnd, LPARAM parameter) {
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    // The plugin runs inside the game process, so only its own windows are candidates.
    if (processId != GetCurrentProcessId()) return TRUE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return TRUE;
    wchar_t imagePath[MAX_PATH]{};
    DWORD pathLength = _countof(imagePath);
    const bool gotPath = QueryFullProcessImageNameW(process, 0, imagePath, &pathLength) != FALSE;
    CloseHandle(process);
    if (!gotPath) return TRUE;
    const wchar_t* executable = std::wcsrchr(imagePath, L'\\');
    executable = executable ? executable + 1 : imagePath;
    if (!IsGameExecutable(executable)) return TRUE;
    RECT bounds{};
    if (!GetWindowRect(hwnd, &bounds)) return TRUE;
    const long long area = static_cast<long long>(bounds.right - bounds.left) * (bounds.bottom - bounds.top);
    auto* search = reinterpret_cast<GameWindowSearch*>(parameter);
    if (area > search->area) {
        search->window = hwnd;
        search->area = area;
    }
    return TRUE;
}

HWND FindGameWindow() {
    GameWindowSearch search{};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

// Moves a layered window so its panel's corner is at (x, y), kept inside the game client area,
// and shows or hides it.
void PlaceWindow(HWND window, const Surface& s, int x, int y, bool place, bool show,
    int clientLeft, int clientTop, int clientWidth, int clientHeight) {
    if (!window) return;
    if (place) {
        x = std::clamp(x, clientLeft, std::max(clientLeft, clientLeft + clientWidth - s.width));
        y = std::clamp(y, clientTop, std::max(clientTop, clientTop + clientHeight - s.height));
        const int windowX = x - kGlowMargin;
        const int windowY = y - kGlowMargin;
        RECT current{};
        GetWindowRect(window, &current);
        if (current.left != windowX || current.top != windowY ||
            current.right - current.left != s.windowWidth || current.bottom - current.top != s.windowHeight)
            SetWindowPos(window, HWND_TOPMOST, windowX, windowY, s.windowWidth, s.windowHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if ((IsWindowVisible(window) != FALSE) != show) ShowWindow(window, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void RefreshOverlayState(bool force = false) {
    const long long now = SteadyMillis();
    if (!force && now - g_lastWindowRefreshMs < 250) return;
    g_lastWindowRefreshMs = now;
    g_gameWindow = FindGameWindow();
    HWND window = g_window.load();
    if (!window) return;

    RECT gameClient{};
    POINT clientOrigin{};
    const bool hasClient = g_gameWindow && IsWindowVisible(g_gameWindow) && !IsIconic(g_gameWindow) &&
        GetClientRect(g_gameWindow, &gameClient);
    if (hasClient) {
        clientOrigin = {gameClient.left, gameClient.top};
        ClientToScreen(g_gameWindow, &clientOrigin);
    }
    const int left = static_cast<int>(clientOrigin.x);
    const int top = static_cast<int>(clientOrigin.y);
    const int clientWidth = static_cast<int>(gameClient.right - gameClient.left);
    const int clientHeight = static_cast<int>(gameClient.bottom - gameClient.top);

    const bool show = g_driverActive.load() && !(g_gameWindow && IsIconic(g_gameWindow));
    // Positions are kept inside the game window; the saved positions themselves are not changed.
    const bool place = show && !g_dragMode && hasClient;
    int mainX = g_position.saved ? g_position.x : left + static_cast<int>(clientWidth * 0.3125f);
    int mainY = g_position.saved ? g_position.y : top + static_cast<int>(clientHeight * 0.56f);
    if (place) {
        mainX = std::clamp(mainX, left, std::max(left, left + clientWidth - g_mainSurface.width));
        mainY = std::clamp(mainY, top, std::max(top, top + clientHeight - g_mainSurface.height));
    }
    PlaceWindow(window, g_mainSurface, mainX, mainY, place, show, left, top, clientWidth, clientHeight);
    g_overlayVisible = show;

    // Detached cards: by default above the main panel, the route card at its left end and the G-meter
    // at its right end, or right next to the route card when both are detached (so they never overlap).
    const int cardY = mainY - kPanelHeight - kCardGap;
    const int routeX = g_routePosition.saved ? g_routePosition.x : mainX;
    const int routeY = g_routePosition.saved ? g_routePosition.y : cardY;
    const int gDefaultX = g_routeSeparate && !g_routePosition.saved ? routeX + kRouteCardWidth + kCardGap
        : mainX + g_mainSurface.width - kGCardWidth;
    PlaceWindow(g_routeWindow, g_routeSurface, routeX, routeY, place, show && g_routeSeparate, left, top, clientWidth, clientHeight);
    PlaceWindow(g_gWindow, g_gSurface,
        g_gPosition.saved ? g_gPosition.x : gDefaultX,
        g_gPosition.saved ? g_gPosition.y : cardY, place, show && g_gmeterSeparate, left, top, clientWidth, clientHeight);
}

void SetWindowMovable(bool movable) {
    for (HWND window : {g_window.load(), g_gWindow, g_routeWindow}) {
        if (!window) continue;
        LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (movable) ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        else ex |= WS_EX_TRANSPARENT;
        SetWindowLongPtrW(window, GWL_EXSTYLE, ex);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    PresentAll();  // shows or removes the accent rim of moving mode
    if (!movable) SaveAllPositions();
}

// Applies a changed choice of detached elements: resizes the main panel and shows / hides the cards.
void ApplyLayout() {
    BuildSurface(g_mainSurface, MainPanelWidth(), kPanelHeight);
    RefreshOverlayState(true);
    PresentAll();
}

// Switches the background theme of every panel (0 = dark carbon, 1 = light).
void ApplyTheme(bool light) {
    g_pal = light ? kLightPalette : kDarkPalette;
    g_background = BuildBackground(kPanelWidth);
    g_routeBackground = BuildBackground(kRouteCardWidth);
    g_panelBase.clear();
    g_routeBase.clear();
    PresentAll();
}

// ---------------------------------------------------------------------------
// Settings window (Ctrl+F12): transparency and the detachable elements
// ---------------------------------------------------------------------------
void DrawOpacityMenu(HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(kMenuBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);
    RECT accentBar{0, 0, client.right, 3};
    HBRUSH accent = CreateSolidBrush(Ref(kDarkPalette.accent));
    FillRect(dc, &accentBar, accent);
    DeleteObject(accent);
    RECT separator{16, 172, client.right - 16, 173};
    HBRUSH line = CreateSolidBrush(RGB(52, 60, 68));
    FillRect(dc, &separator, line);
    DeleteObject(line);
    SetBkMode(dc, TRANSPARENT);
    DrawTextIn(dc, g_fonts.menuTitle, Ref(kDarkPalette.textValue), g_txt->transparencyTitle, RECT{16, 12, client.right - 16, 40}, DT_LEFT | DT_END_ELLIPSIS);
    DrawTextIn(dc, g_fonts.menuLabel, Ref(kDarkPalette.textSoft), g_txt->allPanels, RECT{16, 56, 150, 82}, DT_LEFT | DT_END_ELLIPSIS);
    wchar_t amount[16]{};
    _snwprintf_s(amount, _countof(amount), _TRUNCATE, L"%d%%", g_transparency);
    DrawTextIn(dc, g_fonts.menuValue, Ref(kDarkPalette.accent), amount, RECT{client.right - 60, 56, client.right - 14, 82}, DT_RIGHT);
    DrawTextIn(dc, g_fonts.menuLabel, Ref(kDarkPalette.textSoft), g_txt->background, RECT{16, 98, 150, 124}, DT_LEFT | DT_END_ELLIPSIS);
    DrawTextIn(dc, g_fonts.menuLabel, Ref(kDarkPalette.textSoft), g_txt->language, RECT{16, 134, 150, 160}, DT_LEFT | DT_END_ELLIPSIS);
    DrawTextIn(dc, g_fonts.menuTitle, Ref(kDarkPalette.textValue), g_txt->separateTitle, RECT{16, 180, client.right - 16, 206}, DT_LEFT | DT_END_ELLIPSIS);
    DrawTextIn(dc, g_fonts.menuLabel, Ref(kDarkPalette.textLabel), g_txt->moveHint,
        RECT{16, kOpacityMenuHeight - 34, client.right - 16, kOpacityMenuHeight - 10}, DT_LEFT | DT_END_ELLIPSIS);
}

// Fills the theme drop-down in the current language, keeping the current choice selected.
void FillThemeCombo() {
    if (!g_themeCombo) return;
    SendMessageW(g_themeCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_txt->themeDark));
    SendMessageW(g_themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_txt->themeLight));
    SendMessageW(g_themeCombo, CB_SETCURSEL, g_pal.light ? 1 : 0, 0);
}

// Switches the user interface language: panels, cards and this settings window.
void ApplyLanguage(int language) {
    g_language = std::clamp(language, 0, kLanguageCount - 1);
    g_txt = &kTexts[g_language];
    if (g_gCheck) SetWindowTextW(g_gCheck, g_txt->gMeterSeparate);
    if (g_routeCheck) SetWindowTextW(g_routeCheck, g_txt->routeSeparate);
    FillThemeCombo();
    if (g_opacityMenu) {
        SetWindowTextW(g_opacityMenu, (std::wstring(kVersionTitle) + L" - " + g_txt->settingsTitle).c_str());
        InvalidateRect(g_opacityMenu, nullptr, TRUE);
    }
    g_panelBase.clear();
    g_routeBase.clear();
    PresentAll();
}

HWND CreateCheckbox(HWND parent, int id, const wchar_t* text, int y, bool checked) {
    HWND box = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        16, y, kOpacityMenuWidth - 32, 26, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), g_module, nullptr);
    if (box) {
        SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.menuLabel), TRUE);
        SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    return box;
}

LRESULT CALLBACK OpacityMenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_opacitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            152, 54, 160, 30, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(1)), g_module, nullptr);
        if (g_opacitySlider) {
            SendMessageW(g_opacitySlider, TBM_SETRANGE, TRUE, MAKELONG(0, kMaxTransparency));
            SendMessageW(g_opacitySlider, TBM_SETPOS, TRUE, g_transparency);
        }
        // Background theme drop-down.
        g_themeCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            152, 96, kOpacityMenuWidth - 168, 120, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(kThemeComboId)), g_module, nullptr);
        if (g_themeCombo) {
            SendMessageW(g_themeCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.menuLabel), TRUE);
            FillThemeCombo();
        }
        // Language drop-down: every language is listed under its own name.
        g_languageCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            152, 132, kOpacityMenuWidth - 168, 200, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(kLanguageComboId)), g_module, nullptr);
        if (g_languageCombo) {
            SendMessageW(g_languageCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.menuLabel), TRUE);
            for (const UiText& text : kTexts)
                SendMessageW(g_languageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.languageName));
            SendMessageW(g_languageCombo, CB_SETCURSEL, g_language, 0);
        }
        g_gCheck = CreateCheckbox(hwnd, kGCheckId, g_txt->gMeterSeparate, 212, g_gmeterSeparate);
        g_routeCheck = CreateCheckbox(hwnd, kRouteCheckId, g_txt->routeSeparate, 244, g_routeSeparate);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        // Dark background and light text for the checkboxes and the slider.
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, Ref(kDarkPalette.textValue));
        SetBkColor(dc, kMenuBackground);
        return reinterpret_cast<LRESULT>(g_menuBrush ? g_menuBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kThemeComboId) {
            const LRESULT selection = SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
            if (selection != CB_ERR) {
                ApplyTheme(selection == 1);
                SaveTheme();
            }
            return 0;
        }
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kLanguageComboId) {
            const LRESULT selection = SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
            if (selection != CB_ERR) {
                ApplyLanguage(static_cast<int>(selection));
                SaveTheme();  // also saves the language
            }
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == kGCheckId || LOWORD(wParam) == kRouteCheckId)) {
            const bool checked = SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (LOWORD(wParam) == kGCheckId) g_gmeterSeparate = checked;
            else g_routeSeparate = checked;
            SaveLayout();
            ApplyLayout();
            return 0;
        }
        break;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_opacitySlider) {
            g_transparency = std::clamp(static_cast<int>(SendMessageW(g_opacitySlider, TBM_GETPOS, 0, 0)), 0, kMaxTransparency);
            ApplyWindowTransparency();
            InvalidateRect(hwnd, nullptr, TRUE);
            if (LOWORD(wParam) == TB_ENDTRACK) SaveTransparency();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        DrawOpacityMenu(dc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ToggleOpacityMenu() {
    if (!g_opacityMenu) return;
    if (IsWindowVisible(g_opacityMenu)) {
        ShowWindow(g_opacityMenu, SW_HIDE);
        return;
    }
    RECT target{};
    if (!g_gameWindow || !GetWindowRect(g_gameWindow, &target)) {
        target.left = 0;
        target.top = 0;
        target.right = GetSystemMetrics(SM_CXSCREEN);
        target.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    RECT outer{0, 0, kOpacityMenuWidth, kOpacityMenuHeight};
    AdjustWindowRectEx(&outer, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    const int width = outer.right - outer.left, height = outer.bottom - outer.top;
    const int x = target.left + ((target.right - target.left) - width) / 2;
    const int y = target.top + ((target.bottom - target.top) - height) / 2;
    SetWindowPos(g_opacityMenu, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_opacityMenu, SW_SHOWNOACTIVATE);
    InvalidateRect(g_opacityMenu, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Panel window and the detached cards
// ---------------------------------------------------------------------------
// Detached cards: only drawn with UpdateLayeredWindow; draggable in moving mode.
LRESULT CALLBACK CardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return g_dragMode ? HTCAPTION : HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        int failures = 0;
        if (!RegisterHotKey(hwnd, kMoveHotkeyId, MOD_CONTROL | MOD_NOREPEAT, VK_F11)) failures |= kHotkeyMoveFailed;
        if (!RegisterHotKey(hwnd, kOpacityHotkeyId, MOD_CONTROL | MOD_NOREPEAT, VK_F12)) failures |= kHotkeyOpacityFailed;
        g_hotkeyFailures = failures;
        if (failures) g_hotkeyWarningPending = true;
        SetTimer(hwnd, kTimerId, 250, nullptr);
        SetTimer(hwnd, kGMeterTimerId, kGMeterIntervalMs, nullptr);
        return 0;
    }
    case WM_HOTKEY:
        if (wParam == kMoveHotkeyId) {
            g_dragMode = !g_dragMode;
            SetWindowMovable(g_dragMode);
        } else if (wParam == kOpacityHotkeyId) {
            ToggleOpacityMenu();
        }
        return 0;
    case WM_NCHITTEST:
        return g_dragMode ? HTCAPTION : HTTRANSPARENT;
    case WM_TIMER:
        if (wParam == kGlowTimerId) {
            OnGlowTimer(hwnd);
            return 0;
        }
        if (wParam == kGMeterTimerId) {
            // G-meter: about 20 updates a second, reusing the cached panel (the glow animation,
            // when running, already presents more often and picks up the new values).
            if (!g_overlayVisible && !g_dragMode) return 0;
            UpdateGMeter();
            if (!g_glowTimerRunning) PresentAll(false, g_gmeterSeparate);
            return 0;
        }
        RefreshOverlayState();
        // Safety net: if a lamp change was missed, the glow animation catches up here.
        if (!g_glowTimerRunning && !GlowSettled()) StartGlowAnimation(hwnd);
        PresentAll();
        return 0;
    case kGlowMessage:
        StartGlowAnimation(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1; // the panel is drawn with UpdateLayeredWindow, never painted
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        KillTimer(hwnd, kGlowTimerId);
        KillTimer(hwnd, kGMeterTimerId);
        g_glowTimerRunning = false;
        UnregisterHotKey(hwnd, kMoveHotkeyId);
        UnregisterHotKey(hwnd, kOpacityHotkeyId);
        // Only positions the user is moving right now are saved; otherwise the stored ones are kept
        // (a window may have been shifted temporarily to keep it inside the game window).
        if (g_dragMode) SaveAllPositions();
        g_window = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void WindowMain() {
    LoadSettings();
    CreateFonts();
    LoadPanelAssets();
    g_menuBrush = CreateSolidBrush(kMenuBackground);
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = g_module;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"HaulETASimpleOverlayWindow01";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        DestroyFonts();
        return;
    }

    WNDCLASSEXW cardClass{};
    cardClass.cbSize = sizeof(cardClass);
    cardClass.hInstance = g_module;
    cardClass.lpfnWndProc = CardProc;
    cardClass.lpszClassName = L"HaulETASimpleCardWindow01";
    cardClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    const bool cardClassRegistered = RegisterClassExW(&cardClass) != 0;

    WNDCLASSEXW menuClass{};
    menuClass.cbSize = sizeof(menuClass);
    menuClass.hInstance = g_module;
    menuClass.lpfnWndProc = OpacityMenuProc;
    menuClass.lpszClassName = L"HaulETASimpleOpacityMenu01";
    menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    menuClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    const bool menuClassRegistered = RegisterClassExW(&menuClass) != 0;

    g_gameWindow = FindGameWindow();
    const std::wstring panelTitle = std::wstring(kVersionTitle) + L" - Kombinált panel";
    const std::wstring menuTitle = std::wstring(kVersionTitle) + L" - " + g_txt->settingsTitle;
    if (menuClassRegistered) {
        // Client area is kOpacityMenuWidth x kOpacityMenuHeight; grow the outer size for the caption.
        RECT outer{0, 0, kOpacityMenuWidth, kOpacityMenuHeight};
        AdjustWindowRectEx(&outer, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        g_opacityMenu = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            menuClass.lpszClassName, menuTitle.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
            outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, g_module, nullptr);
    }

    constexpr DWORD kLayeredStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (cardClassRegistered) {
        const std::wstring gTitle = std::wstring(kVersionTitle) + L" - G-méter";
        const std::wstring routeTitle = std::wstring(kVersionTitle) + L" - Útvonal";
        g_gWindow = CreateWindowExW(kLayeredStyle, cardClass.lpszClassName, gTitle.c_str(), WS_POPUP,
            g_gPosition.x - kGlowMargin, g_gPosition.y - kGlowMargin, g_gSurface.windowWidth, g_gSurface.windowHeight,
            nullptr, nullptr, g_module, nullptr);
        g_routeWindow = CreateWindowExW(kLayeredStyle, cardClass.lpszClassName, routeTitle.c_str(), WS_POPUP,
            g_routePosition.x - kGlowMargin, g_routePosition.y - kGlowMargin, g_routeSurface.windowWidth, g_routeSurface.windowHeight,
            nullptr, nullptr, g_module, nullptr);
    }

    HWND window = CreateWindowExW(kLayeredStyle,
        wc.lpszClassName, panelTitle.c_str(), WS_POPUP, g_position.x - kGlowMargin, g_position.y - kGlowMargin,
        g_mainSurface.windowWidth, g_mainSurface.windowHeight,
        nullptr, nullptr, g_module, nullptr);
    if (window) {
        g_window = window;
        ApplyWindowTransparency();
        ShowWindow(window, SW_HIDE);
        RefreshOverlayState(true);
        MSG msg{};
        while (g_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (HWND remaining = g_window.load()) DestroyWindow(remaining);
    }

    for (HWND* card : {&g_gWindow, &g_routeWindow}) {
        if (*card) DestroyWindow(*card);
        *card = nullptr;
    }
    if (g_opacityMenu) {
        DestroyWindow(g_opacityMenu);
        g_opacityMenu = nullptr;
    }
    g_opacitySlider = nullptr;
    g_gCheck = nullptr;
    g_routeCheck = nullptr;
    g_themeCombo = nullptr;
    g_languageCombo = nullptr;
    if (menuClassRegistered) UnregisterClassW(menuClass.lpszClassName, g_module);
    if (cardClassRegistered) UnregisterClassW(cardClass.lpszClassName, g_module);
    UnregisterClassW(wc.lpszClassName, g_module);
    if (g_menuBrush) DeleteObject(g_menuBrush);
    g_menuBrush = nullptr;
    DestroyCanvas();
    DestroyFonts();
    g_panelBase.clear();
    g_routeBase.clear();
    g_overlayVisible = false;
    g_dragMode = false;
    g_glowLeft = 0.0;
    g_glowRight = 0.0;
    g_lightWhite = 0.0;
    g_lightBlue = 0.0;
    g_gmeter = GMeterState{};
}

void StopWindow() {
    g_running = false;
    if (HWND window = g_window.load()) PostMessageW(window, WM_CLOSE, 0, 0);
    if (g_windowThread.joinable()) g_windowThread.join();
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01)
        return SCS_RESULT_unsupported;
    if (!params) return SCS_RESULT_invalid_parameter;
    const auto* p = static_cast<const scs_telemetry_init_params_v101_t*>(params);
    g_log = p->common.log;
    auto warn = [](const char* message) { if (g_log) g_log(SCS_LOG_TYPE_warning, message); };

    const auto startedResult = p->register_for_event(SCS_TELEMETRY_EVENT_started, OnTelemetryEvent, nullptr);
    const auto pausedResult = p->register_for_event(SCS_TELEMETRY_EVENT_paused, OnTelemetryEvent, nullptr);
    const auto configurationResult = p->register_for_event(SCS_TELEMETRY_EVENT_configuration, OnTelemetryEvent, nullptr);
    if (startedResult != SCS_RESULT_ok || pausedResult != SCS_RESULT_ok)
        warn("HaulETA: driving-state events are unavailable; the overlay will remain hidden until gameplay starts.");
    if (configurationResult != SCS_RESULT_ok)
        warn("HaulETA: truck configuration is unavailable; the fuel gauge may not show a level.");

    struct ChannelRegistration {
        const char* name;
        scs_value_type_t type;
        scs_u32_t flags;
        scs_telemetry_channel_callback_t callback;
        const char* failureMessage;
    };
    const ChannelRegistration channels[] = {
        {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreDistance, "HaulETA: GPS route distance channel is unavailable."},
        {SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none,
            StoreSpeed, "HaulETA: truck speed channel is unavailable; trip timing starts when route movement is observed."},
        {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreNavigationTime, "HaulETA: navigation time channel is unavailable."},
        {SCS_TELEMETRY_CHANNEL_local_scale, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreLocalScale, "HaulETA: local time scale channel is unavailable."},
        {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreSpeedLimit, "HaulETA: navigation speed limit channel is unavailable."},
        {SCS_TELEMETRY_TRUCK_CHANNEL_fuel, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreFuel, "HaulETA: fuel amount channel is unavailable."},
        {SCS_TELEMETRY_CHANNEL_next_rest_stop, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreRestStop, "HaulETA: rest-state channel is unavailable; the fatigue figure will show only its outline."},
        {SCS_TELEMETRY_CHANNEL_game_time, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            StoreGameTime, "HaulETA: in-game clock color channel is unavailable; the clock will stay blue."},
    };
    for (const auto& channel : channels) {
        if (p->register_for_channel(channel.name, SCS_U32_NIL, channel.type, channel.flags,
                channel.callback, nullptr) != SCS_RESULT_ok)
            warn(channel.failureMessage);
    }
    // Blinker lamps for the amber edge glow; the context tells the shared callback which side it is.
    static int rightSide = 1;
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_lblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
            SCS_TELEMETRY_CHANNEL_FLAG_none, StoreBlinker, nullptr) != SCS_RESULT_ok ||
        p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_rblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
            SCS_TELEMETRY_CHANNEL_FLAG_none, StoreBlinker, &rightSide) != SCS_RESULT_ok)
        warn("HaulETA: blinker lamp channels are unavailable; the panel edge will not light up with the indicators.");
    // Head lamps for the white / blue frame glow; the context points at the flag each one sets.
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_parking, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
            SCS_TELEMETRY_CHANNEL_FLAG_none, StoreLamp, &g_parkingLights) != SCS_RESULT_ok ||
        p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_low_beam, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
            SCS_TELEMETRY_CHANNEL_FLAG_none, StoreLamp, &g_lowBeam) != SCS_RESULT_ok ||
        p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_high_beam, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
            SCS_TELEMETRY_CHANNEL_FLAG_none, StoreLamp, &g_highBeam) != SCS_RESULT_ok)
        warn("HaulETA: head lamp channels are unavailable; the frame will not light up with the headlights.");
    // Vehicle acceleration for the G-meter.
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration, SCS_U32_NIL, SCS_VALUE_TYPE_fvector,
            SCS_TELEMETRY_CHANNEL_FLAG_no_value, StoreAcceleration, nullptr) != SCS_RESULT_ok)
        warn("HaulETA: acceleration channel is unavailable; the G-meter will show no values.");

    g_running = true;
    g_driverActive = false;
    if (!g_windowThread.joinable()) g_windowThread = std::thread(WindowMain);
    return SCS_RESULT_ok;
}

extern "C" __declspec(dllexport) SCSAPI_VOID scs_telemetry_shutdown(void) {
    StopWindow();
    g_log = nullptr;
    g_hasRoute = false;
    g_hasDistance = false;
    g_hasNavigationTime = false;
    g_hasLocalScale = false;
    g_hasSpeedLimit = false;
    g_hasFuel = false;
    g_hasRestStop = false;
    g_restStopMaxMinutes = 0;
    g_fuelCapacityLiters = 0.0f;
    g_hasGameTime = false;
    g_driverActive = false;
    g_tripStarted = false;
    g_tripCompleted = false;
    g_hotkeyFailures = 0;
    g_hotkeyWarningPending = false;
    g_leftBlinkerLit = false;
    g_rightBlinkerLit = false;
    g_parkingLights = false;
    g_lowBeam = false;
    g_highBeam = false;
    g_hasAccel = false;
    std::lock_guard<std::mutex> lock(g_jobMutex);
    g_job = JobInfo{};
}
