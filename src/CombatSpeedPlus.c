/*
* Combat Speed+
*
* Hooks the game's "combat_speed" selector registration and replaces
* the default option list with custom combat speed values ranging
* from min to max in specific increments. Introduces keyboard shortcuts
* to adjust combat speed dynamically during gameplay, and updates a
* newly added combat speed text display within the battle HUD...
*
*/

#include "CombatSpeedPlus.h"
#include "mewjector.h"
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MOD_NAME "Combat Speed+"

static const bool ENABLE_DEBUG_LOGS = false;

#define COMBAT_SPEED_KEY "combat_speed"
#define COMBAT_HUD_TEXT_NAME "combat_hud_text"
#define COMBAT_HUD_ROOT_OFFSET 0x38
#define DEFAULT_COMBAT_SPEED_MIN 0.25
#define DEFAULT_COMBAT_SPEED_MAX 8.00
#define DEFAULT_COMBAT_SPEED_STEP 0.25
#define DEFAULT_RUNTIME_COMBAT_SPEED 1.0f
#define DEFAULT_PRESET_SPEED_COUNT 8U
#define MAX_COMBAT_SPEED_COUNT 256U

#define MAX_HOTKEYS_PER_BINDING 2U

typedef struct HotkeyBinding
{
    int keys[MAX_HOTKEYS_PER_BINDING];
} HotkeyBinding;

static MewjectorAPI g_mj;
static fn_register_selector g_origRegisterSelector = NULL;
static fn_refresh_combat_speed g_origRefreshCombatSpeed = NULL;
static fn_refresh_combat_hud g_origRefreshCombatHud = NULL;
static fn_game_allocate g_gameAllocate = NULL;
static fn_settings_set_string g_settingsSetString = NULL;
static fn_settings_get_float g_settingsGetFloat = NULL;
static fn_find_ui_child g_findUiChild = NULL;
static fn_init_narrow_string_from_literal g_initNarrowString = NULL;
static fn_set_text_element_string g_setTextElementString = NULL;
static double g_combatSpeedMin = DEFAULT_COMBAT_SPEED_MIN;
static double g_combatSpeedMax = DEFAULT_COMBAT_SPEED_MAX;
static double g_combatSpeedStep = DEFAULT_COMBAT_SPEED_STEP;
static uint32_t g_combatSpeedCount = 32U;
static double g_presetCombatSpeeds[DEFAULT_PRESET_SPEED_COUNT] = { 1.00, 2.00, 3.00, 4.00, 5.00, 6.00, 7.00, 8.00 };
static HotkeyBinding g_decreaseHotkey = { { VK_OEM_MINUS, VK_SUBTRACT } };
static HotkeyBinding g_increaseHotkey = { { VK_OEM_PLUS, VK_ADD } };
static HotkeyBinding g_resetHotkey = { { 0x30, VK_NUMPAD0 } };

static HotkeyBinding g_presetHotkeys[DEFAULT_PRESET_SPEED_COUNT] =
{
    { { 0x31, VK_NUMPAD1 } },
    { { 0x32, VK_NUMPAD2 } },
    { { 0x33, VK_NUMPAD3 } },
    { { 0x34, VK_NUMPAD4 } },
    { { 0x35, VK_NUMPAD5 } },
    { { 0x36, VK_NUMPAD6 } },
    { { 0x37, VK_NUMPAD7 } },
    { { 0x38, VK_NUMPAD8 } }
};

static wchar_t g_configPathW[MAX_PATH] = { 0 };
static HMODULE g_hModule = NULL;
static HANDLE g_hotkeyThread = NULL;
static HANDLE g_hotkeyStopEvent = NULL;
static volatile LONG g_hotkeyThreadStarted = 0;
static void* volatile g_lastCombatSpeedOwner = NULL;
static volatile LONG g_pendingCombatSpeedSteps = 0;
static volatile LONG g_hasPendingCombatSpeedTarget = 0;
static double g_pendingCombatSpeedTarget = DEFAULT_RUNTIME_COMBAT_SPEED;
static volatile LONG g_isApplyingHotkeyChange = 0;
static void* volatile g_lastCombatHudOwner = NULL;

