// Catch2 tests for mpix.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <mpix/mpix.hpp>

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using mpix::ColorFormat;
using mpix::ColorL;
using mpix::ColorLA;
using mpix::ColorRGB;
using mpix::ColorRGBA;
using mpix::Image;
using mpix::ImageFormat;

// ---------------------------------------------------------------------------
// Compile-time guarantees the library silently relies on
// (memcpy / reinterpret_cast / stb all assume tightly packed pixels)
// ---------------------------------------------------------------------------
static_assert(sizeof(ColorL) == 1, "ColorL must be tightly packed");
static_assert(sizeof(ColorLA) == 2, "ColorLA must be tightly packed");
static_assert(sizeof(ColorRGB) == 3, "ColorRGB must be tightly packed");
static_assert(sizeof(ColorRGBA) == 4, "ColorRGBA must be tightly packed");
static_assert(std::is_trivially_copyable_v<ColorL>);
static_assert(std::is_trivially_copyable_v<ColorLA>);
static_assert(std::is_trivially_copyable_v<ColorRGB>);
static_assert(std::is_trivially_copyable_v<ColorRGBA>);
static_assert(std::is_standard_layout_v<ColorL>);
static_assert(std::is_standard_layout_v<ColorLA>);
static_assert(std::is_standard_layout_v<ColorRGB>);
static_assert(std::is_standard_layout_v<ColorRGBA>);
static_assert(static_cast<int>(ColorFormat::L) == 1);
static_assert(static_cast<int>(ColorFormat::LA) == 2);
static_assert(static_cast<int>(ColorFormat::RGB) == 3);
static_assert(static_cast<int>(ColorFormat::RGBA) == 4);
static_assert(std::is_same_v<Image<ColorFormat::L>::PixelFormat, ColorL>);
static_assert(std::is_same_v<Image<ColorFormat::LA>::PixelFormat, ColorLA>);
static_assert(std::is_same_v<Image<ColorFormat::RGB>::PixelFormat, ColorRGB>);
static_assert(std::is_same_v<Image<ColorFormat::RGBA>::PixelFormat, ColorRGBA>);
static_assert(
    std::is_same_v<Image<>::PixelFormat, ColorRGB>,
    "default template argument is RGB"
);
static_assert(std::is_copy_constructible_v<Image<ColorFormat::L>>);
static_assert(std::is_move_constructible_v<Image<ColorFormat::LA>>);
static_assert(std::is_copy_constructible_v<Image<ColorFormat::RGB>>);
static_assert(std::is_move_constructible_v<Image<ColorFormat::RGBA>>);

namespace
{

// ----------------------------- helpers -------------------------------------

// RAII temp file path that is removed on scope exit.
class TempFile
{
  public:
    explicit TempFile(const std::string& ext)
    {
        static unsigned counter = 0;
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        _path =
            fs::temp_directory_path() / ("mimg_test_" + std::to_string(stamp) +
                                         "_" + std::to_string(counter++) + ext);
    }
    ~TempFile()
    {
        std::error_code ec;
        fs::remove(_path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const fs::path& path() const { return _path; }

  private:
    fs::path _path;
};

bool same(const ColorL& a, const ColorL& b)
{
    return a.L == b.L;
}
bool same(const ColorLA& a, const ColorLA& b)
{
    return a.L == b.L && a.A == b.A;
}
bool same(const ColorRGB& a, const ColorRGB& b)
{
    return a.R == b.R && a.G == b.G && a.B == b.B;
}
bool same(const ColorRGBA& a, const ColorRGBA& b)
{
    return a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A;
}

bool near(const ColorL& a, const ColorL& b, int tol)
{
    return std::abs(a.L - b.L) <= tol;
}
bool near(const ColorLA& a, const ColorLA& b, int tol)
{
    return std::abs(a.L - b.L) <= tol && std::abs(a.A - b.A) <= tol;
}
bool near(const ColorRGB& a, const ColorRGB& b, int tol)
{
    return std::abs(a.R - b.R) <= tol && std::abs(a.G - b.G) <= tol &&
           std::abs(a.B - b.B) <= tol;
}
bool near(const ColorRGBA& a, const ColorRGBA& b, int tol)
{
    return std::abs(a.R - b.R) <= tol && std::abs(a.G - b.G) <= tol &&
           std::abs(a.B - b.B) <= tol && std::abs(a.A - b.A) <= tol;
}

template <ColorFormat F> bool images_equal(const Image<F>& a, const Image<F>& b)
{
    if (a.Height() != b.Height() || a.Width() != b.Width()) return false;
    for (size_t r = 0; r < a.Height(); ++r)
        for (size_t c = 0; c < a.Width(); ++c)
            if (!same(a.Pixel(r, c), b.Pixel(r, c))) return false;
    return true;
}

template <ColorFormat F>
bool images_near(const Image<F>& a, const Image<F>& b, int tol)
{
    if (a.Height() != b.Height() || a.Width() != b.Width()) return false;
    for (size_t r = 0; r < a.Height(); ++r)
        for (size_t c = 0; c < a.Width(); ++c)
            if (!near(a.Pixel(r, c), b.Pixel(r, c), tol)) return false;
    return true;
}

// Deterministic, position-dependent pattern: every pixel (almost) unique, so
// transposed / flipped / shifted data cannot pass by accident.
template <ColorFormat F> Image<F> make_pattern(size_t h, size_t w)
{
    Image<F> img{h, w};
    for (size_t r = 0; r < h; ++r)
    {
        for (size_t c = 0; c < w; ++c)
        {
            auto& p = img.Pixel(r, c);
            if constexpr (F == ColorFormat::L)
            {
                p.L = static_cast<uint8_t>((r * 37 + c * 11 + 1) % 256);
            }
            else if constexpr (F == ColorFormat::LA)
            {
                p.L = static_cast<uint8_t>((r * 37 + c * 11 + 1) % 256);
                p.A = static_cast<uint8_t>((r * 7 + c * 5 + 100) % 256);
            }
            else if constexpr (F == ColorFormat::RGB)
            {
                p.R = static_cast<uint8_t>((r * 37 + c * 11 + 1) % 256);
                p.G = static_cast<uint8_t>((r * 13 + c * 53 + 7) % 256);
                p.B = static_cast<uint8_t>((r * 91 + c * 29 + 3) % 256);
            }
            else if constexpr (F == ColorFormat::RGBA)
            {
                p.R = static_cast<uint8_t>((r * 37 + c * 11 + 1) % 256);
                p.G = static_cast<uint8_t>((r * 13 + c * 53 + 7) % 256);
                p.B = static_cast<uint8_t>((r * 91 + c * 29 + 3) % 256);
                p.A = static_cast<uint8_t>((r * 7 + c * 5 + 100) % 256);
            }
        }
    }
    return img;
}

template <ColorFormat F>
Image<F> make_solid(size_t h, size_t w, typename Image<F>::PixelFormat color)
{
    return Image<F>{h, w, color};
}

// Builds a pixel for any format.
template <ColorFormat F>
typename Image<F>::PixelFormat color(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255
)
{
    if constexpr (F == ColorFormat::L) return {r};
    else if constexpr (F == ColorFormat::LA) return {r, a};
    else if constexpr (F == ColorFormat::RGB) return {r, g, b};
    else return {r, g, b, a};
}

// Pseudo-random noise (LCG) - incompressible, good for file size comparisons.
Image<ColorFormat::RGB> make_noise(size_t h, size_t w)
{
    Image<ColorFormat::RGB> img{h, w};
    uint32_t s = 12345;
    auto next = [&s]
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<uint8_t>(s >> 24);
    };
    for (size_t r = 0; r < h; ++r)
        for (size_t c = 0; c < w; ++c)
            img.Pixel(r, c) = ColorRGB{next(), next(), next()};
    return img;
}

template <ColorFormat F> Image<F> as_image(mpix::ImageVariant&& v)
{
    REQUIRE(std::holds_alternative<Image<F>>(v));
    return std::move(std::get<Image<F>>(v));
}

} // namespace

// ===========================================================================
// Colour structs
// ===========================================================================

TEST_CASE("Colour structs: default member values", "[color]")
{
    constexpr ColorL l{};
    CHECK(l.L == 0);

    constexpr ColorLA la{};
    CHECK(la.L == 0);
    CHECK(la.A == 255); // opaque by default

    constexpr ColorRGB rgb{};
    CHECK(rgb.R == 0);
    CHECK(rgb.G == 0);
    CHECK(rgb.B == 0);

    constexpr ColorRGBA rgba{};
    CHECK(rgba.R == 0);
    CHECK(rgba.G == 0);
    CHECK(rgba.B == 0);
    CHECK(rgba.A == 255); // opaque by default
}

TEST_CASE("Colour structs: partial initialisation", "[color]")
{
    // Omitting alpha yields opaque, not transparent.
    ColorLA la{42};
    CHECK(la.L == 42);
    CHECK(la.A == 255);

    ColorLA la_trans{42, 0};
    CHECK(la_trans.L == 42);
    CHECK(la_trans.A == 0);

    static_assert(sizeof(ColorLA) == 2);
    static_assert(std::is_trivially_copyable_v<ColorLA>);

    ColorRGBA c{10, 20, 30};
    CHECK(c.R == 10);
    CHECK(c.G == 20);
    CHECK(c.B == 30);
    CHECK(c.A == 255);

    ColorRGBA t{10, 20, 30, 0};
    CHECK(t.A == 0);

    // Layout must still be tightly packed despite the non-zero default.
    static_assert(sizeof(ColorRGBA) == 4);
    static_assert(std::is_trivially_copyable_v<ColorRGBA>);
}

// ===========================================================================
// Construction
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image: constructed with dimensions and background colour", "[image][ctor]",
    ((ColorFormat F), F), ColorFormat::L, ColorFormat::LA, ColorFormat::RGB,
    ColorFormat::RGBA
)
{
    using Img = Image<F>;
    using Px = typename Img::PixelFormat;

    SECTION("dimensions and channel count are reported correctly")
    {
        Img img{4, 7};
        REQUIRE(img.Height() == 4);
        REQUIRE(img.Width() == 7);
        REQUIRE(img.Channel() == static_cast<size_t>(F));
    }

    SECTION("default background is black; alpha (if present) is fully opaque")
    {
        Img img{3, 5};
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 5; ++c)
            {
                CHECK(same(img.Pixel(r, c), Px{}));
                if constexpr (F == ColorFormat::L)
                {
                    CHECK(img.Pixel(r, c).L == 0);
                }
                else if constexpr (F == ColorFormat::LA)
                {
                    CHECK(img.Pixel(r, c).L == 0);
                    CHECK(img.Pixel(r, c).A == 255);
                }
                else if constexpr (F == ColorFormat::RGB)
                {
                    CHECK(img.Pixel(r, c).R == 0);
                    CHECK(img.Pixel(r, c).G == 0);
                    CHECK(img.Pixel(r, c).B == 0);
                }
                else if constexpr (F == ColorFormat::RGBA)
                {
                    CHECK(img.Pixel(r, c).R == 0);
                    CHECK(img.Pixel(r, c).G == 0);
                    CHECK(img.Pixel(r, c).B == 0);
                    CHECK(img.Pixel(r, c).A == 255);
                }
            }
    }

    SECTION("explicit alpha 0 background is respected for formats with alpha")
    {
        if constexpr (F == ColorFormat::LA)
        {
            Img img{2, 2, ColorLA{9, 0}};
            for (size_t r = 0; r < 2; ++r)
                for (size_t c = 0; c < 2; ++c)
                    CHECK(same(img.Pixel(r, c), ColorLA{9, 0}));
        }
        else if constexpr (F == ColorFormat::RGBA)
        {
            Img img{2, 2, ColorRGBA{9, 8, 7, 0}};
            for (size_t r = 0; r < 2; ++r)
                for (size_t c = 0; c < 2; ++c)
                    CHECK(same(img.Pixel(r, c), ColorRGBA{9, 8, 7, 0}));
        }
    }

    SECTION("custom background colour fills every pixel")
    {
        Px bg = color<F>(10, 20, 30, 40);
        Img img{6, 2, bg};
        for (size_t r = 0; r < 6; ++r)
            for (size_t c = 0; c < 2; ++c)
                REQUIRE(same(img.Pixel(r, c), bg));
    }

    SECTION("1x1 image")
    {
        Px px = color<F>(1, 2, 3, 4);
        Img img{1, 1, px};
        REQUIRE(img.Height() == 1);
        REQUIRE(img.Width() == 1);
        REQUIRE(same(img.Pixel(0, 0), px));
    }

    SECTION("empty images are valid but have no addressable pixels")
    {
        Img a{0, 0};
        CHECK(a.Height() == 0);
        CHECK(a.Width() == 0);
        CHECK_THROWS_AS(a.Pixel(0, 0), std::out_of_range);

        Img b{0, 5};
        CHECK_THROWS_AS(b.Pixel(0, 0), std::out_of_range);

        Img c{5, 0};
        CHECK_THROWS_AS(c.Pixel(0, 0), std::out_of_range);
    }

    SECTION("large-ish allocation works")
    {
        Px bg = color<F>(9, 9, 9, 9);
        Img img{512, 1024, bg};
        REQUIRE(img.Height() == 512);
        REQUIRE(img.Width() == 1024);
        REQUIRE(same(img.Pixel(511, 1023), bg));
    }
}

