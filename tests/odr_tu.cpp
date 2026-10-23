// Second translation unit including mpix.hpp (see tests/CMakeLists.txt).
// Everything non-template in the header must be inline, otherwise linking this
// file together with tests.cpp fails with duplicate symbols.
#include <mpix/mpix.hpp>

int mpix_odr_probe()
{
    mpix::Image<mpix::ColorFormat::RGB> img{2, 3};
    const auto flipped = img.VFlip();
    try
    {
        (void)mpix::read_image_from_file("this-file-does-not-exist.png");
    }
    catch (const std::runtime_error&)
    {
        return static_cast<int>(flipped.Height() * flipped.Width());
    }
    return -1;
}