static void Log(const char* fmt, ...)
{
    char buffer[512];
    va_list ap;

    if (!ENABLE_DEBUG_LOGS)
    {
        return;
    }

    if (!g_mj.Log)
    {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    g_mj.Log(MOD_NAME, "%s", buffer);
}

static const char* GetNarrowStringData(const NarrowString* value)
{
    if (!value)
    {
        return NULL;
    }

    if (value->capacity > 15ULL)
    {
        return value->storage.heap_ptr;
    }

    return value->storage.inline_buf;
}

static size_t GetNarrowStringSize(const NarrowString* value)
{
    if (!value)
    {
        return 0U;
    }

    return (size_t)value->size;
}

static int StringEqualsLiteral(const NarrowString* value, const char* literal)
{
    const char* data;
    size_t literalLength;
    size_t valueLength;

    if (!value || !literal)
    {
        return 0;
    }

    data = GetNarrowStringData(value);
    literalLength = strlen(literal);
    valueLength = GetNarrowStringSize(value);

    if (!data || valueLength != literalLength)
    {
        return 0;
    }

    return memcmp(data, literal, literalLength) == 0;
}

static void InitSmallNarrowString(NarrowString* outString, const char* text)
{
    size_t length;

    memset(outString, 0, sizeof(*outString));

    if (!text)
    {
        outString->storage.inline_buf[0] = '\0';
        outString->size = 0ULL;
        outString->capacity = 15ULL;
        return;
    }

    length = strlen(text);

    if (length > 15U)
    {
        length = 15U;
    }

    memcpy(outString->storage.inline_buf, text, length);
    outString->storage.inline_buf[length] = '\0';
    outString->size = (uint64_t)length;
    outString->capacity = 15ULL;
}

static void InitSmallWideString(WideString* outString, const wchar_t* text)
{
    size_t length;

    memset(outString, 0, sizeof(*outString));

    if (!text)
    {
        outString->storage.inline_buf[0] = L'\0';
        outString->size = 0ULL;
        outString->capacity = 7ULL;
        return;
    }

    length = wcslen(text);

    /* 
    * Force label to fit WideString inline storage...
    * Heap allocation should never be needed...
    */
    if (length > 7U)
    {
        length = 7U;
    }

    memcpy(outString->storage.inline_buf, text, length * sizeof(wchar_t));
    outString->storage.inline_buf[length] = L'\0';
    outString->size = (uint64_t)length;
    outString->capacity = 7ULL;
}

static void FormatValueString(double value, char* outBuffer, size_t outBufferSize)
{
    int written;
    size_t length;

    written = snprintf(outBuffer, outBufferSize, "%.2f", value);

    if (written < 0)
    {
        outBuffer[0] = '\0';
        return;
    }

    length = strlen(outBuffer);

    while (length > 0U && outBuffer[length - 1U] == '0')
    {
        outBuffer[length - 1U] = '\0';
        length--;
    }

    if (length > 0U && outBuffer[length - 1U] == '.')
    {
        outBuffer[length - 1U] = '\0';
    }
}

static void FormatLabelString(double value, wchar_t* outBuffer, size_t outBufferCount)
{
    char valueText[16];
    int written;

    FormatValueString(value, valueText, sizeof(valueText));
    written = swprintf(outBuffer, outBufferCount, L"%Sx", valueText);

    if (written < 0)
    {
        outBuffer[0] = L'\0';
    }
}

static void StartHotkeyThread(void);
static double ClampCombatSpeed(double value);

static int StringEqualsInsensitive(const char* left, const char* right)
{
    char leftChar;
    char rightChar;

    if (!left || !right)
    {
        return 0;
    }

    while (*left && *right)
    {
        leftChar = *left;
        rightChar = *right;

        if (leftChar >= 'a' && leftChar <= 'z')
        {
            leftChar = (char)(leftChar - ('a' - 'A'));
        }

        if (rightChar >= 'a' && rightChar <= 'z')
        {
            rightChar = (char)(rightChar - ('a' - 'A'));
        }

        if (leftChar != rightChar)
        {
            return 0;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

static void TrimToken(char* text)
{
    char* start;
    char* end;
    size_t length;

    if (!text)
    {
        return;
    }

    start = text;

    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
    {
        ++start;
    }

    end = start + strlen(start);

    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        --end;
    }

    length = (size_t)(end - start);

    if (start != text && length > 0U)
    {
        memmove(text, start, length);
    }

    text[length] = '\0';
}

static int IsUnsetHotkeyValue(const char* text)
{
    char buffer[128];

    if (!text)
    {
        return 1;
    }

    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s", text);
    TrimToken(buffer);

    if (!buffer[0])
    {
        return 1;
    }

    return StringEqualsInsensitive(buffer, "NONE");
}

static int ParseFunctionKeyName(const char* text)
{
    char* parseEnd;
    long functionIndex;

    if (!text || (text[0] != 'F' && text[0] != 'f'))
    {
        return 0;
    }

    functionIndex = strtol(text + 1, &parseEnd, 10);

    if (parseEnd == text + 1 || *parseEnd != '\0')
    {
        return 0;
    }

    if (functionIndex < 1L || functionIndex > 24L)
    {
        return 0;
    }

    return VK_F1 + (int)(functionIndex - 1L);
}

static int ParseNamedVirtualKey(const char* text)
{
    static const struct
    {
        const char* name;
        int virtualKey;
    } namedKeys[] =
    {
        { "BACKSPACE", VK_BACK },
        { "TAB", VK_TAB },
        { "ENTER", VK_RETURN },
        { "RETURN", VK_RETURN },
        { "SHIFT", VK_SHIFT },
        { "LSHIFT", VK_LSHIFT },
        { "RSHIFT", VK_RSHIFT },
        { "CTRL", VK_CONTROL },
        { "CONTROL", VK_CONTROL },
        { "LCTRL", VK_LCONTROL },
        { "RCTRL", VK_RCONTROL },
        { "ALT", VK_MENU },
        { "MENU", VK_MENU },
        { "LALT", VK_LMENU },
        { "RALT", VK_RMENU },
        { "PAUSE", VK_PAUSE },
        { "CAPSLOCK", VK_CAPITAL },
        { "ESC", VK_ESCAPE },
        { "ESCAPE", VK_ESCAPE },
        { "SPACE", VK_SPACE },
        { "PAGEUP", VK_PRIOR },
        { "PAGEDOWN", VK_NEXT },
        { "END", VK_END },
        { "HOME", VK_HOME },
        { "LEFT", VK_LEFT },
        { "UP", VK_UP },
        { "RIGHT", VK_RIGHT },
        { "DOWN", VK_DOWN },
        { "INSERT", VK_INSERT },
        { "DELETE", VK_DELETE },
        { "NUMPAD0", VK_NUMPAD0 },
        { "NUMPAD1", VK_NUMPAD1 },
        { "NUMPAD2", VK_NUMPAD2 },
        { "NUMPAD3", VK_NUMPAD3 },
        { "NUMPAD4", VK_NUMPAD4 },
        { "NUMPAD5", VK_NUMPAD5 },
        { "NUMPAD6", VK_NUMPAD6 },
        { "NUMPAD7", VK_NUMPAD7 },
        { "NUMPAD8", VK_NUMPAD8 },
        { "NUMPAD9", VK_NUMPAD9 },
        { "MULTIPLY", VK_MULTIPLY },
        { "ADD", VK_ADD },
        { "SUBTRACT", VK_SUBTRACT },
        { "DECIMAL", VK_DECIMAL },
        { "DIVIDE", VK_DIVIDE },
        { "OEM_PLUS", VK_OEM_PLUS },
        { "PLUS", VK_OEM_PLUS },
        { "OEM_MINUS", VK_OEM_MINUS },
        { "MINUS", VK_OEM_MINUS },
        { "COMMA", VK_OEM_COMMA },
        { "PERIOD", VK_OEM_PERIOD },
        { "SLASH", VK_OEM_2 },
        { "SEMICOLON", VK_OEM_1 },
        { "QUOTE", VK_OEM_7 },
        { "LBRACKET", VK_OEM_4 },
        { "RBRACKET", VK_OEM_6 },
        { "BACKSLASH", VK_OEM_5 },
        { "TILDE", VK_OEM_3 }
    };
    
    size_t index;
    int functionKey;

    functionKey = ParseFunctionKeyName(text);

    if (functionKey != 0)
    {
        return functionKey;
    }

    for (index = 0U; index < sizeof(namedKeys) / sizeof(namedKeys[0]); ++index)
    {
        if (StringEqualsInsensitive(text, namedKeys[index].name))
        {
            return namedKeys[index].virtualKey;
        }
    }

    return 0;
}

static int ParseVirtualKeyToken(const char* text)
{
    char* parseEnd;
    long numericValue;
    int namedValue;

    if (!text || !text[0])
    {
        return 0;
    }

    if (text[1] == '\0')
    {
        if (text[0] >= 'a' && text[0] <= 'z')
        {
            return text[0] - ('a' - 'A');
        }

        if ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= '0' && text[0] <= '9'))
        {
            return text[0];
        }
    }

    namedValue = ParseNamedVirtualKey(text);

    if (namedValue != 0)
    {
        return namedValue;
    }

    numericValue = strtol(text, &parseEnd, 0);

    if (parseEnd == text || *parseEnd != '\0')
    {
        return 0;
    }

    if (numericValue <= 0L || numericValue > 255L)
    {
        return 0;
    }

    return (int)numericValue;
}

static void ParseHotkeyBinding(const char* text, HotkeyBinding* binding)
{
    char buffer[128];
    char* cursor;
    uint32_t parsedCount;

    if (!binding)
    {
        return;
    }

    memset(binding->keys, 0, sizeof(binding->keys));

    if (IsUnsetHotkeyValue(text))
    {
        return;
    }

    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s", text);
    cursor = buffer;
    parsedCount = 0U;

    while (cursor && parsedCount < MAX_HOTKEYS_PER_BINDING)
    {
        char* separator;
        int virtualKey;

        separator = strchr(cursor, ',');

        if (separator)
        {
            *separator = '\0';
        }

        TrimToken(cursor);
        virtualKey = ParseVirtualKeyToken(cursor);

        if (virtualKey != 0)
        {
            binding->keys[parsedCount] = virtualKey;
            ++parsedCount;
        }

        if (!separator)
        {
            break;
        }

        cursor = separator + 1;
    }
}

static int IsHotkeyBindingPressed(const HotkeyBinding* binding)
{
    uint32_t index;

    if (!binding)
    {
        return 0;
    }

    for (index = 0U; index < MAX_HOTKEYS_PER_BINDING; ++index)
    {
        if (binding->keys[index] != 0 && (GetAsyncKeyState(binding->keys[index]) & 0x8000) != 0)
        {
            return 1;
        }
    }

    return 0;
}

static double ParseConfigDouble(const char* text, double fallbackValue)
{
    char* parseEnd;
    double parsed;

    if (!text || !text[0])
    {
        return fallbackValue;
    }

    parsed = strtod(text, &parseEnd);

    if (parseEnd == text)
    {
        return fallbackValue;
    }

    return parsed;
}

static double SnapValueToConfiguredStep(double value)
{
    double relative;
    double snappedIndex;
    double snappedValue;

    relative = (value - g_combatSpeedMin) / g_combatSpeedStep;
    snappedIndex = floor(relative + 0.5);
    snappedValue = g_combatSpeedMin + (snappedIndex * g_combatSpeedStep);
    return ClampCombatSpeed(snappedValue);
}

static void BuildDefaultPresetCombatSpeeds(double* outPresets, uint32_t presetCount)
{
    static const double normalizedDefaults[DEFAULT_PRESET_SPEED_COUNT] = { 1.00, 2.00, 3.00, 4.00, 5.00, 6.00, 7.00, 8.00 };
    uint32_t index;

    if (!outPresets)
    {
        return;
    }

    for (index = 0U; index < presetCount; ++index)
    {
        double presetValue;

        if (index < DEFAULT_PRESET_SPEED_COUNT)
        {
            presetValue = normalizedDefaults[index];
        }
        else
        {
            presetValue = g_combatSpeedMin;
        }

        if (presetValue < g_combatSpeedMin)
        {
            presetValue = g_combatSpeedMin;
        }

        if (presetValue > g_combatSpeedMax)
        {
            presetValue = g_combatSpeedMax;
        }

        outPresets[index] = SnapValueToConfiguredStep(presetValue);
    }
}

static void BuildDefaultConfigPathFromModule(void)
{
    wchar_t modulePathW[MAX_PATH];
    wchar_t* extension;

    g_configPathW[0] = L'\0';
    modulePathW[0] = L'\0';

    if (!g_hModule)
    {
        return;
    }

    if (!GetModuleFileNameW(g_hModule, modulePathW, MAX_PATH))
    {
        return;
    }

    wcsncpy(g_configPathW, modulePathW, MAX_PATH - 1U);
    g_configPathW[MAX_PATH - 1U] = L'\0';

    extension = wcsrchr(g_configPathW, L'.');

    if (extension)
    {
        wcscpy(extension, L".ini");
    }
    else
    {
        wcscat(g_configPathW, L".ini");
    }
}

static void LoadCombatSpeedConfig(void)
{
    char valueText[64];
    double configuredMin;
    double configuredMax;
    double configuredStep;
    double span;
    double stepsFloat;
    uint32_t computedCount;
    uint32_t presetIndex;

    BuildDefaultConfigPathFromModule();

    configuredMin = DEFAULT_COMBAT_SPEED_MIN;
    configuredMax = DEFAULT_COMBAT_SPEED_MAX;
    configuredStep = DEFAULT_COMBAT_SPEED_STEP;

    if (g_configPathW[0] != L'\0')
    {
        wchar_t valueTextW[64];
        int converted;

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"CombatSpeed", L"Min", L"0.25", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted <= 0)
        {
            valueText[0] = '\0';
        }

        configuredMin = ParseConfigDouble(valueText, configuredMin);

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"CombatSpeed", L"Max", L"8.00", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted <= 0)
        {
            valueText[0] = '\0';
        }

        configuredMax = ParseConfigDouble(valueText, configuredMax);

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"CombatSpeed", L"Step", L"0.25", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted <= 0)
        {
            valueText[0] = '\0';
        }

        configuredStep = ParseConfigDouble(valueText, configuredStep);
    }

    if (configuredMin <= 0.0)
    {
        configuredMin = DEFAULT_COMBAT_SPEED_MIN;
    }

    if (configuredMax < configuredMin)
    {
        configuredMax = configuredMin;
    }

    if (configuredStep <= 0.0)
    {
        configuredStep = DEFAULT_COMBAT_SPEED_STEP;
    }

    span = configuredMax - configuredMin;
    stepsFloat = (span / configuredStep) + 1.0000001;

    if (stepsFloat < 1.0)
    {
        stepsFloat = 1.0;
    }

    computedCount = (uint32_t)stepsFloat;

    if (computedCount > MAX_COMBAT_SPEED_COUNT)
    {
        computedCount = MAX_COMBAT_SPEED_COUNT;
        configuredMax = configuredMin + ((double)(computedCount - 1U) * configuredStep);
    }

    g_combatSpeedMin = configuredMin;
    g_combatSpeedMax = configuredMax;
    g_combatSpeedStep = configuredStep;
    g_combatSpeedCount = computedCount;

    BuildDefaultPresetCombatSpeeds(g_presetCombatSpeeds, DEFAULT_PRESET_SPEED_COUNT);

    if (g_configPathW[0] != L'\0')
    {
        wchar_t valueTextW[64];
        int converted;

        for (presetIndex = 0U; presetIndex < DEFAULT_PRESET_SPEED_COUNT; ++presetIndex)
        {
            wchar_t keyNameW[16];
            double configuredPreset;

            _snwprintf_s(keyNameW, sizeof(keyNameW) / sizeof(keyNameW[0]), _TRUNCATE, L"Preset%u", presetIndex + 1U);
            valueText[0] = '\0';
            valueTextW[0] = L'\0';
            GetPrivateProfileStringW(L"CombatSpeed", keyNameW, L"", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
            converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

            if (converted > 1)
            {
                configuredPreset = ParseConfigDouble(valueText, g_presetCombatSpeeds[presetIndex]);
                g_presetCombatSpeeds[presetIndex] = SnapValueToConfiguredStep(configuredPreset);
            }
        }

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"Hotkeys", L"Decrease", L"MISSING", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted > 0 && strcmp(valueText, "MISSING") != 0)
        {
            ParseHotkeyBinding(valueText, &g_decreaseHotkey);
        }

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"Hotkeys", L"Increase", L"MISSING", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted > 0 && strcmp(valueText, "MISSING") != 0)
        {
            ParseHotkeyBinding(valueText, &g_increaseHotkey);
        }

        valueText[0] = '\0';
        valueTextW[0] = L'\0';
        GetPrivateProfileStringW(L"Hotkeys", L"Reset", L"MISSING", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
        converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

        if (converted > 0 && strcmp(valueText, "MISSING") != 0)
        {
            ParseHotkeyBinding(valueText, &g_resetHotkey);
        }

        for (presetIndex = 0U; presetIndex < DEFAULT_PRESET_SPEED_COUNT; ++presetIndex)
        {
            wchar_t keyNameW[16];

            _snwprintf_s(keyNameW, sizeof(keyNameW) / sizeof(keyNameW[0]), _TRUNCATE, L"Preset%u", presetIndex + 1U);
            valueText[0] = '\0';
            valueTextW[0] = L'\0';
            GetPrivateProfileStringW(L"Hotkeys", keyNameW, L"MISSING", valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), g_configPathW);
            converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, valueText, (int)sizeof(valueText), NULL, NULL);

            if (converted > 0 && strcmp(valueText, "MISSING") != 0)
            {
                ParseHotkeyBinding(valueText, &g_presetHotkeys[presetIndex]);
            }
        }
    }

    Log("CombatSpeed config loaded: min=%.2f max=%.2f step=%.2f count=%u", g_combatSpeedMin, g_combatSpeedMax, g_combatSpeedStep, g_combatSpeedCount);
}