TEST_CASE("Image: constructed from raw L data", "[image][ctor][raw]")
{
    const std::vector<uint8_t> raw = {
        1, 2, 3, // row 0
        4, 5, 6  // row 1
    };
    Image<ColorFormat::L> img{2, 3, raw.data()};

    REQUIRE(img.Height() == 2);
    REQUIRE(img.Width() == 3);

    CHECK(same(img.Pixel(0, 0), ColorL{1}));
    CHECK(same(img.Pixel(0, 1), ColorL{2}));
    CHECK(same(img.Pixel(0, 2), ColorL{3}));
    CHECK(same(img.Pixel(1, 0), ColorL{4}));
    CHECK(same(img.Pixel(1, 1), ColorL{5}));
    CHECK(same(img.Pixel(1, 2), ColorL{6}));
}

TEST_CASE("Image: constructed from raw LA data", "[image][ctor][raw]")
{
    const std::vector<uint8_t> raw = {
        1, 2, 3, 4, // row 0
        5, 6, 7, 8  // row 1
    };
    Image<ColorFormat::LA> img{2, 2, raw.data()};

    REQUIRE(img.Height() == 2);
    REQUIRE(img.Width() == 2);

    CHECK(same(img.Pixel(0, 0), ColorLA{1, 2}));
    CHECK(same(img.Pixel(0, 1), ColorLA{3, 4}));
    CHECK(same(img.Pixel(1, 0), ColorLA{5, 6}));
    CHECK(same(img.Pixel(1, 1), ColorLA{7, 8}));
}

TEST_CASE("Image: constructed from raw RGB data", "[image][ctor][raw]")
{
    // 2 rows x 3 cols, row-major, tightly packed
    const std::vector<uint8_t> raw = {
        1,  2,  3,  4,  5,  6,  7,  8,  9,  // row 0
        10, 11, 12, 13, 14, 15, 16, 17, 18, // row 1
    };
    Image<ColorFormat::RGB> img{2, 3, raw.data()};

    REQUIRE(img.Height() == 2);
    REQUIRE(img.Width() == 3);

    CHECK(same(img.Pixel(0, 0), ColorRGB{1, 2, 3}));
    CHECK(same(img.Pixel(0, 1), ColorRGB{4, 5, 6}));
    CHECK(same(img.Pixel(0, 2), ColorRGB{7, 8, 9}));
    CHECK(same(img.Pixel(1, 0), ColorRGB{10, 11, 12}));
    CHECK(same(img.Pixel(1, 1), ColorRGB{13, 14, 15}));
    CHECK(same(img.Pixel(1, 2), ColorRGB{16, 17, 18}));
}

