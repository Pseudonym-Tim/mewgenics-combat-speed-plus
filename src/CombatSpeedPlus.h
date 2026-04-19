#ifndef COMBAT_SPEED_PLUS_H
#define COMBAT_SPEED_PLUS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/*
 * RVA offsets...
 *
 * 0x96CB80 is the selector registration path used by the game settings menu,
 * we hook it and replace the options vector for the "combat_speed" row...
 *
 * Notes:
 * - combat_speed settings-row construction: ~0x28DF37..0x28E073
 * - runtime float readback of combat_speed: ~0x8D61DD..0x8D623B
 * - option vector copy constructor: 0x2978F0/0x2979E0
 * - option vector destructor: 0x297AD0
 * - game allocator: 0x052590
 *
 * SelectorWidget::Option is 0x40 bytes total:
 *   +0x00 NarrowString value
 *   +0x20 WideString   label
 */
#define RVA_REGISTER_SELECTOR 0x96CB80
#define RVA_GAME_ALLOCATOR 0x052590

/*
 * 32 bytes...
 * Small string capacity threshold: 15 chars...
 */
typedef struct NarrowString
{
    union
    {
        char* heap_ptr;
        char inline_buf[16];
    } storage;
    uint64_t size;
    uint64_t capacity;
} NarrowString;

/*
 * Game wide string type used for selector labels...
 * 32 bytes...
 * Small string capacity threshold: 7 wchar_t code units...
 * (The game copies heap-backed labels using (len * 2) + 2 bytes)...
 */
typedef struct WideString
{
    union
    {
        wchar_t* heap_ptr;
        wchar_t inline_buf[8];
    } storage;
    uint64_t size;
    uint64_t capacity;
} WideString;

typedef struct SelectorOption
{
    NarrowString value;
    WideString label;
} SelectorOption;

typedef struct OptionVector
{
    SelectorOption* begin;
    SelectorOption* end;
    SelectorOption* capacity_end;
} OptionVector;

typedef void* (__cdecl* fn_game_allocate)(size_t size);

typedef void* (__fastcall* fn_register_selector)(void* panel, NarrowString* key, NarrowString* title, OptionVector* options, int enabled_flag, void* callback);

#endif