static int ResolveGameAllocator(void)
{
    UINT_PTR gameBase;
    UINT_PTR allocatorAddress;

    if (g_gameAllocate)
    {
        return 1;
    }

    if (!g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return 0;
    }

    allocatorAddress = gameBase + (UINT_PTR)RVA_GAME_ALLOCATOR;
    g_gameAllocate = (fn_game_allocate)allocatorAddress;
    return 1;
}

static int ResolveRuntimeFunctions(void)
{
    UINT_PTR gameBase;

    if (!g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return 0;
    }

    g_settingsSetString = (fn_settings_set_string)(gameBase + (UINT_PTR)RVA_SETTINGS_SET_STRING);
    g_settingsGetFloat = (fn_settings_get_float)(gameBase + (UINT_PTR)RVA_SETTINGS_GET_FLOAT);
    g_findUiChild = (fn_find_ui_child)(gameBase + (UINT_PTR)RVA_FIND_UI_CHILD);
    g_initNarrowString = (fn_init_narrow_string_from_literal)(gameBase + (UINT_PTR)RVA_INIT_NARROW_STRING);
    g_setTextElementString = (fn_set_text_element_string)(gameBase + (UINT_PTR)RVA_SET_TEXT_ELEMENT_STRING);

    if (!g_origRefreshCombatSpeed)
    {
        /*
        * This gets replaced with the trampoline after the hook is installed...
        * BUT before that, keep a direct pointer available for fallback...
        */
        g_origRefreshCombatSpeed = (fn_refresh_combat_speed)(gameBase + (UINT_PTR)RVA_REFRESH_COMBAT_SPEED);
    }

    return 1;
}