TEST_CASE("Image: constructed from raw RGBA data", "[image][ctor][raw]")
{
    const std::vector<uint8_t> raw = {
        1,  2,  3,  4,  5,  6,  7,  8,  // row 0
        9,  10, 11, 12, 13, 14, 15, 16, // row 1
        17, 18, 19, 20, 21, 22, 23, 24, // row 2
    };
    Image<ColorFormat::RGBA> img{3, 2, raw.data()};

    REQUIRE(img.Height() == 3);
    REQUIRE(img.Width() == 2);
    CHECK(same(img.Pixel(0, 0), ColorRGBA{1, 2, 3, 4}));
    CHECK(same(img.Pixel(0, 1), ColorRGBA{5, 6, 7, 8}));
    CHECK(same(img.Pixel(1, 0), ColorRGBA{9, 10, 11, 12}));
    CHECK(same(img.Pixel(1, 1), ColorRGBA{13, 14, 15, 16}));
    CHECK(same(img.Pixel(2, 0), ColorRGBA{17, 18, 19, 20}));
    CHECK(same(img.Pixel(2, 1), ColorRGBA{21, 22, 23, 24}));
}

TEST_CASE(
    "Image: raw-data constructor deep-copies the source", "[image][ctor][raw]"
)
{
    std::vector<uint8_t> raw = {1, 2, 3, 4, 5, 6};
    Image<ColorFormat::RGB> img{1, 2, raw.data()};

    std::fill(raw.begin(), raw.end(), uint8_t{0xFF});
    raw.clear();
    raw.shrink_to_fit();

    CHECK(same(img.Pixel(0, 0), ColorRGB{1, 2, 3}));
    CHECK(same(img.Pixel(0, 1), ColorRGB{4, 5, 6}));
}

TEST_CASE(
    "Image: raw-data constructor accepts non-const pointers",
    "[image][ctor][raw]"
)
{
    // read_image_from_file passes a plain uint8_t*; make sure overload
    // resolution picks the pointer ctor (not the private bool one).
    uint8_t raw[3] = {7, 8, 9};
    uint8_t* p = raw;
    Image<ColorFormat::RGB> img{1, 1, p};
    CHECK(same(img.Pixel(0, 0), ColorRGB{7, 8, 9}));
}

// ===========================================================================
// Pixel access
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image: pixel access, mutation and bounds checking", "[image][access]",
    ((ColorFormat F), F), ColorFormat::L, ColorFormat::LA, ColorFormat::RGB,
    ColorFormat::RGBA
)
{
    using Img = Image<F>;
    using Px = typename Img::PixelFormat;

    // Deliberately non-square so that row/col or height/width mix-ups fail.
    constexpr size_t H = 3;
    constexpr size_t W = 5;
    Img img = make_pattern<F>(H, W);

    SECTION("every valid coordinate is reachable")
    {
        for (size_t r = 0; r < H; ++r)
            for (size_t c = 0; c < W; ++c)
                REQUIRE_NOTHROW(img.Pixel(r, c));
    }

    SECTION("pixels are addressed row-major, rows are height, cols are width")
    {
        Img marker{H, W};
        marker.Pixel(2, 4) = color<F>(200, 100, 50, 60);
        marker.Pixel(0, 4) = color<F>(1, 1, 1, 1);
        marker.Pixel(2, 0) = color<F>(2, 2, 2, 2);

        CHECK(same(marker.Pixel(2, 4), color<F>(200, 100, 50, 60)));
        CHECK(same(marker.Pixel(0, 4), color<F>(1, 1, 1, 1)));
        CHECK(same(marker.Pixel(2, 0), color<F>(2, 2, 2, 2)));
        // untouched neighbours remain at the default
        CHECK(same(marker.Pixel(1, 4), Px{}));
        CHECK(same(marker.Pixel(2, 3), Px{}));
        CHECK(same(marker.Pixel(0, 0), Px{}));
    }

    SECTION("writing one pixel does not disturb any other pixel")
    {
        const Img before = img;
        img.Pixel(1, 2) = color<F>(255, 254, 253, 252);
        for (size_t r = 0; r < H; ++r)
            for (size_t c = 0; c < W; ++c)
            {
                if (r == 1 && c == 2)
                    CHECK(same(img.Pixel(r, c), color<F>(255, 254, 253, 252)));
                else CHECK(same(img.Pixel(r, c), before.Pixel(r, c)));
            }
    }

    SECTION("individual channels can be modified through the reference")
    {
        if constexpr (F == ColorFormat::L || F == ColorFormat::LA)
        {
            img.Pixel(0, 0).L = 123;
            CHECK(img.Pixel(0, 0).L == 123);
        }
        else
        {
            img.Pixel(0, 0).G = 123;
            CHECK(img.Pixel(0, 0).G == 123);
        }
    }

    SECTION("operator() is equivalent to Pixel()")
    {
        for (size_t r = 0; r < H; ++r)
            for (size_t c = 0; c < W; ++c)
                CHECK(same(img(r, c), img.Pixel(r, c)));

        img(2, 3) = color<F>(5, 6, 7, 8);
        CHECK(same(img.Pixel(2, 3), color<F>(5, 6, 7, 8)));
        if constexpr (F == ColorFormat::L || F == ColorFormat::LA)
        {
            img(2, 3).L = 99;
            CHECK(img.Pixel(2, 3).L == 99);
        }
        else
        {
            img(2, 3).R = 99;
            CHECK(img.Pixel(2, 3).R == 99);
        }
    }

    SECTION("const access returns by value, non-const returns a reference")
    {
        const Img& cimg = img;
        static_assert(std::is_same_v<decltype(cimg.Pixel(0, 0)), Px>);
        static_assert(std::is_same_v<decltype(img.Pixel(0, 0)), Px&>);
        static_assert(std::is_same_v<decltype(cimg(0, 0)), Px>);
        static_assert(std::is_same_v<decltype(img(0, 0)), Px&>);
        CHECK(same(cimg.Pixel(1, 1), img.Pixel(1, 1)));
    }

    SECTION("row out of range throws std::out_of_range")
    {
        CHECK_THROWS_AS(img.Pixel(H, 0), std::out_of_range); // boundary
        CHECK_THROWS_AS(img.Pixel(H + 10, 0), std::out_of_range);
        CHECK_THROWS_AS(img(H, 0), std::out_of_range);
        CHECK_THROWS_AS(std::as_const(img).Pixel(H, 0), std::out_of_range);
        CHECK_THROWS_AS(std::as_const(img)(H, 0), std::out_of_range);
    }

    SECTION("col out of range throws std::out_of_range")
    {
        CHECK_THROWS_AS(img.Pixel(0, W), std::out_of_range); // boundary
        CHECK_THROWS_AS(img.Pixel(0, W + 10), std::out_of_range);
        CHECK_THROWS_AS(img(0, W), std::out_of_range);
        CHECK_THROWS_AS(std::as_const(img).Pixel(0, W), std::out_of_range);
        CHECK_THROWS_AS(std::as_const(img)(0, W), std::out_of_range);
    }

    SECTION("a col value valid only for the *other* axis is rejected")
    {
        // W=5, H=3: (0, 4) is valid, (4, 0) is not.
        CHECK_NOTHROW(img.Pixel(0, W - 1));
        CHECK_THROWS_AS(img.Pixel(W - 1, 0), std::out_of_range);
    }

    SECTION("size_t wrap-around (e.g. -1) is rejected, not UB")
    {
        const size_t huge = static_cast<size_t>(-1);
        CHECK_THROWS_AS(img.Pixel(huge, 0), std::out_of_range);
        CHECK_THROWS_AS(img.Pixel(0, huge), std::out_of_range);
    }

    SECTION("exception messages identify the offending value")
    {
        CHECK_THROWS_WITH(
            img.Pixel(7, 0),
            ContainsSubstring("Row value 7") && ContainsSubstring("height 3")
        );
        CHECK_THROWS_WITH(
            img.Pixel(0, 9),
            ContainsSubstring("Col value 9") && ContainsSubstring("width 5")
        );
        CHECK_THROWS_WITH(
            std::as_const(img).Pixel(7, 0), ContainsSubstring("Row value 7")
        );
        CHECK_THROWS_WITH(
            std::as_const(img).Pixel(0, 9), ContainsSubstring("Col value 9")
        );
    }

    SECTION("row is checked before col when both are invalid")
    {
        CHECK_THROWS_WITH(img.Pixel(H, W), ContainsSubstring("Row value"));
    }
}

