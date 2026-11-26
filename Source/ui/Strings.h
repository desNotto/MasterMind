#pragma once

// String table for user-visible text (spec section 8). English is the
// default; a Russian table is added in M7.
namespace mm::ui::strings
{
inline constexpr const char* placeholderTitle = "Placeholder window";
inline constexpr const char* placeholderBody = "The real interface arrives in milestone M1. "
                                               "Audio passes through unchanged.";
inline constexpr const char* windowSize = "Window";
inline constexpr const char* sizeCustom = "Custom";
inline constexpr const char* sizeButtonTooltip = "Switch the window between 100% (1280 x 800) and 120% (1536 x 960)";
inline constexpr const char* cpu = "CPU";
inline constexpr const char* latency = "Latency";
inline constexpr const char* samples = "samples";
inline constexpr const char* ms = "ms";
} // namespace mm::ui::strings