static void* GetSettingsRoot(void)
{
    UINT_PTR gameBase;
    UINT_PTR singletonAddress;
    UINT_PTR managerObject;

    if (!g_mj.GetGameBase)
    {
        return NULL;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return NULL;
    }

    singletonAddress = gameBase + (UINT_PTR)RVA_SETTINGS_SINGLETON;
    managerObject = *(UINT_PTR*)singletonAddress;

    if (!managerObject)
    {
        return NULL;
    }

    return (void*)(managerObject + (UINT_PTR)SETTINGS_ROOT_OFFSET);
}

static double ClampCombatSpeed(double value)
{
    if (value < g_combatSpeedMin)
    {
        value = g_combatSpeedMin;
    }

    if (value > g_combatSpeedMax)
    {
        value = g_combatSpeedMax;
    }

    return value;
}

static double SnapCombatSpeedToStep(double value)
{
    return SnapValueToConfiguredStep(value);
}

/*
* Replace the game's original combat_speed selector entries entirely
* instead of patching the incoming option vector! For these reasons:
*
* 1. Actual option memory ownership is a friggin' mystery...
* 2. Rebuilding guarantees us valid ordering/count...
* 3. Using the game allocator to reduce allocator mismatch...
*/
static int BuildCombatSpeedOptions(OptionVector* outVector)
{
    SelectorOption* entries;
    uint32_t index;

    if (!outVector)
    {
        return 0;
    }

    memset(outVector, 0, sizeof(*outVector));

    if (!ResolveGameAllocator() || !g_gameAllocate)
    {
        return 0;
    }

    entries = (SelectorOption*)g_gameAllocate(sizeof(SelectorOption) * g_combatSpeedCount);

    if (!entries)
    {
        return 0;
    }

    memset(entries, 0, sizeof(SelectorOption) * g_combatSpeedCount);

    for (index = 0U; index < g_combatSpeedCount; ++index)
    {
        char valueText[16];
        wchar_t labelText[16];
        double value;

        value = g_combatSpeedMin + ((double)index * g_combatSpeedStep);

        FormatValueString(value, valueText, sizeof(valueText));
        FormatLabelString(value, labelText, sizeof(labelText));

        InitSmallNarrowString(&entries[index].value, valueText);
        InitSmallWideString(&entries[index].label, labelText);
    }

    outVector->begin = entries;
    outVector->end = entries + g_combatSpeedCount;
    outVector->capacity_end = entries + g_combatSpeedCount;
    return 1;
}

