#ifndef COMBAT_SPEED_PLUS_H
#define COMBAT_SPEED_PLUS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define RVA_REGISTER_SELECTOR        0x96CB80 // Settings-menu selector registration path...
#define RVA_GAME_ALLOCATOR           0x052590 // Game allocator used for selector option/vector storage...
#define RVA_SETTINGS_SET_STRING      0x9C1080 // Generic settings write helper...
#define RVA_SETTINGS_GET_FLOAT       0x9C1560 // Generic settings float read helper...
#define RVA_REFRESH_COMBAT_SPEED     0x8D61A0 // Live combat-speed refresh path...
#define RVA_REFRESH_COMBAT_HUD       0x80E390 // Combat HUD refresh/update...
#define RVA_FIND_UI_CHILD            0x059B70 // UI child lookup helper...
#define RVA_INIT_NARROW_STRING       0x052640 // Constructs a NarrowString...
#define RVA_STRING_DESTRUCTOR        0x0522D0 // Destroys a NarrowString and frees heap storage...
#define RVA_SET_TEXT_ELEMENT_STRING  0x97E390 // Writes a NarrowString into a text UI element...
#define RVA_SETTINGS_SINGLETON       0x13B1A90 // Global settings-manager singleton pointer...
#define SETTINGS_ROOT_OFFSET         0x3B0 // Offset from the settings-manager singleton to the settings root...

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