// ===========================================================================
// Copy / move semantics
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image: copy and move construction", "[image][copy]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;
    using Px = typename Img::PixelFormat;

    Img original = make_pattern<F>(4, 6);

    SECTION("copy is equal and independent")
    {
        Img copy{original};
        REQUIRE(copy.Height() == original.Height());
        REQUIRE(copy.Width() == original.Width());
        REQUIRE(copy.Channel() == original.Channel());
        REQUIRE(images_equal(copy, original));

        copy.Pixel(0, 0) = color<F>(250, 250, 250, 250);
        CHECK_FALSE(images_equal(copy, original));
        // original untouched
        CHECK(same(original.Pixel(0, 0), make_pattern<F>(4, 6).Pixel(0, 0)));
    }

    SECTION("move construction transfers contents")
    {
        const Img reference = original;
        Img moved{std::move(original)};
        REQUIRE(moved.Height() == 4);
        REQUIRE(moved.Width() == 6);
        REQUIRE(moved.Channel() == static_cast<size_t>(F));
        REQUIRE(images_equal(moved, reference));
    }
}

// ===========================================================================
// Resize
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image::Resize: output geometry", "[image][resize]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;
    const Img src = make_pattern<F>(8, 12);

    SECTION("downscale")
    {
        Img out = src.Resize(4, 6);
        CHECK(out.Height() == 4);
        CHECK(out.Width() == 6);
        CHECK(out.Channel() == src.Channel());
    }
    SECTION("upscale")
    {
        Img out = src.Resize(16, 24);
        CHECK(out.Height() == 16);
        CHECK(out.Width() == 24);
    }
    SECTION("height and width are not swapped (non-uniform scale)")
    {
        Img out = src.Resize(2, 9);
        CHECK(out.Height() == 2);
        CHECK(out.Width() == 9);
    }
    SECTION("resize to 1x1")
    {
        Img out = src.Resize(1, 1);
        CHECK(out.Height() == 1);
        CHECK(out.Width() == 1);
    }
    SECTION("resizing does not modify the source")
    {
        const Img snapshot = src;
        (void)src.Resize(3, 5);
        (void)src.Resize(30, 50);
        CHECK(images_equal(src, snapshot));
    }
    SECTION("all output pixels are addressable")
    {
        Img out = src.Resize(5, 7);
        for (size_t r = 0; r < 5; ++r)
            for (size_t c = 0; c < 7; ++c)
                REQUIRE_NOTHROW(out.Pixel(r, c));
        CHECK_THROWS_AS(out.Pixel(5, 0), std::out_of_range);
        CHECK_THROWS_AS(out.Pixel(0, 7), std::out_of_range);
    }
}

TEMPLATE_TEST_CASE_SIG(
    "Image::Resize: pixel content", "[image][resize]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;
    using Px = typename Img::PixelFormat;

    SECTION("resizing to the same size is (near-)identity")
    {
        const Img src = make_pattern<F>(9, 13);
        Img out = src.Resize(9, 13);
        REQUIRE(out.Height() == 9);
        REQUIRE(out.Width() == 13);
        CHECK(images_near(src, out, 2));
    }

    SECTION("solid colour stays solid when downscaled")
    {
        Px solid_px = color<F>(200, 100, 50, 255);
        Img out = make_solid<F>(16, 16, solid_px).Resize(5, 3);
        for (size_t r = 0; r < out.Height(); ++r)
            for (size_t c = 0; c < out.Width(); ++c)
                REQUIRE(near(out.Pixel(r, c), solid_px, 2));
    }

    SECTION("solid colour stays solid when upscaled")
    {
        Px solid_px = color<F>(12, 220, 99, 255);
        Img out = make_solid<F>(3, 4, solid_px).Resize(11, 17);
        for (size_t r = 0; r < out.Height(); ++r)
            for (size_t c = 0; c < out.Width(); ++c)
                REQUIRE(near(out.Pixel(r, c), solid_px, 2));
    }

    SECTION("averaging is done in linear light (sRGB-aware)")
    {
        // black/white checker -> single pixel: linear mean 0.5 => sRGB ~188
        Img src{2, 2};
        Px black = color<F>(0, 0, 0, 255);
        Px white = color<F>(255, 255, 255, 255);
        src.Pixel(0, 0) = black;
        src.Pixel(0, 1) = white;
        src.Pixel(1, 0) = white;
        src.Pixel(1, 1) = black;

        Img out = src.Resize(1, 1);
        REQUIRE(out.Height() == 1);
        REQUIRE(out.Width() == 1);
        const auto p = out.Pixel(0, 0);
        if constexpr (F == ColorFormat::L || F == ColorFormat::LA)
        {
            CHECK(std::abs(p.L - 188) <= 3);
            CHECK(p.L > 140);
        }
        else
        {
            CHECK(std::abs(p.R - 188) <= 3);
            CHECK(std::abs(p.G - 188) <= 3);
            CHECK(std::abs(p.B - 188) <= 3);
            // must NOT be the naive (non-linear) mean of 127/128
            CHECK(p.R > 140);
        }
    }

    SECTION("white and black solid images are preserved exactly-ish")
    {
        Px black = color<F>(0, 0, 0, 255);
        Px white = color<F>(255, 255, 255, 255);
        Img b = make_solid<F>(8, 8, black).Resize(3, 3);
        Img w = make_solid<F>(8, 8, white).Resize(3, 3);
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
            {
                CHECK(near(b.Pixel(r, c), black, 1));
                CHECK(near(w.Pixel(r, c), white, 1));
            }
    }
}

TEST_CASE(
    "Image::Resize: alpha channel is carried through for RGBA",
    "[image][resize][rgba]"
)
{
    SECTION("RGBA: constant semi-transparent alpha is preserved")
    {
        Image<ColorFormat::RGBA> src =
            make_solid<ColorFormat::RGBA>(10, 10, ColorRGBA{80, 90, 100, 128});
        Image<ColorFormat::RGBA> out = src.Resize(4, 4);
        for (size_t r = 0; r < 4; ++r)
            for (size_t c = 0; c < 4; ++c)
            {
                CHECK(std::abs(out.Pixel(r, c).A - 128) <= 2);
            }
    }

    SECTION(
        "RGBA: alpha is not treated as a colour channel (not gamma-adjusted)"
    )
    {
        // alpha 0 / 255 checker averaged: linear mean 127/128, NOT 188
        Image<ColorFormat::RGBA> src{2, 2, ColorRGBA{50, 50, 50, 0}};
        src.Pixel(0, 1).A = 255;
        src.Pixel(1, 0).A = 255;
        Image<ColorFormat::RGBA> out = src.Resize(1, 1);
        CHECK(std::abs(out.Pixel(0, 0).A - 128) <= 3);
    }
}

TEST_CASE("Image::Resize: failure reporting", "[image][resize][errors]")
{
    // Assumes stb_image_resize2 returns failure for zero-sized dimensions.
    // If your stb version behaves differently, adjust these cases.
    Image<ColorFormat::RGB> src{4, 4, ColorRGB{1, 2, 3}};

    CHECK_THROWS_AS(src.Resize(0, 4), std::runtime_error);
    CHECK_THROWS_AS(src.Resize(4, 0), std::runtime_error);
    CHECK_THROWS_WITH(src.Resize(0, 0), ContainsSubstring("Failed to resize"));

    Image<ColorFormat::RGB> empty{0, 0};
    CHECK_THROWS_AS(empty.Resize(2, 2), std::runtime_error);

    SECTION("a failed resize leaves the source intact")
    {
        const auto snapshot = src;
        try
        {
            (void)src.Resize(0, 0);
        }
        catch (const std::runtime_error&)
        {
        }
        CHECK(images_equal(src, snapshot));
    }
}