static double ReadConfiguredCombatSpeed(void)
{
    NarrowString key;
    void* settingsRoot;
    float value;

    if (!ResolveRuntimeFunctions() || !g_settingsGetFloat)
    {
        return DEFAULT_RUNTIME_COMBAT_SPEED;
    }

    settingsRoot = GetSettingsRoot();

    if (!settingsRoot)
    {
        return DEFAULT_RUNTIME_COMBAT_SPEED;
    }

    InitSmallNarrowString(&key, COMBAT_SPEED_KEY);
    value = g_settingsGetFloat(settingsRoot, &key, DEFAULT_RUNTIME_COMBAT_SPEED);
    return (double)value;
}

static void ApplyConfiguredCombatSpeedToOwner(void* owner)
{
    if (!owner)
    {
        return;
    }

    if (!ResolveRuntimeFunctions() || !g_origRefreshCombatSpeed)
    {
        return;
    }

    g_origRefreshCombatSpeed(owner);
}

static int WriteConfiguredCombatSpeed(double value)
{
    NarrowString key;
    NarrowString valueString;
    char valueText[16];
    void* settingsRoot;

    if (!ResolveRuntimeFunctions() || !g_settingsSetString)
    {
        return 0;
    }

    settingsRoot = GetSettingsRoot();

    if (!settingsRoot)
    {
        return 0;
    }

    value = ClampCombatSpeed(SnapCombatSpeedToStep(value));
    FormatValueString(value, valueText, sizeof(valueText));

    InitSmallNarrowString(&key, COMBAT_SPEED_KEY);
    InitSmallNarrowString(&valueString, valueText);

    g_settingsSetString(settingsRoot, &key, &valueString);
    Log("Combat speed set to %s", valueText);
    return 1;
}

