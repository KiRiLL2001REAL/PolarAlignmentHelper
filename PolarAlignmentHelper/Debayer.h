#pragma once

#include <stdexcept>
#include <minwindef.h>

class Debayer
{
public:
	static constexpr unsigned fourccRGGB = 1111967570;
	static constexpr unsigned fourccBGGR = 1380403010;
	static constexpr unsigned fourccGRBG = 1195528775;
	static constexpr unsigned fourccGBRG = 1196573255;

	// by Grok
	template<typename T>
	static void demosaic(
		const T* raw,
		int width,
		int height,
		unsigned fourcc,
		T* rgb
	) {
		if (width < 2 || height < 2) { // ћинимальный размер дл€ демозаики
			throw std::invalid_argument("Image is too small.");
		}

		// Ћ€мбда дл€ получени€ значени€ пиксел€ с зацикливанием по кра€м (replicate)
		auto get_raw = [&](int y, int x) -> uint32_t {
			y = max(0, min(height - 1, y));
			x = max(0, min(width  - 1, x));
			return static_cast<uint32_t>(raw[y * width + x]);
		};

        // Ћ€мбда дл€ определени€ цвета в позиции (с обработкой отрицательных индексов)
        auto get_bayer_color = [&](int y, int x) -> char {
            int ry = y % 2;
            if (ry < 0) ry += 2;
            int rx = x % 2;
            if (rx < 0) rx += 2;

            if (fourccRGGB == fourcc) {
                if (ry == 0 && rx == 0) return 'R';
                if (ry == 0 && rx == 1) return 'G';
                if (ry == 1 && rx == 0) return 'G';
                if (ry == 1 && rx == 1) return 'B';
            }
            else if (fourccBGGR == fourcc) {
                if (ry == 0 && rx == 0) return 'B';
                if (ry == 0 && rx == 1) return 'G';
                if (ry == 1 && rx == 0) return 'G';
                if (ry == 1 && rx == 1) return 'R';
            }
            else if (fourccGRBG == fourcc) {
                if (ry == 0 && rx == 0) return 'G';
                if (ry == 0 && rx == 1) return 'R';
                if (ry == 1 && rx == 0) return 'B';
                if (ry == 1 && rx == 1) return 'G';
            }
            else if (fourccGBRG == fourcc) {
                if (ry == 0 && rx == 0) return 'G';
                if (ry == 0 && rx == 1) return 'B';
                if (ry == 1 && rx == 0) return 'R';
                if (ry == 1 && rx == 1) return 'G';
            }
            return '?';
        };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                char color = get_bayer_color(y, x);
                size_t idx = static_cast<size_t>(y) * width * 3 + static_cast<size_t>(x) * 3;

                uint32_t R = 0, G = 0, B = 0;

                if (color == 'R') {
                    R = get_raw(y, x);
                    // «еленый Ч среднее 4 ортогональных соседей (всегда G)
                    G = (get_raw(y - 1, x) + get_raw(y + 1, x) +
                        get_raw(y, x - 1) + get_raw(y, x + 1)) / 4;
                    // —иний Ч среднее 4 диагональных соседей (всегда B)
                    B = (get_raw(y - 1, x - 1) + get_raw(y - 1, x + 1) +
                        get_raw(y + 1, x - 1) + get_raw(y + 1, x + 1)) / 4;
                }
                else if (color == 'B') {
                    B = get_raw(y, x);
                    // «еленый Ч среднее 4 ортогональных
                    G = (get_raw(y - 1, x) + get_raw(y + 1, x) +
                        get_raw(y, x - 1) + get_raw(y, x + 1)) / 4;
                    //  расный Ч среднее 4 диагональных (всегда R)
                    R = (get_raw(y - 1, x - 1) + get_raw(y - 1, x + 1) +
                        get_raw(y + 1, x - 1) + get_raw(y + 1, x + 1)) / 4;
                }
                else if (color == 'G') {
                    G = get_raw(y, x);
                    // ќпредел€ем, какие цвета по горизонтали (R или B)
                    char hor_color = get_bayer_color(y, x - 1);
                    if (hor_color == 'R') {
                        // √оризонталь Ч R, вертикаль Ч B
                        R = (get_raw(y, x - 1) + get_raw(y, x + 1)) / 2;
                        B = (get_raw(y - 1, x) + get_raw(y + 1, x)) / 2;
                    }
                    else if (hor_color == 'B') {
                        // √оризонталь Ч B, вертикаль Ч R
                        B = (get_raw(y, x - 1) + get_raw(y, x + 1)) / 2;
                        R = (get_raw(y - 1, x) + get_raw(y + 1, x)) / 2;
                    }
                }

                // «аписываем в выходной буфер (R-G-B пор€док)
                rgb[idx + 0] = static_cast<T>(R);
                rgb[idx + 1] = static_cast<T>(G);
                rgb[idx + 2] = static_cast<T>(B);
            }
        }
	}
};