// ===========================================================================
// const-correctness and assignment
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image: Write is callable on const images (all formats)",
    "[image][io][const]", ((ColorFormat F), F), ColorFormat::L, ColorFormat::LA,
    ColorFormat::RGB, ColorFormat::RGBA
)
{
    // Regression: Write<BMP/PNG/TGA> must not cast away constness.
    const Image<F> img = make_pattern<F>(3, 3);

    SECTION("BMP")
    {
        TempFile f{".bmp"};
        REQUIRE_NOTHROW(img.template Write<ImageFormat::BMP>(f.path()));
        CHECK(fs::file_size(f.path()) > 0);
    }
    SECTION("JPEG")
    {
        TempFile f{".jpg"};
        REQUIRE_NOTHROW(img.template Write<ImageFormat::JPEG>(f.path()));
        CHECK(fs::file_size(f.path()) > 0);
    }
    SECTION("PNG")
    {
        TempFile f{".png"};
        REQUIRE_NOTHROW(img.template Write<ImageFormat::PNG>(f.path()));
        CHECK(fs::file_size(f.path()) > 0);
    }
    SECTION("TGA")
    {
        TempFile f{".tga"};
        REQUIRE_NOTHROW(img.template Write<ImageFormat::TGA>(f.path()));
        CHECK(fs::file_size(f.path()) > 0);
    }
}

static_assert(std::is_copy_assignable_v<Image<ColorFormat::L>>);
static_assert(std::is_move_assignable_v<Image<ColorFormat::L>>);
static_assert(std::is_copy_assignable_v<Image<ColorFormat::LA>>);
static_assert(std::is_move_assignable_v<Image<ColorFormat::LA>>);
static_assert(std::is_copy_assignable_v<Image<ColorFormat::RGB>>);
static_assert(std::is_move_assignable_v<Image<ColorFormat::RGB>>);
static_assert(std::is_copy_assignable_v<Image<ColorFormat::RGBA>>);
static_assert(std::is_move_assignable_v<Image<ColorFormat::RGBA>>);

TEMPLATE_TEST_CASE_SIG(
    "Image: copy and move assignment", "[image][assign]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;
    using Px = typename Img::PixelFormat;

    SECTION("copy assignment replaces size and contents")
    {
        Img a{1, 1};
        const Img b = make_pattern<F>(4, 5);
        a = b;
        CHECK(a.Height() == 4);
        CHECK(a.Width() == 5);
        CHECK(a.Channel() == static_cast<size_t>(F));
        CHECK(images_equal(a, b));

        a.Pixel(0, 0) = color<F>(9, 9, 9, 9);
        CHECK_FALSE(images_equal(a, b)); // independent storage
    }

    SECTION("move assignment replaces size and contents")
    {
        Img a{1, 1};
        Img b = make_pattern<F>(3, 7);
        const Img ref = b;
        a = std::move(b);
        CHECK(a.Height() == 3);
        CHECK(a.Width() == 7);
        CHECK(images_equal(a, ref));
    }

    SECTION("self copy-assignment is safe")
    {
        Img a = make_pattern<F>(3, 3);
        const Img ref = a;
        Img& alias = a;
        a = alias;
        CHECK(images_equal(a, ref));
    }

    SECTION("assignment to/from a differently sized image")
    {
        Img big = make_pattern<F>(20, 30);
        Img small = make_pattern<F>(2, 2);
        big = small;
        CHECK(big.Height() == 2);
        CHECK(big.Width() == 2);
        CHECK_THROWS_AS(big.Pixel(2, 0), std::out_of_range);
        CHECK_THROWS_AS(big.Pixel(0, 2), std::out_of_range);
    }
}

// ===========================================================================
// Write + read round trips
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image::Write/read_image_from_file: lossless round trip (PNG, TGA)",
    "[image][io][roundtrip]", ((ColorFormat F), F), ColorFormat::L,
    ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;

    // Odd width/height on purpose to catch row padding / stride bugs.
    const Img src = make_pattern<F>(7, 11);

    SECTION("PNG")
    {
        TempFile f{".png"};
        Img copy = src;
        REQUIRE_NOTHROW(copy.template Write<ImageFormat::PNG>(f.path()));
        REQUIRE(fs::exists(f.path()));
        REQUIRE(fs::file_size(f.path()) > 0);

        Img loaded = as_image<F>(mpix::read_image_from_file(f.path()));
        CHECK(loaded.Channel() == static_cast<size_t>(F));
        CHECK(images_equal(loaded, src));
    }

    SECTION("TGA")
    {
        TempFile f{".tga"};
        Img copy = src;
        REQUIRE_NOTHROW(copy.template Write<ImageFormat::TGA>(f.path()));
        REQUIRE(fs::exists(f.path()));

        Img loaded = as_image<F>(mpix::read_image_from_file(f.path()));
        CHECK(images_equal(loaded, src));
    }

    SECTION("write does not modify the image")
    {
        TempFile f{".png"};
        Img copy = src;
        copy.template Write<ImageFormat::PNG>(f.path());
        CHECK(images_equal(copy, src));
    }

    SECTION("default Write format is JPEG and produces a loadable file")
    {
        TempFile f{".jpg"};
        Img copy = src;
        REQUIRE_NOTHROW(copy.Write(f.path()));
        auto v = mpix::read_image_from_file(f.path());
        std::visit(
            [&](const auto& img)
            {
                CHECK(img.Height() == 7);
                CHECK(img.Width() == 11);
            },
            v
        );
    }
}

TEST_CASE(
    "Image::Write: BMP round trip (RGB exact, incl. row padding)",
    "[image][io][bmp]"
)
{
    // Width 5 * 3 bytes = 15 bytes/row -> requires 1 byte of BMP row padding.
    for (size_t w : {1u, 2u, 3u, 5u, 6u, 7u})
    {
        CAPTURE(w);
        Image<ColorFormat::RGB> src = make_pattern<ColorFormat::RGB>(4, w);
        TempFile f{".bmp"};
        REQUIRE_NOTHROW(src.Write<ImageFormat::BMP>(f.path()));
        auto loaded =
            as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
        CHECK(images_equal(loaded, src));
    }
}

TEST_CASE(
    "Image::Write: BMP with RGBA writes a loadable file of right size",
    "[image][io][bmp]"
)
{
    Image<ColorFormat::RGBA> src = make_pattern<ColorFormat::RGBA>(5, 3);
    TempFile f{".bmp"};
    REQUIRE_NOTHROW(src.Write<ImageFormat::BMP>(f.path()));
    auto v = mpix::read_image_from_file(f.path());
    std::visit(
        [](const auto& img)
        {
            CHECK(img.Height() == 5);
            CHECK(img.Width() == 3);
        },
        v
    );
}

TEST_CASE("Image::Write: JPEG is lossy but close", "[image][io][jpeg]")
{
    SECTION("RGB solid colour")
    {
        const ColorRGB color{120, 60, 200};
        Image<ColorFormat::RGB> src{16, 24, color};
        TempFile f{".jpg"};
        src.Write<ImageFormat::JPEG>(f.path());

        auto loaded =
            as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
        REQUIRE(loaded.Height() == 16);
        REQUIRE(loaded.Width() == 24);
        for (size_t r = 0; r < 16; ++r)
            for (size_t c = 0; c < 24; ++c)
                REQUIRE(near(loaded.Pixel(r, c), color, 6));
    }

    SECTION("RGBA is written (alpha dropped by the format) and reloads as RGB")
    {
        Image<ColorFormat::RGBA> src{8, 8, ColorRGBA{100, 150, 200, 77}};
        TempFile f{".jpg"};
        REQUIRE_NOTHROW(src.Write<ImageFormat::JPEG>(f.path()));
        auto loaded =
            as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
        CHECK(loaded.Height() == 8);
        CHECK(loaded.Width() == 8);
        CHECK(near(loaded.Pixel(4, 4), ColorRGB{100, 150, 200}, 6));
    }
}