static void* FindNamedCombatHudElement(void* hudOwner, const char* elementNameText)
{
    void* uiRoot;
    NarrowString elementName;
    void* textElement;

    if (!hudOwner || !elementNameText)
    {
        return NULL;
    }

    if (!ResolveRuntimeFunctions())
    {
        return NULL;
    }

    if (!g_findUiChild || !g_initNarrowString)
    {
        return NULL;
    }

    uiRoot = *(void**)((uint8_t*)hudOwner + COMBAT_HUD_ROOT_OFFSET);

    if (!uiRoot)
    {
        return NULL;
    }

    memset(&elementName, 0, sizeof(elementName));
    g_initNarrowString(&elementName, elementNameText);
    textElement = g_findUiChild(uiRoot, &elementName);
    return textElement;
}

static int SetNamedCombatHudText(void* hudOwner, const char* elementNameText, const char* text)
{
    void* textElement;
    NarrowString textValue;

    if (!hudOwner || !elementNameText || !text)
    {
        return 0;
    }

    if (!ResolveRuntimeFunctions())
    {
        return 0;
    }

    if (!g_setTextElementString || !g_initNarrowString)
    {
        return 0;
    }

    textElement = FindNamedCombatHudElement(hudOwner, elementNameText);

    if (!textElement)
    {
        return 0;
    }

    memset(&textValue, 0, sizeof(textValue));
    g_initNarrowString(&textValue, text);
    g_setTextElementString(textElement, &textValue, 1U, 1U);
    return 1;
}

static void UpdateCombatHudSpeedText(void* hudOwner)
{
    char displayText[64];
    char speedText[16];
    double combatSpeedValue;

    if (!hudOwner)
    {
        return;
    }

    combatSpeedValue = ReadConfiguredCombatSpeed();
    combatSpeedValue = ClampCombatSpeed(SnapCombatSpeedToStep(combatSpeedValue));

    FormatValueString(combatSpeedValue, speedText, sizeof(speedText));
    _snprintf_s(displayText, sizeof(displayText), _TRUNCATE, "Combat speed: %sx", speedText);
    SetNamedCombatHudText(hudOwner, COMBAT_HUD_TEXT_NAME, displayText);
}

static int IsShiftHeld(void)
{
    SHORT leftState;
    SHORT rightState;

    leftState = GetAsyncKeyState(VK_LSHIFT);
    rightState = GetAsyncKeyState(VK_RSHIFT);
    return ((leftState & 0x8000) != 0) || ((rightState & 0x8000) != 0);
}

static int BuildCombatSpeedDeltaFromDirection(int direction)
{
    double deltaValue;
    LONG deltaSteps;

    if (direction == 0)
    {
        return 0;
    }

    deltaValue = g_combatSpeedStep;

    if (IsShiftHeld())
    {
        deltaValue = 1.0;
    }

    deltaSteps = (LONG)floor((deltaValue / g_combatSpeedStep) + 0.5);

    if (deltaSteps < 1)
    {
        deltaSteps = 1;
    }

    if (direction < 0)
    {
        deltaSteps = -deltaSteps;
    }

    return (int)deltaSteps;
}

