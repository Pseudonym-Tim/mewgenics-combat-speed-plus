/*
* Combat Speed+
*
* Hooks the game's "combat_speed" selector registration and replaces
* the default option list with custom combat speed values ranging
* from min to max in specific increments...
*
* Outline of what we do:
* - Selector hook interception...
* - Game allocator for option storage...
* - Inline-only string construction to avoid heap ownership issues...
*/

#include "CombatSpeedPlus.h"
#include "mewjector.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define MOD_NAME "Combat Speed+"

static const bool ENABLE_DEBUG_LOGS = false;

#define COMBAT_SPEED_KEY "combat_speed"
#define DEFAULT_COMBAT_SPEED_MIN 0.25
#define DEFAULT_COMBAT_SPEED_MAX 8.00
#define DEFAULT_COMBAT_SPEED_STEP 0.25
#define MAX_COMBAT_SPEED_COUNT 256U

static MewjectorAPI g_mj;
static fn_register_selector g_origRegisterSelector = NULL;
static fn_game_allocate g_gameAllocate = NULL;
static double g_combatSpeedMin = DEFAULT_COMBAT_SPEED_MIN;
static double g_combatSpeedMax = DEFAULT_COMBAT_SPEED_MAX;
static double g_combatSpeedStep = DEFAULT_COMBAT_SPEED_STEP;
static uint32_t g_combatSpeedCount = 32U;
static wchar_t g_configPathW[MAX_PATH] = { 0 };
static HMODULE g_hModule = NULL;

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

    Log("CombatSpeed config loaded: min=%.2f max=%.2f step=%.2f count=%u source=%s", g_combatSpeedMin, g_combatSpeedMax, g_combatSpeedStep, g_combatSpeedCount, g_configPathW[0] ? "ini" : "defaults");
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
    Log("Resolved game allocator at RVA 0x%X -> %p", RVA_GAME_ALLOCATOR, (void*)g_gameAllocate);
    return 1;
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
        Log("FAILED to resolve game allocator");
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

static void* __fastcall HookRegisterSelector(void* panel, NarrowString* key, NarrowString* title, OptionVector* options, int enabled_flag, void* callback)
{
    OptionVector replacementOptions;
    OptionVector* effectiveOptions;

    memset(&replacementOptions, 0, sizeof(replacementOptions));
    effectiveOptions = options;

    if (StringEqualsLiteral(key, COMBAT_SPEED_KEY))
    {
        /* 
        * If replacement generation fails, fall back to original selector options
        * rather than breaking the settings UI...
        */
        if (BuildCombatSpeedOptions(&replacementOptions))
        {
            effectiveOptions = &replacementOptions;
            Log("Replacing combat_speed options with %.2fx..%.2fx in %.2fx steps using narrow values + wide labels", g_combatSpeedMin, g_combatSpeedMax, g_combatSpeedStep);
        }
        else
        {
            Log("FAILED to build replacement combat_speed options, leaving original options in place!");
        }
    }

    if (!g_origRegisterSelector)
    {
        return NULL;
    }

    return g_origRegisterSelector(panel, key, title, effectiveOptions, enabled_flag, callback);
}

static void Initialize(void)
{
    void* trampoline;

    if (!MJ_Resolve(&g_mj))
    {
        return;
    }

    trampoline = NULL;

    ResolveGameAllocator();
    LoadCombatSpeedConfig();

    /*
    * MAGIC NUMBERS!!!! 
    * (15-byte overwrite: Minimum instruction span required for detour...)
    * (20-byte trampoline: Preserves displaced instructions before returning to original code...)
    */
    if (g_mj.InstallHook(RVA_REGISTER_SELECTOR, 15, (void*)HookRegisterSelector, &trampoline, 20, MOD_NAME))
    {
        g_origRegisterSelector = (fn_register_selector)trampoline;
        Log("Hook installed at RVA 0x%X", RVA_REGISTER_SELECTOR);
    }
    else
    {
        Log("FAILED to install hook at RVA 0x%X", RVA_REGISTER_SELECTOR);
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
        if (MJ_Resolve(&g_mj))
        {
            g_mj.Log(MOD_NAME, "Unloading!");
        }
    }

    return true;
}