TEST_CASE("Image::Write: overwrites existing files", "[image][io]")
{
    TempFile f{".png"};
    Image<ColorFormat::RGB> first{4, 4, ColorRGB{255, 0, 0}};
    Image<ColorFormat::RGB> second{6, 3, ColorRGB{0, 255, 0}};
    first.Write<ImageFormat::PNG>(f.path());
    second.Write<ImageFormat::PNG>(f.path());

    auto loaded =
        as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
    CHECK(loaded.Height() == 6);
    CHECK(loaded.Width() == 3);
    CHECK(same(loaded.Pixel(0, 0), ColorRGB{0, 255, 0}));
}

TEST_CASE("Image::Write: failure reporting", "[image][io][errors]")
{
    Image<ColorFormat::RGB> img{2, 2, ColorRGB{1, 2, 3}};
    const fs::path bad =
        fs::temp_directory_path() / "mimg_no_such_dir_xyz" / "out";

    SECTION("nonexistent directory throws runtime_error for every format")
    {
        CHECK_THROWS_AS(
            img.Write<ImageFormat::PNG>(bad / "a.png"), std::runtime_error
        );
        CHECK_THROWS_AS(
            img.Write<ImageFormat::JPEG>(bad / "a.jpg"), std::runtime_error
        );
        CHECK_THROWS_AS(
            img.Write<ImageFormat::BMP>(bad / "a.bmp"), std::runtime_error
        );
        CHECK_THROWS_AS(
            img.Write<ImageFormat::TGA>(bad / "a.tga"), std::runtime_error
        );
    }

    SECTION("error message names the path")
    {
        CHECK_THROWS_WITH(
            img.Write<ImageFormat::PNG>(bad / "a.png"),
            ContainsSubstring("Failed to write the image at") &&
                ContainsSubstring("a.png")
        );
    }

    SECTION("writing to a path that is a directory throws")
    {
        CHECK_THROWS_AS(
            img.Write<ImageFormat::PNG>(fs::temp_directory_path()),
            std::runtime_error
        );
    }
}

TEST_CASE(
    "Image::Write: JPEG quality parameter is honoured", "[image][io][jpeg]"
)
{
    const Image<ColorFormat::RGB> noise = make_noise(64, 64);
    TempFile q10{".jpg"};
    TempFile q50{".jpg"};
    TempFile q100{".jpg"};
    noise.Write<ImageFormat::JPEG>(q10.path(), 10);
    noise.Write<ImageFormat::JPEG>(q50.path(), 50);
    noise.Write<ImageFormat::JPEG>(q100.path(), 100);

    const auto s10 = fs::file_size(q10.path());
    const auto s50 = fs::file_size(q50.path());
    const auto s100 = fs::file_size(q100.path());
    CHECK(s10 < s50);
    CHECK(s50 < s100);
}

TEST_CASE(
    "Image::Write: lower JPEG quality gives larger error", "[image][io][jpeg]"
)
{
    const auto src = make_noise(32, 32);
    TempFile lo{".jpg"};
    TempFile hi{".jpg"};
    src.Write<ImageFormat::JPEG>(lo.path(), 5);
    src.Write<ImageFormat::JPEG>(hi.path(), 100);

    auto err = [&](const fs::path& p)
    {
        auto img = as_image<ColorFormat::RGB>(mpix::read_image_from_file(p));
        long total = 0;
        for (size_t r = 0; r < 32; ++r)
            for (size_t c = 0; c < 32; ++c)
            {
                const auto a = src.Pixel(r, c);
                const auto b = img.Pixel(r, c);
                total += std::abs(a.R - b.R) + std::abs(a.G - b.G) +
                         std::abs(a.B - b.B);
            }
        return total;
    };
    CHECK(err(lo.path()) > err(hi.path()));
}

// ===========================================================================
// read_image_from_file
// ===========================================================================

TEST_CASE(
    "read_image_from_file: variant alternative follows channel count",
    "[image][io][read]"
)
{
    SECTION("1-channel file -> L")
    {
        TempFile f{".png"};
        make_pattern<ColorFormat::L>(3, 3).Write<ImageFormat::PNG>(f.path());
        auto v = mpix::read_image_from_file(f.path());
        CHECK(std::holds_alternative<Image<ColorFormat::L>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::LA>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGB>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGBA>>(v));
    }

    SECTION("2-channel file -> LA")
    {
        TempFile f{".png"};
        make_pattern<ColorFormat::LA>(3, 3).Write<ImageFormat::PNG>(f.path());
        auto v = mpix::read_image_from_file(f.path());
        CHECK(std::holds_alternative<Image<ColorFormat::LA>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::L>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGB>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGBA>>(v));
    }

    SECTION("3-channel file -> RGB")
    {
        TempFile f{".png"};
        make_pattern<ColorFormat::RGB>(3, 3).Write<ImageFormat::PNG>(f.path());
        auto v = mpix::read_image_from_file(f.path());
        CHECK(std::holds_alternative<Image<ColorFormat::RGB>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::L>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::LA>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGBA>>(v));
    }

    SECTION("4-channel file -> RGBA")
    {
        TempFile f{".png"};
        make_pattern<ColorFormat::RGBA>(3, 3).Write<ImageFormat::PNG>(f.path());
        auto v = mpix::read_image_from_file(f.path());
        CHECK(std::holds_alternative<Image<ColorFormat::RGBA>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::L>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::LA>>(v));
        CHECK_FALSE(std::holds_alternative<Image<ColorFormat::RGB>>(v));
    }
}

TEST_CASE(
    "read_image_from_file: height/width are not swapped", "[image][io][read]"
)
{
    TempFile f{".png"};
    make_pattern<ColorFormat::RGB>(3, 10).Write<ImageFormat::PNG>(f.path());
    auto img = as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
    CHECK(img.Height() == 3);
    CHECK(img.Width() == 10);
}

TEST_CASE("read_image_from_file: error handling", "[image][io][read][errors]")
{
    SECTION("missing file")
    {
        const fs::path missing =
            fs::temp_directory_path() / "mimg_definitely_missing_file.png";
        REQUIRE_FALSE(fs::exists(missing));
        CHECK_THROWS_AS(
            mpix::read_image_from_file(missing), std::runtime_error
        );
        CHECK_THROWS_WITH(
            mpix::read_image_from_file(missing),
            ContainsSubstring("Failed to load image from") &&
                ContainsSubstring("mimg_definitely_missing_file.png")
        );
    }

    SECTION("file that is not an image")
    {
        TempFile f{".png"};
        {
            std::ofstream out(f.path(), std::ios::binary);
            out << "this is definitely not a PNG file";
        }
        CHECK_THROWS_AS(
            mpix::read_image_from_file(f.path()), std::runtime_error
        );
    }

    SECTION("empty file")
    {
        TempFile f{".png"};
        {
            std::ofstream out(f.path(), std::ios::binary);
        }
        CHECK_THROWS_AS(
            mpix::read_image_from_file(f.path()), std::runtime_error
        );
    }

    SECTION("truncated PNG")
    {
        TempFile good{".png"};
        make_noise(32, 32).Write<ImageFormat::PNG>(good.path());
        std::ifstream in(good.path(), std::ios::binary);
        std::vector<char> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        REQUIRE(bytes.size() > 64);

        TempFile cut{".png"};
        {
            std::ofstream out(cut.path(), std::ios::binary);
            out.write(
                bytes.data(), static_cast<std::streamsize>(bytes.size() / 3)
            );
        }
        CHECK_THROWS_AS(
            mpix::read_image_from_file(cut.path()), std::runtime_error
        );
    }

    SECTION("grayscale (1 channel) image is properly supported")
    {
        TempFile f{".png"};
        const uint8_t gray[4] = {0, 85, 170, 255};
        REQUIRE(
            stbi_write_png(f.path().string().c_str(), 2, 2, 1, gray, 2) != 0
        );
        auto v = mpix::read_image_from_file(f.path());
        REQUIRE(std::holds_alternative<Image<ColorFormat::L>>(v));
        auto img = std::get<Image<ColorFormat::L>>(v);
        CHECK(img.Height() == 2);
        CHECK(img.Width() == 2);
        CHECK(img.Pixel(0, 0).L == 0);
        CHECK(img.Pixel(0, 1).L == 85);
        CHECK(img.Pixel(1, 0).L == 170);
        CHECK(img.Pixel(1, 1).L == 255);
    }

    SECTION("grayscale + alpha (2 channel) image is properly supported")
    {
        TempFile f{".png"};
        const uint8_t ga[8] = {0, 255, 85, 255, 170, 255, 255, 255};
        REQUIRE(stbi_write_png(f.path().string().c_str(), 2, 2, 2, ga, 4) != 0);
        auto v = mpix::read_image_from_file(f.path());
        REQUIRE(std::holds_alternative<Image<ColorFormat::LA>>(v));
        auto img = std::get<Image<ColorFormat::LA>>(v);
        CHECK(img.Height() == 2);
        CHECK(img.Width() == 2);
        CHECK(img.Pixel(0, 0).L == 0);
        CHECK(img.Pixel(0, 0).A == 255);
        CHECK(img.Pixel(0, 1).L == 85);
        CHECK(img.Pixel(0, 1).A == 255);
    }
}