static void QueueCombatSpeedReset(void)
{
    double targetValue;

    targetValue = ClampCombatSpeed(SnapCombatSpeedToStep(1.0));
    g_pendingCombatSpeedTarget = targetValue;
    InterlockedExchange(&g_pendingCombatSpeedSteps, 0);
    InterlockedExchange(&g_hasPendingCombatSpeedTarget, 1);
}

static void QueueCombatSpeedPreset(uint32_t presetIndex)
{
    double targetValue;

    if (presetIndex >= DEFAULT_PRESET_SPEED_COUNT)
    {
        return;
    }

    targetValue = ClampCombatSpeed(SnapCombatSpeedToStep(g_presetCombatSpeeds[presetIndex]));
    g_pendingCombatSpeedTarget = targetValue;
    InterlockedExchange(&g_pendingCombatSpeedSteps, 0);
    InterlockedExchange(&g_hasPendingCombatSpeedTarget, 1);
}

static void QueueCombatSpeedAdjustment(int direction)
{
    int deltaSteps;

    deltaSteps = BuildCombatSpeedDeltaFromDirection(direction);

    if (deltaSteps != 0)
    {
        InterlockedExchange(&g_hasPendingCombatSpeedTarget, 0);
        InterlockedExchangeAdd(&g_pendingCombatSpeedSteps, (LONG)deltaSteps);
    }
}

static void ConsumeQueuedCombatSpeedAdjustments(void* combatOwner)
{
    LONG pendingSteps;
    LONG hasPendingTarget;
    double currentValue;
    double nextValue;

    if (!combatOwner)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_isApplyingHotkeyChange, 1, 0) != 0)
    {
        return;
    }

    hasPendingTarget = InterlockedExchange(&g_hasPendingCombatSpeedTarget, 0);
    pendingSteps = InterlockedExchange(&g_pendingCombatSpeedSteps, 0);

    if (!hasPendingTarget && pendingSteps == 0)
    {
        InterlockedExchange(&g_isApplyingHotkeyChange, 0);
        return;
    }

    currentValue = ReadConfiguredCombatSpeed();

    if (currentValue < g_combatSpeedMin)
    {
        currentValue = g_combatSpeedMin;
    }

    if (currentValue > g_combatSpeedMax)
    {
        currentValue = g_combatSpeedMax;
    }

    currentValue = SnapCombatSpeedToStep(currentValue);

    if (hasPendingTarget)
    {
        nextValue = ClampCombatSpeed(SnapCombatSpeedToStep(g_pendingCombatSpeedTarget));
    }
    else
    {
        nextValue = currentValue + ((double)pendingSteps * g_combatSpeedStep);
        nextValue = ClampCombatSpeed(SnapCombatSpeedToStep(nextValue));
    }

    if (fabs(nextValue - currentValue) >= 0.0001)
    {
        if (WriteConfiguredCombatSpeed(nextValue))
        {
            ApplyConfiguredCombatSpeedToOwner(combatOwner);

            if (g_lastCombatHudOwner)
            {
                UpdateCombatHudSpeedText((void*)g_lastCombatHudOwner);
            }
        }
    }

    InterlockedExchange(&g_isApplyingHotkeyChange, 0);
}

static int IsDecreasePressed(void)
{
    return IsHotkeyBindingPressed(&g_decreaseHotkey);
}


static int IsIncreasePressed(void)
{
    return IsHotkeyBindingPressed(&g_increaseHotkey);
}


static int IsResetPressed(void)
{
    return IsHotkeyBindingPressed(&g_resetHotkey);
}


static int IsPresetPressed(uint32_t presetIndex)
{
    if (presetIndex >= DEFAULT_PRESET_SPEED_COUNT)
    {
        return 0;
    }

    return IsHotkeyBindingPressed(&g_presetHotkeys[presetIndex]);
}


static DWORD WINAPI HotkeyThreadProc(LPVOID parameter)
{
    HANDLE stopEvent;
    int previousMinusPressed;
    int previousPlusPressed;
    int previousResetPressed;
    int previousPresetPressed[DEFAULT_PRESET_SPEED_COUNT];
    uint32_t presetIndex;

    stopEvent = (HANDLE)parameter;
    previousMinusPressed = 0;
    previousPlusPressed = 0;
    previousResetPressed = 0;

    for (presetIndex = 0U; presetIndex < DEFAULT_PRESET_SPEED_COUNT; ++presetIndex)
    {
        previousPresetPressed[presetIndex] = 0;
    }

    while (WaitForSingleObject(stopEvent, 16U) == WAIT_TIMEOUT)
    {
        int minusPressed;
        int plusPressed;
        int resetPressed;

        minusPressed = IsDecreasePressed();
        plusPressed = IsIncreasePressed();
        resetPressed = IsResetPressed();

        if (minusPressed && !previousMinusPressed)
        {
            QueueCombatSpeedAdjustment(-1);
        }

        if (plusPressed && !previousPlusPressed)
        {
            QueueCombatSpeedAdjustment(+1);
        }

        if (resetPressed && !previousResetPressed)
        {
            QueueCombatSpeedReset();
        }

        for (presetIndex = 0U; presetIndex < DEFAULT_PRESET_SPEED_COUNT; ++presetIndex)
        {
            int presetPressed;

            presetPressed = IsPresetPressed(presetIndex);

            if (presetPressed && !previousPresetPressed[presetIndex])
            {
                QueueCombatSpeedPreset(presetIndex);
            }

            previousPresetPressed[presetIndex] = presetPressed;
        }

        previousMinusPressed = minusPressed;
        previousPlusPressed = plusPressed;
        previousResetPressed = resetPressed;
    }

    return 0U;
}

