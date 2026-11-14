// The one translation unit that compiles the vendored single-header libraries.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define MSF_GIF_IMPL
#include "msf_gif/msf_gif.h"