TEST_CASE(
    "read_image_from_file: accepts std::filesystem::path with spaces",
    "[image][io][read]"
)
{
    TempFile base{".png"};
    fs::path p = base.path();
    p.replace_filename("mimg test with spaces.png");
    struct Cleanup
    {
        fs::path p;
        ~Cleanup()
        {
            std::error_code ec;
            fs::remove(p, ec);
        }
    } cleanup{p};

    Image<ColorFormat::RGB> img = make_pattern<ColorFormat::RGB>(2, 2);
    img.Write<ImageFormat::PNG>(p);
    auto loaded = as_image<ColorFormat::RGB>(mpix::read_image_from_file(p));
    CHECK(images_equal(loaded, img));
}

// ===========================================================================
// Integration: pipeline
// ===========================================================================

TEST_CASE("Pipeline: read -> resize -> write -> read", "[image][integration]")
{
    SECTION("RGBA pipeline")
    {
        TempFile in{".png"};
        TempFile out{".png"};

        Image<ColorFormat::RGBA> src =
            make_solid<ColorFormat::RGBA>(20, 30, ColorRGBA{10, 200, 90, 255});
        src.Write<ImageFormat::PNG>(in.path());

        auto loaded =
            as_image<ColorFormat::RGBA>(mpix::read_image_from_file(in.path()));
        auto small = loaded.Resize(5, 8);
        small.Write<ImageFormat::PNG>(out.path());

        auto final_img =
            as_image<ColorFormat::RGBA>(mpix::read_image_from_file(out.path()));
        REQUIRE(final_img.Height() == 5);
        REQUIRE(final_img.Width() == 8);
        for (size_t r = 0; r < 5; ++r)
            for (size_t c = 0; c < 8; ++c)
                REQUIRE(
                    near(final_img.Pixel(r, c), ColorRGBA{10, 200, 90, 255}, 2)
                );
    }

    SECTION("L (Grayscale) pipeline")
    {
        TempFile in{".png"};
        TempFile out{".png"};

        Image<ColorFormat::L> src =
            make_solid<ColorFormat::L>(20, 30, ColorL{120});
        src.Write<ImageFormat::PNG>(in.path());

        auto loaded =
            as_image<ColorFormat::L>(mpix::read_image_from_file(in.path()));
        auto small = loaded.Resize(5, 8);
        small.Write<ImageFormat::PNG>(out.path());

        auto final_img =
            as_image<ColorFormat::L>(mpix::read_image_from_file(out.path()));
        REQUIRE(final_img.Height() == 5);
        REQUIRE(final_img.Width() == 8);
        for (size_t r = 0; r < 5; ++r)
            for (size_t c = 0; c < 8; ++c)
                REQUIRE(near(final_img.Pixel(r, c), ColorL{120}, 2));
    }

    SECTION("LA (Grayscale with Alpha) pipeline")
    {
        TempFile in{".png"};
        TempFile out{".png"};

        Image<ColorFormat::LA> src =
            make_solid<ColorFormat::LA>(20, 30, ColorLA{120, 200});
        src.Write<ImageFormat::PNG>(in.path());

        auto loaded =
            as_image<ColorFormat::LA>(mpix::read_image_from_file(in.path()));
        auto small = loaded.Resize(5, 8);
        small.Write<ImageFormat::PNG>(out.path());

        auto final_img =
            as_image<ColorFormat::LA>(mpix::read_image_from_file(out.path()));
        REQUIRE(final_img.Height() == 5);
        REQUIRE(final_img.Width() == 8);
        for (size_t r = 0; r < 5; ++r)
            for (size_t c = 0; c < 8; ++c)
                REQUIRE(near(final_img.Pixel(r, c), ColorLA{120, 200}, 2));
    }
}

// ===========================================================================
// VFlip, extra I/O coverage and thread-safety
// ===========================================================================

TEMPLATE_TEST_CASE_SIG(
    "Image::VFlip: reverses rows", "[image][vflip]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;

    // Covers 1 row, even and odd heights, 1 column and wide images.
    const auto [h, w] = GENERATE(
        std::pair<size_t, size_t>{1, 1}, std::pair<size_t, size_t>{1, 7},
        std::pair<size_t, size_t>{2, 3}, std::pair<size_t, size_t>{3, 1},
        std::pair<size_t, size_t>{4, 5}, std::pair<size_t, size_t>{5, 4},
        std::pair<size_t, size_t>{16, 9}
    );
    CAPTURE(h, w);

    const Img src = make_pattern<F>(h, w);
    const Img flipped = src.VFlip();

    SECTION("geometry is preserved")
    {
        CHECK(flipped.Height() == h);
        CHECK(flipped.Width() == w);
        CHECK(flipped.Channel() == src.Channel());
        CHECK(flipped.Format() == F);
    }

    SECTION("row r of the result is row h-1-r of the source")
    {
        for (size_t r = 0; r < h; ++r)
            for (size_t c = 0; c < w; ++c)
                REQUIRE(same(flipped.Pixel(r, c), src.Pixel(h - 1 - r, c)));
    }

    SECTION("columns are not mirrored")
    {
        // Within a row, pixel order is untouched.
        for (size_t c = 0; c < w; ++c)
            CHECK(same(flipped.Pixel(h - 1, c), src.Pixel(0, c)));
    }

    SECTION("the source is not modified")
    {
        CHECK(images_equal(src, make_pattern<F>(h, w)));
    }

    SECTION("flipping twice gives the original")
    {
        CHECK(images_equal(flipped.VFlip(), src));
    }

    SECTION("result is independent storage")
    {
        Img copy = src.VFlip();
        copy.Pixel(0, 0) = color<F>(1, 2, 3, 4);
        CHECK(images_equal(src, make_pattern<F>(h, w)));
    }
}

TEMPLATE_TEST_CASE_SIG(
    "Image::VFlip: degenerate sizes", "[image][vflip]", ((ColorFormat F), F),
    ColorFormat::L, ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    using Img = Image<F>;

    for (auto [h, w] :
         {std::pair<size_t, size_t>{0, 0}, std::pair<size_t, size_t>{0, 5},
          std::pair<size_t, size_t>{5, 0}})
    {
        CAPTURE(h, w);
        const Img empty{h, w};
        Img flipped{1, 1};
        REQUIRE_NOTHROW(flipped = empty.VFlip());
        CHECK(flipped.Height() == h);
        CHECK(flipped.Width() == w);
    }
}