static void* __fastcall HookRegisterSelector(void* panel, NarrowString* key, NarrowString* title, OptionVector* options, int enabled_flag, void* callback)
{
    OptionVector replacementOptions;
    OptionVector* effectiveOptions;

    memset(&replacementOptions, 0, sizeof(replacementOptions));
    effectiveOptions = options;

    if (StringEqualsLiteral(key, COMBAT_SPEED_KEY))
    {
        if (BuildCombatSpeedOptions(&replacementOptions))
        {
            effectiveOptions = &replacementOptions;
        }
    }

    if (!g_origRegisterSelector)
    {
        return NULL;
    }

    return g_origRegisterSelector(panel, key, title, effectiveOptions, enabled_flag, callback);
}

static void __fastcall HookRefreshCombatSpeed(void* combatOwner)
{
    InterlockedExchangePointer((PVOID volatile*)&g_lastCombatSpeedOwner, combatOwner);
    StartHotkeyThread();

    if (g_origRefreshCombatSpeed)
    {
        g_origRefreshCombatSpeed(combatOwner);
    }

    ConsumeQueuedCombatSpeedAdjustments(combatOwner);
}

static void __fastcall HookRefreshCombatHud(void* hudOwner)
{
    InterlockedExchangePointer((PVOID volatile*)&g_lastCombatHudOwner, hudOwner);

    if (g_origRefreshCombatHud)
    {
        g_origRefreshCombatHud(hudOwner);
    }

    UpdateCombatHudSpeedText(hudOwner);
}

static void StartHotkeyThread(void)
{
    if (InterlockedCompareExchange(&g_hotkeyThreadStarted, 1, 0) != 0)
    {
        return;
    }

    g_hotkeyStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!g_hotkeyStopEvent)
    {
        return;
    }

    g_hotkeyThread = CreateThread(NULL, 0U, HotkeyThreadProc, g_hotkeyStopEvent, 0U, NULL);

    if (!g_hotkeyThread)
    {
        CloseHandle(g_hotkeyStopEvent);
        g_hotkeyStopEvent = NULL;
        InterlockedExchange(&g_hotkeyThreadStarted, 0);
    }
}

static void StopHotkeyThread(void)
{
    if (g_hotkeyStopEvent)
    {
        SetEvent(g_hotkeyStopEvent);
    }

    if (g_hotkeyThread)
    {
        WaitForSingleObject(g_hotkeyThread, 1000U);
        CloseHandle(g_hotkeyThread);
        g_hotkeyThread = NULL;
    }

    if (g_hotkeyStopEvent)
    {
        CloseHandle(g_hotkeyStopEvent);
        g_hotkeyStopEvent = NULL;
    }

    InterlockedExchange(&g_hotkeyThreadStarted, 0);
}

static void Initialize(void)
{
    void* selectorTrampoline;
    void* refreshTrampoline;
    void* refreshHudTrampoline;

    if (!MJ_Resolve(&g_mj))
    {
        return;
    }

    selectorTrampoline = NULL;
    refreshTrampoline = NULL;
    refreshHudTrampoline = NULL;

    ResolveGameAllocator();
    ResolveRuntimeFunctions();
    LoadCombatSpeedConfig();

    if (g_mj.InstallHook(RVA_REGISTER_SELECTOR, 15U, (void*)HookRegisterSelector, &selectorTrampoline, 20U, MOD_NAME))
    {
        g_origRegisterSelector = (fn_register_selector)selectorTrampoline;
    }

    /*
    * 0x8D61A0 does not have a safe 15-byte boundary... The first whole-instruction boundary past 15 bytes is 21 bytes...
    */
    if (g_mj.InstallHook(RVA_REFRESH_COMBAT_SPEED, 21U, (void*)HookRefreshCombatSpeed, &refreshTrampoline, 32U, MOD_NAME))
    {
        g_origRefreshCombatSpeed = (fn_refresh_combat_speed)refreshTrampoline;
    }
    else if (g_mj.GetGameBase)
    {
        g_origRefreshCombatSpeed = (fn_refresh_combat_speed)(g_mj.GetGameBase() + (UINT_PTR)RVA_REFRESH_COMBAT_SPEED);
    }

    if (g_mj.InstallHook(RVA_REFRESH_COMBAT_HUD, 16U, (void*)HookRefreshCombatHud, &refreshHudTrampoline, 20U, MOD_NAME))
    {
        g_origRefreshCombatHud = (fn_refresh_combat_hud)refreshHudTrampoline;
    }
    else if (g_mj.GetGameBase)
    {
        g_origRefreshCombatHud = (fn_refresh_combat_hud)(g_mj.GetGameBase() + (UINT_PTR)RVA_REFRESH_COMBAT_HUD);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        if (MJ_Resolve(&g_mj))
        {
            g_mj.Log(MOD_NAME, "Loading!");
        }

        Initialize();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        StopHotkeyThread();

        if (MJ_Resolve(&g_mj))
        {
            g_mj.Log(MOD_NAME, "Unloading!");
        }
    }

    return true;
}