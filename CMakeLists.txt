cmake_minimum_required(VERSION 3.24)
project(CombatSpeedPlus LANGUAGES C)

add_library(CombatSpeedPlus SHARED
  src/CombatSpeedPlus.c
)

target_compile_features(CombatSpeedPlus PRIVATE c_std_11)

target_compile_definitions(CombatSpeedPlus PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(CombatSpeedPlus PRIVATE
  user32
)

set_target_properties(CombatSpeedPlus PROPERTIES
  OUTPUT_NAME "CombatSpeedPlus"
)
