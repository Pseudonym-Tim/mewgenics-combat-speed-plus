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
#include <wchar.h>

#define MOD_NAME "Combat Speed+"

static const bool ENABLE_DEBUG_LOGS = true;

#define COMBAT_SPEED_KEY "combat_speed"
#define COMBAT_SPEED_MIN 0.25
#define COMBAT_SPEED_MAX 8.00
#define COMBAT_SPEED_STEP 0.25
#define COMBAT_SPEED_COUNT 32

static MewjectorAPI g_mj;
static fn_register_selector g_origRegisterSelector = NULL;
static fn_game_allocate g_gameAllocate = NULL;

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

    entries = (SelectorOption*)g_gameAllocate(sizeof(SelectorOption) * COMBAT_SPEED_COUNT);
    
    if (!entries)
    {
        return 0;
    }

    memset(entries, 0, sizeof(SelectorOption) * COMBAT_SPEED_COUNT);

    for (index = 0U; index < COMBAT_SPEED_COUNT; ++index)
    {
        char valueText[16];
        wchar_t labelText[16];
        double value;

        /* (8.00 - 0.25) / 0.25 + 1 = 32 entries */
        value = COMBAT_SPEED_MIN + ((double)index * COMBAT_SPEED_STEP);

        FormatValueString(value, valueText, sizeof(valueText));
        FormatLabelString(value, labelText, sizeof(labelText));

        InitSmallNarrowString(&entries[index].value, valueText);
        InitSmallWideString(&entries[index].label, labelText);
    }

    outVector->begin = entries;
    outVector->end = entries + COMBAT_SPEED_COUNT;
    outVector->capacity_end = entries + COMBAT_SPEED_COUNT;
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
            Log("Replacing combat_speed options with %.2fx..%.2fx in %.2fx steps using narrow values + wide labels", COMBAT_SPEED_MIN, COMBAT_SPEED_MAX, COMBAT_SPEED_STEP);
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

    return TRUE;
}