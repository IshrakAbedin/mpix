add_library(mpix_stb_image STATIC)

target_include_directories(mpix_stb_image
  PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(mpix_stb_image
  PRIVATE
    stb_impl.cpp

  PUBLIC
    FILE_SET HEADERS
    FILES
      stb_image.h
      stb_image_write.h
      stb_image_resize2.h
)
