#pragma once

#include <algorithm>

namespace mm::window
{
/** Window size in logical pixels. */
struct Size
{
    int width = 0;
    int height = 0;

    friend constexpr bool operator== (const Size&, const Size&) = default;
};

inline constexpr Size minimumSize { 480, 300 };
inline constexpr Size maximumSize { 16384, 16384 };
inline constexpr Size size100 { 1280, 800 }; // reference size, 100 percent
inline constexpr Size size120 { 1536, 960 }; // 120 percent

enum class SizePreset
{
    percent100,
    percent120,
    custom
};

constexpr SizePreset presetFor (Size s) noexcept
{
    if (s == size100)
        return SizePreset::percent100;

    if (s == size120)
        return SizePreset::percent120;

    return SizePreset::custom;
}

/** Size button: 100 percent goes to 120 percent; 120 percent or any custom
    size goes to 100 percent (spec sections 8 and 12). */
constexpr Size sizeAfterButtonClick (Size current) noexcept
{
    return current == size100 ? size120 : size100;
}

/** Limits a stored size to the visible work area and the allowed range.
    A work area with a non-positive dimension is treated as unknown. */
constexpr Size clampToWorkArea (Size requested, Size workArea) noexcept
{
    int w = requested.width;
    int h = requested.height;

    if (workArea.width > 0)
        w = std::min (w, workArea.width);

    if (workArea.height > 0)
        h = std::min (h, workArea.height);

    w = std::clamp (w, minimumSize.width, maximumSize.width);
    h = std::clamp (h, minimumSize.height, maximumSize.height);
    return { w, h };
}

/** Uniform scale relative to the 100 percent reference size. */
constexpr float scaleFactor (Size s) noexcept
{
    return std::min (static_cast<float> (s.width) / static_cast<float> (size100.width),
                     static_cast<float> (s.height) / static_cast<float> (size100.height));
}

constexpr const char* presetName (SizePreset p) noexcept
{
    switch (p)
    {
        case SizePreset::percent100: return "100%";
        case SizePreset::percent120: return "120%";
        case SizePreset::custom: break;
    }

    return "custom";
}
} // namespace mm::window
