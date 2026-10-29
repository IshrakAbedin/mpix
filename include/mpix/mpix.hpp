#pragma once

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

namespace mpix
{

/// Pixel layout of an image; the value is the number of channels.
enum class ColorFormat
{
    L = 1,   // Grayscale without alpha
    LA = 2,  // Grayscale with alpha
    RGB = 3, // RGB without alpha
    RGBA = 4 // RGB wit alpha
};

/// Converts ColorFormat to equivalent stbi pixel layout for resizing
[[nodiscard]] constexpr stbir_pixel_layout color_format_to_pixel_layout(
    ColorFormat f
) noexcept
{
    switch (f)
    {
        case ColorFormat::L:
            return STBIR_1CHANNEL;
        case ColorFormat::LA:
            return STBIR_RA;
        case ColorFormat::RGB:
            return STBIR_RGB;
        case ColorFormat::RGBA:
            return STBIR_RGBA;
    }
    return STBIR_1CHANNEL;
}

// Base template class for Color, intentionally undefined.
// Each specialization is a tightly packed pixel of 8-bit channels.
template <ColorFormat F> struct Color;

template <> struct Color<ColorFormat::L>
{
    uint8_t L{0};
};

template <> struct Color<ColorFormat::LA>
{
    uint8_t L{0};
    uint8_t A{255};
};

template <> struct Color<ColorFormat::RGB>
{
    uint8_t R{0};
    uint8_t G{0};
    uint8_t B{0};
};

template <> struct Color<ColorFormat::RGBA>
{
    uint8_t R{0};
    uint8_t G{0};
    uint8_t B{0};
    uint8_t A{255};
};

using ColorL = Color<ColorFormat::L>;
using ColorLA = Color<ColorFormat::LA>;
using ColorRGB = Color<ColorFormat::RGB>;
using ColorRGBA = Color<ColorFormat::RGBA>;

/// File format used by Image::Write.
enum class ImageFormat
{
    BMP,
    JPEG,
    PNG,
    TGA
};

/// An 8-bit image with the channel layout F, stored row-major from the
/// top-left. Errors are reported by exceptions.
template <ColorFormat F = ColorFormat::RGB> class Image
{
  public:
    using PixelFormat = Color<F>;

  private:
    std::vector<PixelFormat> _buffer;
    size_t _height{0};
    size_t _width{0};
    size_t _channel{static_cast<size_t>(F)};

  private:
    // Placeholder ctor for deferred construction
    Image(size_t height, size_t width, bool _) : _height{height}, _width{width}
    {
        _buffer.resize(_height * _width);
    }

  public:
    /// Creates a height x width image filled with bg_color (default: black,
    /// opaque for formats with alpha).
    Image(size_t height, size_t width, PixelFormat bg_color = PixelFormat{})
        : _height{height}, _width{width}, _buffer{height * width, bg_color}
    {
    }

    /// Creates an image by copying height * width * channels bytes of tightly
    /// packed, row-major pixel data. The buffer must be large enough.
    Image(size_t height, size_t width, const uint8_t* raw_data)
        : _height{height}, _width{width}
    {
        _buffer.resize(_height * _width);
        std::memcpy(_buffer.data(), raw_data, _height * _width * _channel);
    }

    /// Number of pixel rows.
    [[nodiscard]] size_t Height() const noexcept { return _height; }

    /// Number of pixel columns.
    [[nodiscard]] size_t Width() const noexcept { return _width; }

    /// Number of channels per pixel (1-4).
    [[nodiscard]] size_t Channel() const noexcept { return _channel; }

    /// Bounds-checked read access. Throws std::out_of_range.
    [[nodiscard]] PixelFormat Pixel(size_t row, size_t col) const
    {
        if (row >= _height)
        {
            throw std::out_of_range(
                std::format(
                    "Row value {} is greater than height {}", row, _height
                )
            );
        }
        if (col >= _width)
        {
            throw std::out_of_range(
                std::format(
                    "Col value {} is greater than width {}", col, _width
                )
            );
        }
        return _buffer[row * _width + col];
    }

    /// Bounds-checked mutable access. Throws std::out_of_range.
    [[nodiscard]] PixelFormat& Pixel(size_t row, size_t col)
    {
        if (row >= _height)
        {
            throw std::out_of_range(
                std::format(
                    "Row value {} is greater than height {}", row, _height
                )
            );
        }
        if (col >= _width)
        {
            throw std::out_of_range(
                std::format(
                    "Col value {} is greater than width {}", col, _width
                )
            );
        }
        return _buffer[row * _width + col];
    }

    /// Same as Pixel(row, col).
    [[nodiscard]] PixelFormat operator()(size_t row, size_t col) const
    {
        return Pixel(row, col);
    }

    /// Same as Pixel(row, col).
    [[nodiscard]] PixelFormat& operator()(size_t row, size_t col)
    {
        return Pixel(row, col);
    }

    /// The ColorFormat of this image.
    [[nodiscard]] constexpr ColorFormat Format() const noexcept { return F; }

    /// Returns a resized copy (sRGB-aware filtering).
    /// Throws std::runtime_error if stb fails to resize.
    [[nodiscard]] Image Resize(size_t target_height, size_t target_width) const
    {
        Image<F> resized{target_height, target_width, true};

        constexpr stbir_pixel_layout layout = color_format_to_pixel_layout(F);

        auto result = stbir_resize_uint8_srgb(
            reinterpret_cast<const uint8_t*>(_buffer.data()),
            static_cast<int>(_width), static_cast<int>(_height), 0,
            reinterpret_cast<uint8_t*>(resized._buffer.data()),
            static_cast<int>(target_width), static_cast<int>(target_height), 0,
            layout
        );

        if (!result)
        {
            throw std::runtime_error("Failed to resize image");
        }

        return resized;
    }

    /// Writes the image to path as format I. `quality` (1-100) applies to
    /// JPEG only. Throws std::runtime_error on failure.
    template <ImageFormat I = ImageFormat::JPEG>
    void Write(const std::filesystem::path& path, int quality = 100) const
    {
        int success = 0;

        if constexpr (I == ImageFormat::BMP)
        {
            success = stbi_write_bmp(
                path.string().c_str(), _width, _height, _channel,
                static_cast<const void*>(_buffer.data())
            );
        }
        else if constexpr (I == ImageFormat::JPEG)
        {
            success = stbi_write_jpg(
                path.string().c_str(), _width, _height, _channel,
                static_cast<const void*>(_buffer.data()), quality
            );
        }
        else if constexpr (I == ImageFormat::PNG)
        {
            success = stbi_write_png(
                path.string().c_str(), _width, _height, _channel,
                static_cast<const void*>(_buffer.data()), 0
            );
        }
        else if constexpr (I == ImageFormat::TGA)
        {
            success = stbi_write_tga(
                path.string().c_str(), _width, _height, _channel,
                static_cast<const void*>(_buffer.data())
            );
        }
        else
        {
            throw std::runtime_error("Invalid Image Format provided");
        }

        if (!success)
        {
            throw std::runtime_error(
                std::format("Failed to write the image at {}", path.string())
            );
        };
    }

    /// Returns a copy flipped top-to-bottom (rows reversed).
    [[nodiscard]] Image VFlip() const
    {
        Image flipped = *this;
        if (flipped._height == 1) return flipped;
        for (size_t r = 0; r < flipped._height / 2; r++)
        {
            auto r1_start = flipped._buffer.begin() + r * flipped._width;
            auto r1_end = r1_start + flipped._width;
            auto r2_start = flipped._buffer.begin() +
                            (flipped._height - 1 - r) * flipped._width;

            std::swap_ranges(r1_start, r1_end, r2_start);
        }
        return flipped;
    }
};

/// Any image type that read_image_from_file can return.
using ImageVariant = std::variant<
    Image<ColorFormat::L>,     // Only grayscale image
    Image<ColorFormat::LA>,    // Grayscale with Alpha
    Image<ColorFormat::RGB>,   // Only RGB image
    Image<ColorFormat::RGBA>>; // RGB image with Alpha

/// Loads an image file, keeping the channel count stored in the file.
/// Throws std::runtime_error if the file cannot be read or decoded.
inline ImageVariant read_image_from_file(const std::filesystem::path& path)
{
    int w, h, c;

    uint8_t* data = stbi_load(path.string().c_str(), &w, &h, &c, 0);

    if (!data)
    {
        throw std::runtime_error(
            std::format("Failed to load image from {}", path.string())
        );
    }

    if (c == 1)
    {
        Image<ColorFormat::L> img{
            static_cast<size_t>(h), static_cast<size_t>(w), data
        };
        stbi_image_free(data);
        return img;
    }
    else if (c == 2)
    {
        Image<ColorFormat::LA> img{
            static_cast<size_t>(h), static_cast<size_t>(w), data
        };
        stbi_image_free(data);
        return img;
    }
    else if (c == 3)
    {
        Image<ColorFormat::RGB> img{
            static_cast<size_t>(h), static_cast<size_t>(w), data
        };
        stbi_image_free(data);
        return img;
    }
    else if (c == 4)
    {
        Image<ColorFormat::RGBA> img{
            static_cast<size_t>(h), static_cast<size_t>(w), data
        };
        stbi_image_free(data);
        return img;
    }
    else
    {
        stbi_image_free(data);
        throw std::runtime_error(
            std::format(
                "Unknown image channel depth of {}, must be between 1 to 4", c
            )
        );
    }
}

} // namespace mpix