TEST_CASE("Image::VFlip: top and bottom rows swap exactly", "[image][vflip]")
{
    Image<ColorFormat::RGB> img{3, 2};
    img.Pixel(0, 0) = ColorRGB{1, 1, 1};
    img.Pixel(0, 1) = ColorRGB{2, 2, 2};
    img.Pixel(1, 0) = ColorRGB{3, 3, 3};
    img.Pixel(1, 1) = ColorRGB{4, 4, 4};
    img.Pixel(2, 0) = ColorRGB{5, 5, 5};
    img.Pixel(2, 1) = ColorRGB{6, 6, 6};

    const auto f = img.VFlip();
    CHECK(same(f.Pixel(0, 0), ColorRGB{5, 5, 5}));
    CHECK(same(f.Pixel(0, 1), ColorRGB{6, 6, 6}));
    CHECK(same(f.Pixel(1, 0), ColorRGB{3, 3, 3})); // middle row stays
    CHECK(same(f.Pixel(1, 1), ColorRGB{4, 4, 4}));
    CHECK(same(f.Pixel(2, 0), ColorRGB{1, 1, 1}));
    CHECK(same(f.Pixel(2, 1), ColorRGB{2, 2, 2}));
}

TEMPLATE_TEST_CASE_SIG(
    "Image::VFlip: flipped image survives a file round trip",
    "[image][vflip][io]", ((ColorFormat F), F), ColorFormat::L, ColorFormat::LA,
    ColorFormat::RGB, ColorFormat::RGBA
)
{
    const Image<F> src = make_pattern<F>(5, 4);
    TempFile f{".png"};
    src.VFlip().template Write<ImageFormat::PNG>(f.path());

    const auto loaded = as_image<F>(mpix::read_image_from_file(f.path()));
    REQUIRE(loaded.Height() == 5);
    REQUIRE(loaded.Width() == 4);
    for (size_t r = 0; r < 5; ++r)
        for (size_t c = 0; c < 4; ++c)
            REQUIRE(same(loaded.Pixel(r, c), src.Pixel(4 - r, c)));
}

TEST_CASE("Image::Resize: L is averaged in linear light", "[image][resize][l]")
{
    Image<ColorFormat::L> src{2, 2};
    src.Pixel(0, 1).L = 255;
    src.Pixel(1, 0).L = 255;
    const auto out = src.Resize(1, 1);
    CHECK(std::abs(out.Pixel(0, 0).L - 188) <= 3);
}

TEST_CASE("Image::Resize: LA alpha is filtered linearly", "[image][resize][la]")
{
    mpix::Image<mpix::ColorFormat::LA> src{2, 2, mpix::ColorLA{50, 0}};
    src.Pixel(0, 1).A = 255;
    src.Pixel(1, 0).A = 255;

    const auto out = src.Resize(1, 1);
    CHECK(std::abs(out.Pixel(0, 0).A - 128) <= 3);
    CHECK(std::abs(out.Pixel(0, 0).L - 50) <= 2);
}

TEMPLATE_TEST_CASE_SIG(
    "Image::Write: every file format works for every colour format",
    "[image][io]", ((ColorFormat F), F), ColorFormat::L, ColorFormat::LA,
    ColorFormat::RGB, ColorFormat::RGBA
)
{
    const Image<F> img = make_pattern<F>(6, 5);

    const auto check = [&](const std::string& ext, auto write)
    {
        TempFile f{ext};
        REQUIRE_NOTHROW(write(f.path()));
        REQUIRE(fs::file_size(f.path()) > 0);
        auto v = mpix::read_image_from_file(f.path());
        std::visit(
            [](const auto& loaded)
            {
                CHECK(loaded.Height() == 6);
                CHECK(loaded.Width() == 5);
            },
            v
        );
    };

    SECTION("BMP")
    {
        check(
            ".bmp",
            [&](const fs::path& p) { img.template Write<ImageFormat::BMP>(p); }
        );
    }
    SECTION("JPEG")
    {
        check(
            ".jpg", [&](const fs::path& p)
            { img.template Write<ImageFormat::JPEG>(p, 90); }
        );
    }
    SECTION("PNG")
    {
        check(
            ".png",
            [&](const fs::path& p) { img.template Write<ImageFormat::PNG>(p); }
        );
    }
    SECTION("TGA")
    {
        check(
            ".tga",
            [&](const fs::path& p) { img.template Write<ImageFormat::TGA>(p); }
        );
    }
}

TEMPLATE_TEST_CASE_SIG(
    "Image: lossless formats preserve channel count and pixels",
    "[image][io][roundtrip]", ((ColorFormat F), F), ColorFormat::L,
    ColorFormat::LA, ColorFormat::RGB, ColorFormat::RGBA
)
{
    const Image<F> src = make_pattern<F>(7, 11);

    SECTION("PNG")
    {
        TempFile f{".png"};
        src.template Write<ImageFormat::PNG>(f.path());
        const auto loaded = as_image<F>(mpix::read_image_from_file(f.path()));
        CHECK(loaded.Format() == F);
        CHECK(images_equal(loaded, src));
    }
    SECTION("TGA")
    {
        TempFile f{".tga"};
        src.template Write<ImageFormat::TGA>(f.path());
        auto v = mpix::read_image_from_file(f.path());
        // TGA stores L as 1 channel and the others as given; LA may be read
        // back as 2 channels. Only assert exactness when the format survives.
        if (std::holds_alternative<Image<F>>(v))
            CHECK(images_equal(std::get<Image<F>>(v), src));
    }
}

TEST_CASE("Image::Write: overwrite replaces the file completely", "[image][io]")
{
    // A smaller file written over a larger one must not leave trailing bytes.
    TempFile f{".bmp"};
    make_noise(64, 64).Write<ImageFormat::BMP>(f.path());
    const auto big = fs::file_size(f.path());

    const Image<ColorFormat::RGB> small{2, 2, ColorRGB{1, 2, 3}};
    small.Write<ImageFormat::BMP>(f.path());
    CHECK(fs::file_size(f.path()) < big);

    const auto loaded =
        as_image<ColorFormat::RGB>(mpix::read_image_from_file(f.path()));
    CHECK(images_equal(loaded, small));
}

TEST_CASE(
    "read_image_from_file: a directory is not an image",
    "[image][io][read][errors]"
)
{
    CHECK_THROWS_AS(
        mpix::read_image_from_file(fs::temp_directory_path()),
        std::runtime_error
    );
}

TEST_CASE(
    "read_image_from_file: error message contains the reason",
    "[image][io][read][errors]"
)
{
    TempFile f{".png"};
    {
        std::ofstream out(f.path(), std::ios::binary);
        out << "not an image";
    }
    CHECK_THROWS_WITH(
        mpix::read_image_from_file(f.path()),
        ContainsSubstring("Failed to load image from") &&
            ContainsSubstring(f.path().filename().string())
    );
}

TEST_CASE("Concurrent Write / VFlip / read are safe", "[image][thread]")
{
    // Regression for the removed global stb "flip on write" state.
    constexpr int kThreads = 8;
    const Image<ColorFormat::RGBA> src =
        make_pattern<ColorFormat::RGBA>(32, 24);

    std::vector<std::thread> threads;
    std::vector<int> ok(kThreads, 0);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&, t]
            {
                try
                {
                    const bool flip = (t % 2) == 1;
                    TempFile f{".png"};
                    for (int i = 0; i < 10; ++i)
                    {
                        if (flip) src.VFlip().Write<ImageFormat::PNG>(f.path());
                        else src.Write<ImageFormat::PNG>(f.path());

                        auto v = mpix::read_image_from_file(f.path());
                        const auto& img = std::get<Image<ColorFormat::RGBA>>(v);
                        const Image<ColorFormat::RGBA> expected =
                            flip ? src.VFlip() : src;
                        if (!images_equal(img, expected)) return;
                    }
                    ok[static_cast<size_t>(t)] = 1;
                }
                catch (...)
                {
                }
            }
        );
    }
    for (auto& th : threads)
        th.join();

    for (int t = 0; t < kThreads; ++t)
    {
        CAPTURE(t);
        CHECK(ok[static_cast<size_t>(t)] == 1);
    }
}

// Defined in odr_tu.cpp: a second TU including mpix.hpp.
int mpix_odr_probe();

TEST_CASE(
    "Header can be included from several translation units", "[image][odr]"
)
{
    CHECK(mpix_odr_probe() == 6);
}
