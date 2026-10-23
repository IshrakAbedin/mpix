# mpix

**M**anaged **Pix**els: a small C++20 library for loading, editing, resizing, and
saving 8-bit images, built on [stb](https://github.com/nothings/stb).

- Grayscale (`L`), grayscale + alpha (`LA`), `RGB` and `RGBA` images
- Load / save PNG, JPEG, BMP and TGA
- Bounds-checked pixel access, `Resize`, `VFlip`
- Errors are reported with exceptions
- stb is bundled; there is nothing else to install

## Usage

```cpp
#include <mpix/mpix.hpp>

int main()
{
    // The channel layout of the file decides the image type.
    mpix::ImageVariant v = mpix::read_image_from_file("in.png");

    std::visit(
        [](const auto& img)
        {
            auto out = img.Resize(256, 256).VFlip();
            out.template Write<mpix::ImageFormat::JPEG>("out.jpg", 90);
        },
        v
    );

    mpix::Image<mpix::ColorFormat::RGBA> canvas{64, 64, {255, 0, 0}};
    canvas.Pixel(10, 20).A = 128;
}
```

|                                                    |                                                 |
| -------------------------------------------------- | ----------------------------------------------- |
| `Image<F>(h, w, color)` / `(h, w, const uint8_t*)` | Filled image / copy of raw packed pixels        |
| `Height() Width() Channel() Format()`              | Properties                                      |
| `Pixel(r, c)` / `operator()(r, c)`                 | Bounds-checked access (`std::out_of_range`)     |
| `Resize(h, w)`                                     | Resized copy                                    |
| `VFlip()`                                          | Vertically flipped copy                         |
| `Write<ImageFormat>(path, quality)`                | Save as `BMP`, `JPEG` (default), `PNG` or `TGA` |
| `read_image_from_file(path)`                       | Returns an `ImageVariant`                       |

JPEG drops alpha and BMP stores grayscale as RGB.

## CMake

Requires CMake 3.23 and a C++20 compiler with `<format>`.

```cmake
# FetchContent
include(FetchContent)
FetchContent_Declare(
    mpix
    GIT_REPOSITORY https://github.com/IshrakAbedin/mpix.git
    GIT_TAG        <TAG VERSON> # e.g., v0.1.1
)
FetchContent_MakeAvailable(mpix)

# or CPM
CPMAddPackage("gh:IshrakAbedin/mpix@<version>") #e.g., mpix@0.1.1

target_link_libraries(my_app PRIVATE mpix::mpix)
```

`add_subdirectory(mpix)` works as well. Tests are only built by default when
mpix is the top-level project; control them with `-DMPIX_BUILD_TESTS=ON|OFF` (or `-DBUILD_TESTING=OFF` in general).

## Tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Catch2 is downloaded with CPM.

## Notes

`mpix` compiles the bundled stb implementations into a small static library
(`mpix_stb_image`). If your project also compiles stb implementations you will
get duplicate symbols; use a single copy.

## License

MIT, see `LICENSE`. `stb` is public domain / MIT (see `vendor/stb/LICENSE`).
