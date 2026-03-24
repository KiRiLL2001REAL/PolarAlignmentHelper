#pragma once

namespace Imaging {

    struct FrameHeader {
        unsigned width;
        unsigned height;
        unsigned fourCC;
        size_t buffer_size;
        unsigned short bpp;
        bool isRaw;
    };

}