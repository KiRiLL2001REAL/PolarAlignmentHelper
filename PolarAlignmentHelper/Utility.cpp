#include "Utility.h"

#include <utf8.h>

#include <fstream>
#include <filesystem>
#include <mutex>
#include <toojpeg.h>

const std::string& Utility::wstringToUtf8(const std::wstring& wstr)
{
    thread_local std::string result;
    result.clear();
    utf8::utf16to8(
        wstr.data(),
        wstr.data() + wstr.size(),
        std::back_inserter(result)
    );
    return result;
}

namespace {
    std::ofstream* __writeRGBJpeg_ofstream = NULL;
    std::mutex __writeRGBJpegMutex;

    void writeOneByte(unsigned char byte) {
        *__writeRGBJpeg_ofstream << byte;
    }
}

bool Utility::writeRGBJpeg(
    unsigned char* data,
    unsigned width,
    unsigned height,
    unsigned quality,
    const std::string& filename
) {
    std::scoped_lock lock(__writeRGBJpegMutex);

    namespace fs = std::filesystem;

    fs::path p_filename = fs::weakly_canonical(fs::path(filename));
    fs::path fname = fs::path(p_filename).filename();
    fs::path fdir = fs::path(p_filename).remove_filename();

    if (fs::exists(fdir)) {
        if (!fs::is_directory(fdir)) {
            printf("[E] Utility::writeRGBJpeg: \"%s\" is not a directory.\n", fdir.generic_string().c_str());
            return false;
        }
    }
    else
        fs::create_directories(fdir);

    if (!__writeRGBJpeg_ofstream)
        __writeRGBJpeg_ofstream = new std::ofstream(filename, std::ios_base::out | std::ios_base::binary);

    if (!__writeRGBJpeg_ofstream->is_open()) {
        printf("[E] Utility::writeRGBJpeg: Can't write a file \"%s\"\n", filename.c_str());
        return false;
    }

    bool ok = TooJpeg::writeJpeg(writeOneByte, data, width, height, true, quality, false);

    __writeRGBJpeg_ofstream->close();
    delete __writeRGBJpeg_ofstream;
    __writeRGBJpeg_ofstream = NULL;

    return ok;
}
