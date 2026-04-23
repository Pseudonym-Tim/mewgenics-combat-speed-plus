#ifndef COMBAT_SPEED_PLUS_H
#define COMBAT_SPEED_PLUS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/*
 * RVA offsets...
 *
 * 0x96CB80 -> selector registration path used by the settings menu...
 * 0x052590 -> game allocator used for selector option storage...
 * 0x9C1080 -> generic settings string write helper...
 * 0x9C1560 -> generic settings float read helper...
 * 0x8D61A0 -> Live combat-speed refresh path that pushes the configured value into the active combat object...
 * 0x13B1A90 -> Global settings-manager singleton pointer...
 *
 * Notes:
 * - 0x299CE0 is the menu callback wrapper for combat_speed, it copies the
 *   selected string and forwards the write through 0x9C1080...
 * - 0x8D61DD - 0x8D623B builds the "combat_speed" key inline and refreshes
 *   the active combat object from settings outside the menu code path...
 * - 0x28DF37 - 0x28E073 is the settings-row construction path...
 */
#define RVA_REGISTER_SELECTOR        0x96CB80
#define RVA_GAME_ALLOCATOR           0x052590
#define RVA_SETTINGS_SET_STRING      0x9C1080
#define RVA_SETTINGS_GET_FLOAT       0x9C1560
#define RVA_REFRESH_COMBAT_SPEED     0x8D61A0
#define RVA_REFRESH_COMBAT_HUD       0x80E390
#define RVA_FIND_UI_CHILD            0x059B70
#define RVA_INIT_NARROW_STRING       0x052640
#define RVA_STRING_DESTRUCTOR        0x0522D0
#define RVA_SET_TEXT_ELEMENT_STRING  0x97E390
#define RVA_SETTINGS_SINGLETON       0x13B1A90
#define SETTINGS_ROOT_OFFSET         0x3B0

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
typedef void* (__fastcall* fn_settings_set_string)(void* settings_root, NarrowString* key, NarrowString* value);
typedef float (__fastcall* fn_settings_get_float)(void* settings_root, NarrowString* key, float default_value);
typedef void (__fastcall* fn_refresh_combat_speed)(void* combat_owner);
typedef void (__fastcall* fn_refresh_combat_hud)(void* hud_owner);
typedef void* (__fastcall* fn_find_ui_child)(void* ui_root, NarrowString* name);
typedef NarrowString* (__fastcall* fn_init_narrow_string_from_literal)(NarrowString* out_string, const char* text);
typedef void (__fastcall* fn_string_destructor)(NarrowString* value);
typedef void* (__fastcall* fn_set_text_element_string)(void* text_element, NarrowString* text, uint8_t treat_as_plain_text, uint8_t commit_immediately);

#endif