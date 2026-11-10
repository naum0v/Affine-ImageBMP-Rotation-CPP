#include <iostream>
#include <stdint.h>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t file_type{ 0x4D42 };
    uint32_t file_size{ 0 };
    uint16_t reserved1{ 0 };
    uint16_t reserved2{ 0 };
    uint32_t offset_data{ 0 };
};

struct BMPInfoHeader {
    uint32_t size{ 0 };
    int32_t width{ 0 };
    int32_t height{ 0 };
    uint16_t planes{ 1 };
    uint16_t bit_count{ 0 };
    uint32_t compression{ 0 };
    uint32_t size_image{ 0 };
    int32_t x_pixels_per_meter{ 0 };
    int32_t y_pixels_per_meter{ 0 };
    uint32_t colors_used{ 0 };
    uint32_t colors_important{ 0 };
};

struct BMPColorHeader {
    uint32_t red_mask{ 0x00ff0000 };
    uint32_t green_mask{ 0x0000ff00 };
    uint32_t blue_mask{ 0x000000ff };
    uint32_t alpha_mask{ 0xff000000 };
    uint32_t color_space_type{ 0x73524742 };
    uint32_t unused[16]{ 0 };
};

struct RGBQuad {
    uint8_t blue{ 0 };
    uint8_t green{ 0 };
    uint8_t red{ 0 };
    uint8_t reserved{ 0 };
};
#pragma pack(pop)

struct BMP {
    BMPFileHeader file_header;
    BMPInfoHeader bmp_info_header;
    BMPColorHeader bmp_color_header;
    std::vector<RGBQuad> palette;
    std::vector<uint8_t> data;

    void read(const std::string& filename) {
        std::ifstream inp{ filename, std::ios_base::binary };
        if (!inp) {
            throw std::runtime_error("Не удалось открыть файл изображения.");
        }

        inp.read((char*)&file_header, sizeof(file_header));
        if (file_header.file_type != 0x4D42) {
            throw std::runtime_error("Ошибка: файл не является BMP.");
        }

        inp.read((char*)&bmp_info_header, sizeof(bmp_info_header));

        if (bmp_info_header.bit_count == 8) {
            uint32_t num_colors = (bmp_info_header.colors_used == 0) ? 256 : bmp_info_header.colors_used;
            if (num_colors > 256) num_colors = 256;
            palette.resize(num_colors);
            inp.read((char*)palette.data(), num_colors * sizeof(RGBQuad));
        }
        else if (bmp_info_header.bit_count == 32) {
            if (bmp_info_header.size >= (sizeof(BMPInfoHeader) + sizeof(BMPColorHeader))) {
                inp.read((char*)&bmp_color_header, sizeof(bmp_color_header));
            }
            else {
                throw std::runtime_error("Ошибка: файл не содержит информацию о цветовых масках.");
            }
        }

        inp.seekg(file_header.offset_data, inp.beg);

        if (bmp_info_header.bit_count == 32) {
            bmp_info_header.size = sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
        }
        else if (bmp_info_header.bit_count == 8) {
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + palette.size() * sizeof(RGBQuad);
        }
        else {
            bmp_info_header.size = sizeof(BMPInfoHeader);
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
        }
        file_header.file_size = file_header.offset_data;

        if (bmp_info_header.height < 0) {
            throw std::runtime_error("Программа работает только с BMP изображениями с началом координат в левом нижнем углу.");
        }

        data.resize(bmp_info_header.width * bmp_info_header.height * bmp_info_header.bit_count / 8);

        if (bmp_info_header.width % 4 == 0) {
            inp.read((char*)data.data(), data.size());
            file_header.file_size += static_cast<uint32_t>(data.size());
        }
        else {
            uint32_t row_stride = bmp_info_header.width * bmp_info_header.bit_count / 8;
            uint32_t new_stride = (row_stride + 3) & (~3);
            std::vector<uint8_t> padding_row(new_stride - row_stride);

            for (int y = 0; y < bmp_info_header.height; ++y) {
                inp.read((char*)(data.data() + row_stride * y), row_stride);
                inp.read((char*)padding_row.data(), padding_row.size());
            }
            file_header.file_size += static_cast<uint32_t>(data.size()) + bmp_info_header.height * static_cast<uint32_t>(padding_row.size());
        }

        if (bmp_info_header.bit_count == 8) {
            convert_8bit_to_24bit();
        }
    }

    void convert_8bit_to_24bit() {
        if (bmp_info_header.bit_count != 8) return;

        std::vector<uint8_t> new_data;
        new_data.resize(bmp_info_header.width * bmp_info_header.height * 3);

        for (uint32_t y = 0; y < bmp_info_header.height; ++y) {
            for (uint32_t x = 0; x < bmp_info_header.width; ++x) {
                uint8_t index = data[y * bmp_info_header.width + x];
                RGBQuad color = palette[index];
                new_data[(y * bmp_info_header.width + x) * 3] = color.blue;
                new_data[(y * bmp_info_header.width + x) * 3 + 1] = color.green;
                new_data[(y * bmp_info_header.width + x) * 3 + 2] = color.red;
            }
        }

        data = new_data;
        bmp_info_header.bit_count = 24;
        bmp_info_header.size_image = data.size();
        palette.clear();
    }

    void write(const std::string& filename) {
        std::ofstream of{ filename, std::ios_base::binary };
        if (!of) {
            throw std::runtime_error("Не удалось открыть файл для записи.");
        }

        if (bmp_info_header.bit_count == 32) {
            bmp_info_header.size = sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
        }
        else if (bmp_info_header.bit_count == 8) {
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + palette.size() * sizeof(RGBQuad);
        }
        else {
            bmp_info_header.size = sizeof(BMPInfoHeader);
            file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
        }
        file_header.file_size = file_header.offset_data;

        if (bmp_info_header.width % 4 == 0) {
            of.write((const char*)&file_header, sizeof(file_header));
            of.write((const char*)&bmp_info_header, sizeof(bmp_info_header));
            if (bmp_info_header.bit_count == 32) {
                of.write((const char*)&bmp_color_header, sizeof(bmp_color_header));
            }
            else if (bmp_info_header.bit_count == 8) {
                of.write((const char*)palette.data(), palette.size() * sizeof(RGBQuad));
            }

            of.write((const char*)data.data(), data.size());
            file_header.file_size += static_cast<uint32_t>(data.size());
        }
        else {
            uint32_t row_stride = bmp_info_header.width * bmp_info_header.bit_count / 8;
            uint32_t new_stride = (row_stride + 3) & (~3);
            std::vector<uint8_t> padding_row(new_stride - row_stride);

            of.write((const char*)&file_header, sizeof(file_header));
            of.write((const char*)&bmp_info_header, sizeof(bmp_info_header));
            if (bmp_info_header.bit_count == 32) {
                of.write((const char*)&bmp_color_header, sizeof(bmp_color_header));
            }
            else if (bmp_info_header.bit_count == 8) {
                of.write((const char*)palette.data(), palette.size() * sizeof(RGBQuad));
            }

            for (int y = 0; y < bmp_info_header.height; ++y) {
                of.write((const char*)(data.data() + row_stride * y), row_stride);
                of.write((const char*)padding_row.data(), padding_row.size());
            }
            file_header.file_size += static_cast<uint32_t>(data.size()) + bmp_info_header.height * static_cast<uint32_t>(padding_row.size());
        }
    }

    void rotate_nearest(const BMP& input, BMP& output, double angle_rad) {
        int channels = input.bmp_info_header.bit_count / 8;
        int width = input.bmp_info_header.width;
        int height = input.bmp_info_header.height;

        output.bmp_info_header = input.bmp_info_header;
        output.bmp_color_header = input.bmp_color_header;
        output.file_header = input.file_header;
        output.data.resize(width * height * channels, 0);

        double cx = width / 2.0;
        double cy = height / 2.0;
        double cos_theta = cos(angle_rad);
        double sin_theta = sin(angle_rad);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                double x_orig = cx + cos_theta * (x - cx) + sin_theta * (y - cy);
                double y_orig = cy - sin_theta * (x - cx) + cos_theta * (y - cy);

                if (x_orig >= 0 && x_orig < width && y_orig >= 0 && y_orig < height) {
                    int x0 = static_cast<int>(std::round(x_orig));
                    int y0 = static_cast<int>(std::round(y_orig));
                    x0 = std::max(0, std::min(x0, width - 1));
                    y0 = std::max(0, std::min(y0, height - 1));

                    for (int c = 0; c < channels; c++) {
                        output.data[(y * width + x) * channels + c] =
                            input.data[(y0 * width + x0) * channels + c];
                    }
                }
            }
        }
    }

    void rotate_bilinear(const BMP& input, BMP& output, double angle_rad) {
        int channels = input.bmp_info_header.bit_count / 8;
        int width = input.bmp_info_header.width;
        int height = input.bmp_info_header.height;

        output.bmp_info_header = input.bmp_info_header;
        output.bmp_color_header = input.bmp_color_header;
        output.file_header = input.file_header;
        output.data.resize(width * height * channels, 0);

        double cx = width / 2.0;
        double cy = height / 2.0;
        double cos_theta = cos(angle_rad);
        double sin_theta = sin(angle_rad);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                double x_orig = cx + cos_theta * (x - cx) + sin_theta * (y - cy);
                double y_orig = cy - sin_theta * (x - cx) + cos_theta * (y - cy);

                if (x_orig >= 0 && x_orig < width && y_orig >= 0 && y_orig < height) {
                    int x0 = static_cast<int>(std::floor(x_orig));
                    int x1 = x0 + 1;
                    int y0 = static_cast<int>(std::floor(y_orig));
                    int y1 = y0 + 1;

                    x0 = std::max(0, std::min(x0, width - 1));
                    x1 = std::max(0, std::min(x1, width - 1));
                    y0 = std::max(0, std::min(y0, height - 1));
                    y1 = std::max(0, std::min(y1, height - 1));

                    double dx = x_orig - x0;
                    double dy = y_orig - y0;

                    for (int c = 0; c < channels; c++) {
                        double v00 = input.data[(y0 * width + x0) * channels + c];
                        double v10 = input.data[(y0 * width + x1) * channels + c];
                        double v01 = input.data[(y1 * width + x0) * channels + c];
                        double v11 = input.data[(y1 * width + x1) * channels + c];

                        double value = v00 * (1 - dx) * (1 - dy) +
                            v10 * dx * (1 - dy) +
                            v01 * (1 - dx) * dy +
                            v11 * dx * dy;

                        value = std::max(0.0, std::min(255.0, value));
                        output.data[(y * width + x) * channels + c] = static_cast<uint8_t>(std::round(value));
                    }
                }
            }
        }
    }

    void rotate_nearest_full(const BMP& input, BMP& output, double angle_rad, int new_width, int new_height) {
        int channels = input.bmp_info_header.bit_count / 8;
        int old_width = input.bmp_info_header.width;
        int old_height = input.bmp_info_header.height;

        output.bmp_info_header = input.bmp_info_header;
        output.bmp_color_header = input.bmp_color_header;
        output.file_header = input.file_header;
        output.bmp_info_header.width = new_width;
        output.bmp_info_header.height = new_height;
        output.data.resize(new_width * new_height * channels, 0);

        double old_cx = old_width / 2.0;
        double old_cy = old_height / 2.0;
        double new_cx = new_width / 2.0;
        double new_cy = new_height / 2.0;
        double cos_theta = cos(angle_rad);
        double sin_theta = sin(angle_rad);

        for (int y = 0; y < new_height; y++) {
            for (int x = 0; x < new_width; x++) {
                double x_orig = old_cx + cos_theta * (x - new_cx) + sin_theta * (y - new_cy);
                double y_orig = old_cy - sin_theta * (x - new_cx) + cos_theta * (y - new_cy);

                if (x_orig >= 0 && x_orig < old_width && y_orig >= 0 && y_orig < old_height) {
                    int x0 = static_cast<int>(std::round(x_orig));
                    int y0 = static_cast<int>(std::round(y_orig));
                    x0 = std::max(0, std::min(x0, old_width - 1));
                    y0 = std::max(0, std::min(y0, old_height - 1));

                    for (int c = 0; c < channels; c++) {
                        output.data[(y * new_width + x) * channels + c] =
                            input.data[(y0 * old_width + x0) * channels + c];
                    }
                }
            }
        }
    }

    void rotate_bilinear_full(const BMP& input, BMP& output, double angle_rad, int new_width, int new_height) {
        int channels = input.bmp_info_header.bit_count / 8;
        int old_width = input.bmp_info_header.width;
        int old_height = input.bmp_info_header.height;

        output.bmp_info_header = input.bmp_info_header;
        output.bmp_color_header = input.bmp_color_header;
        output.file_header = input.file_header;
        output.bmp_info_header.width = new_width;
        output.bmp_info_header.height = new_height;
        output.data.resize(new_width * new_height * channels, 0);

        double old_cx = old_width / 2.0;
        double old_cy = old_height / 2.0;
        double new_cx = new_width / 2.0;
        double new_cy = new_height / 2.0;
        double cos_theta = cos(angle_rad);
        double sin_theta = sin(angle_rad);

        for (int y = 0; y < new_height; y++) {
            for (int x = 0; x < new_width; x++) {
                double x_orig = old_cx + cos_theta * (x - new_cx) + sin_theta * (y - new_cy);
                double y_orig = old_cy - sin_theta * (x - new_cx) + cos_theta * (y - new_cy);

                if (x_orig >= 0 && x_orig < old_width && y_orig >= 0 && y_orig < old_height) {
                    int x0 = static_cast<int>(std::floor(x_orig));
                    int x1 = x0 + 1;
                    int y0 = static_cast<int>(std::floor(y_orig));
                    int y1 = y0 + 1;

                    x0 = std::max(0, std::min(x0, old_width - 1));
                    x1 = std::max(0, std::min(x1, old_width - 1));
                    y0 = std::max(0, std::min(y0, old_height - 1));
                    y1 = std::max(0, std::min(y1, old_height - 1));

                    double dx = x_orig - x0;
                    double dy = y_orig - y0;

                    for (int c = 0; c < channels; c++) {
                        double v00 = input.data[(y0 * old_width + x0) * channels + c];
                        double v10 = input.data[(y0 * old_width + x1) * channels + c];
                        double v01 = input.data[(y1 * old_width + x0) * channels + c];
                        double v11 = input.data[(y1 * old_width + x1) * channels + c];

                        double value = v00 * (1 - dx) * (1 - dy) +
                            v10 * dx * (1 - dy) +
                            v01 * (1 - dx) * dy +
                            v11 * dx * dy;

                        value = std::max(0.0, std::min(255.0, value));
                        output.data[(y * new_width + x) * channels + c] = static_cast<uint8_t>(std::round(value));
                    }
                }
            }
        }
    }
};

// =========== Многопоточный поворот (разделение по пикселям) ===========
struct ThreadData {
    BMP* input;
    BMP* output;
    double old_cx;
    double old_cy;
    double new_cx;
    double new_cy;
    double cos_theta;
    double sin_theta;
    int new_width;
    int new_height;
    int thread_id;
    int num_threads;
};

void rotate_bilinear_thread(ThreadData* data) {
    BMP* input = data->input;
    BMP* output = data->output;
    double old_cx = data->old_cx;
    double old_cy = data->old_cy;
    double new_cx = data->new_cx;
    double new_cy = data->new_cy;
    double cos_theta = data->cos_theta;
    double sin_theta = data->sin_theta;
    int new_width = data->new_width;
    int new_height = data->new_height;
    int thread_id = data->thread_id;
    int num_threads = data->num_threads;

    int old_width = input->bmp_info_header.width;
    int old_height = input->bmp_info_header.height;
    int channels = input->bmp_info_header.bit_count / 8;

    int total_pixels = new_width * new_height;
    for (int index = thread_id; index < total_pixels; index += num_threads) {
        int y = index / new_width;
        int x = index % new_width;

        double x_orig = old_cx + cos_theta * (x - new_cx) + sin_theta * (y - new_cy);
        double y_orig = old_cy - sin_theta * (x - new_cx) + cos_theta * (y - new_cy);

        if (x_orig >= 0 && x_orig < old_width && y_orig >= 0 && y_orig < old_height) {
            int x0 = static_cast<int>(std::floor(x_orig));
            int x1 = x0 + 1;
            int y0 = static_cast<int>(std::floor(y_orig));
            int y1 = y0 + 1;

            x0 = std::max(0, std::min(x0, old_width - 1));
            x1 = std::max(0, std::min(x1, old_width - 1));
            y0 = std::max(0, std::min(y0, old_height - 1));
            y1 = std::max(0, std::min(y1, old_height - 1));

            double dx = x_orig - x0;
            double dy = y_orig - y0;

            for (int c = 0; c < channels; c++) {
                double v00 = input->data[(y0 * old_width + x0) * channels + c];
                double v10 = input->data[(y0 * old_width + x1) * channels + c];
                double v01 = input->data[(y1 * old_width + x0) * channels + c];
                double v11 = input->data[(y1 * old_width + x1) * channels + c];

                double value = v00 * (1 - dx) * (1 - dy) +
                    v10 * dx * (1 - dy) +
                    v01 * (1 - dx) * dy +
                    v11 * dx * dy;

                value = std::max(0.0, std::min(255.0, value));
                output->data[(y * new_width + x) * channels + c] = static_cast<uint8_t>(std::round(value));
            }
        }
    }
}

// =========== Многопоточный блочный поворот ===========
struct TileThreadData {
    BMP* input;
    BMP* output;
    double old_cx;
    double old_cy;
    double new_cx;
    double new_cy;
    double cos_theta;
    double sin_theta;
    int new_width;
    int new_height;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
};

void rotate_bilinear_tile(TileThreadData* data) {
    BMP* input = data->input;
    BMP* output = data->output;
    double old_cx = data->old_cx;
    double old_cy = data->old_cy;
    double new_cx = data->new_cx;
    double new_cy = data->new_cy;
    double cos_theta = data->cos_theta;
    double sin_theta = data->sin_theta;
    int new_width = data->new_width;
    int new_height = data->new_height;
    int start_x = data->start_x;
    int start_y = data->start_y;
    int end_x = data->end_x;
    int end_y = data->end_y;

    int old_width = input->bmp_info_header.width;
    int old_height = input->bmp_info_header.height;
    int channels = input->bmp_info_header.bit_count / 8;

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            double x_orig = old_cx + cos_theta * (x - new_cx) + sin_theta * (y - new_cy);
            double y_orig = old_cy - sin_theta * (x - new_cx) + cos_theta * (y - new_cy);

            if (x_orig >= 0 && x_orig < old_width && y_orig >= 0 && y_orig < old_height) {
                int x0 = static_cast<int>(std::floor(x_orig));
                int x1 = x0 + 1;
                int y0 = static_cast<int>(std::floor(y_orig));
                int y1 = y0 + 1;

                x0 = std::max(0, std::min(x0, old_width - 1));
                x1 = std::max(0, std::min(x1, old_width - 1));
                y0 = std::max(0, std::min(y0, old_height - 1));
                y1 = std::max(0, std::min(y1, old_height - 1));

                double dx = x_orig - x0;
                double dy = y_orig - y0;

                for (int c = 0; c < channels; c++) {
                    double v00 = input->data[(y0 * old_width + x0) * channels + c];
                    double v10 = input->data[(y0 * old_width + x1) * channels + c];
                    double v01 = input->data[(y1 * old_width + x0) * channels + c];
                    double v11 = input->data[(y1 * old_width + x1) * channels + c];

                    double value = v00 * (1 - dx) * (1 - dy) +
                        v10 * dx * (1 - dy) +
                        v01 * (1 - dx) * dy +
                        v11 * dx * dy;

                    value = std::max(0.0, std::min(255.0, value));
                    output->data[(y * new_width + x) * channels + c] = static_cast<uint8_t>(std::round(value));
                }
            }
        }
    }
}

// =========== Вспомогательные функции ===========
std::pair<int, int> calculate_new_size(int old_width, int old_height, double cos_theta, double sin_theta) {
    double w = old_width;
    double h = old_height;

    double corners[4][2] = {
        { -w / 2, -h / 2 },
        { w / 2, -h / 2 },
        { w / 2, h / 2 },
        { -w / 2, h / 2 }
    };

    double max_abs_x = 0;
    double max_abs_y = 0;
    for (int i = 0; i < 4; i++) {
        double x = corners[i][0];
        double y = corners[i][1];
        double x_rot = x * cos_theta + y * sin_theta;
        double y_rot = -x * sin_theta + y * cos_theta;

        double abs_x = std::abs(x_rot);
        double abs_y = std::abs(y_rot);
        if (abs_x > max_abs_x) max_abs_x = abs_x;
        if (abs_y > max_abs_y) max_abs_y = abs_y;
    }

    int new_width = static_cast<int>(std::ceil(2 * max_abs_x));
    int new_height = static_cast<int>(std::ceil(2 * max_abs_y));

    return { new_width, new_height };
}

void print_transformation_matrix(double cos_theta, double sin_theta, double cx, double cy) {
    double a11 = cos_theta;
    double a12 = sin_theta;
    double a13 = cx * (1 - cos_theta) - cy * sin_theta;
    double a21 = -sin_theta;
    double a22 = cos_theta;
    double a23 = cy * (1 - cos_theta) + cx * sin_theta;

    std::cout << "\nОбратная матрица преобразования:\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "[" << a11 << " " << a12 << " " << a13 << "]\n";
    std::cout << "[" << a21 << " " << a22 << " " << a23 << "]\n";
    std::cout << "[0.000000 0.000000 1.000000]\n";
}

// =========== Основные функции поворотов ===========
void affine_rotation_cropped(BMP& input_image, double angle_rad, const std::string& base_name) {
    BMP output_nn, output_bln;

    auto start = std::chrono::high_resolution_clock::now();
    output_nn.rotate_nearest(input_image, output_nn, angle_rad);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "\nВремя поворота (ближайший сосед): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_nn_name = base_name + "_rotate_nn.bmp";
    output_nn.write(output_nn_name);
    std::cout << "Изображение сохранено как: " << output_nn_name << std::endl;

    start = std::chrono::high_resolution_clock::now();
    output_bln.rotate_bilinear(input_image, output_bln, angle_rad);
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Время поворота (билинейная): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_bln_name = base_name + "_rotate_bln.bmp";
    output_bln.write(output_bln_name);
    std::cout << "Изображение сохранено как: " << output_bln_name << std::endl;
}

void affine_rotation_full(BMP& input_image, double angle_rad, const std::string& base_name) {
    double cos_theta = cos(angle_rad);
    double sin_theta = sin(angle_rad);

    auto [new_width, new_height] = calculate_new_size(
        input_image.bmp_info_header.width,
        input_image.bmp_info_header.height,
        cos_theta, sin_theta
    );

    BMP output_nn, output_bln;

    auto start = std::chrono::high_resolution_clock::now();
    output_nn.rotate_nearest_full(input_image, output_nn, angle_rad, new_width, new_height);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "\nВремя поворота (ближайший сосед): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_nn_name = base_name + "_rotate_nn_full.bmp";
    output_nn.write(output_nn_name);
    std::cout << "Изображение сохранено как: " << output_nn_name << std::endl;

    start = std::chrono::high_resolution_clock::now();
    output_bln.rotate_bilinear_full(input_image, output_bln, angle_rad, new_width, new_height);
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Время поворота (билинейная): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_bln_name = base_name + "_rotate_bln_full.bmp";
    output_bln.write(output_bln_name);
    std::cout << "Изображение сохранено как: " << output_bln_name << std::endl;
}

void affine_rotation_multithreaded(BMP& input_image, double angle_rad, const std::string& base_name) {
    int num_threads;
    std::cout << "Введите количество потоков: ";
    std::cin >> num_threads;

    double cos_theta = cos(angle_rad);
    double sin_theta = sin(angle_rad);

    auto [new_width, new_height] = calculate_new_size(
        input_image.bmp_info_header.width,
        input_image.bmp_info_header.height,
        cos_theta, sin_theta
    );

    BMP output;
    output.bmp_info_header = input_image.bmp_info_header;
    output.bmp_color_header = input_image.bmp_color_header;
    output.file_header = input_image.file_header;
    output.bmp_info_header.width = new_width;
    output.bmp_info_header.height = new_height;
    int channels = input_image.bmp_info_header.bit_count / 8;
    output.data.resize(new_width * new_height * channels, 0);

    double old_cx = input_image.bmp_info_header.width / 2.0;
    double old_cy = input_image.bmp_info_header.height / 2.0;
    double new_cx = new_width / 2.0;
    double new_cy = new_height / 2.0;

    std::vector<std::thread> threads;
    std::vector<ThreadData> thread_data(num_threads);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i] = {
            &input_image,
            &output,
            old_cx,
            old_cy,
            new_cx,
            new_cy,
            cos_theta,
            sin_theta,
            new_width,
            new_height,
            i,
            num_threads
        };
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_threads; i++) {
        threads.push_back(std::thread(rotate_bilinear_thread, &thread_data[i]));
    }
    for (auto& t : threads) {
        t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Время поворота (билинейная, " << num_threads << " потоков): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_name = base_name + "_rotate_multithreaded.bmp";
    output.write(output_name);
    std::cout << "Изображение сохранено как: " << output_name << std::endl;
}

void affine_rotation_tiled(BMP& input_image, double angle_rad, const std::string& base_name) {
    int tile_size;
    std::cout << "Введите размер тайла: ";
    std::cin >> tile_size;

    double cos_theta = cos(angle_rad);
    double sin_theta = sin(angle_rad);

    auto [new_width, new_height] = calculate_new_size(
        input_image.bmp_info_header.width,
        input_image.bmp_info_header.height,
        cos_theta, sin_theta
    );

    BMP output;
    output.bmp_info_header = input_image.bmp_info_header;
    output.bmp_color_header = input_image.bmp_color_header;
    output.file_header = input_image.file_header;
    output.bmp_info_header.width = new_width;
    output.bmp_info_header.height = new_height;
    int channels = input_image.bmp_info_header.bit_count / 8;
    output.data.resize(new_width * new_height * channels, 0);

    double old_cx = input_image.bmp_info_header.width / 2.0;
    double old_cy = input_image.bmp_info_header.height / 2.0;
    double new_cx = new_width / 2.0;
    double new_cy = new_height / 2.0;

    int total_tiles_x = (new_width + tile_size - 1) / tile_size;
    int total_tiles_y = (new_height + tile_size - 1) / tile_size;
    int total_tiles = total_tiles_x * total_tiles_y;

    std::cout << "Общее количество тайлов: " << total_tiles << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int tile_y = 0; tile_y < total_tiles_y; ++tile_y) {
        for (int tile_x = 0; tile_x < total_tiles_x; ++tile_x) {
            int start_x = tile_x * tile_size;
            int end_x = std::min(start_x + tile_size, new_width);
            int start_y = tile_y * tile_size;
            int end_y = std::min(start_y + tile_size, new_height);

            TileThreadData tile_data = {
                &input_image,
                &output,
                old_cx,
                old_cy,
                new_cx,
                new_cy,
                cos_theta,
                sin_theta,
                new_width,
                new_height,
                start_x,
                start_y,
                end_x,
                end_y
            };

            rotate_bilinear_tile(&tile_data);

            int processed = tile_y * total_tiles_x + tile_x + 1;
            std::cout << "Обработан тайл " << processed << " из " << total_tiles << std::endl;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Время поворота (блочный метод): "
        << std::fixed << std::setprecision(3) << elapsed.count() << " с\n";

    std::string output_name = base_name + "_rotate_tiled.bmp";
    output.write(output_name);
    std::cout << "Изображение сохранено как: " << output_name << std::endl;
}

// =========== Главное меню ===========
void show_main_menu() {
    std::cout << "\n========== ГЛАВНОЕ МЕНЮ ==========\n";
    std::cout << "0. Выход из программы\n";
    std::cout << "1. Аффинный поворот\n";
    std::cout << "Выберите пункт меню: ";
}

void show_rotation_menu() {
    std::cout << "\n========== ВЫБОР ТИПА ПОВОРОТА ==========\n";
    std::cout << "1. Аффинный поворот обрезанный\n";
    std::cout << "2. Аффинный поворот полный\n";
    std::cout << "3. Аффинный поворот многопоточный\n";
    std::cout << "4. Аффинный поворот многопоточный блочный\n";
    std::cout << "Выберите тип поворота: ";
}

bool ask_another_rotation() {
    char choice;
    std::cout << "\nХотите сделать другой поворот? [Y/N]: ";
    std::cin >> choice;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return (choice == 'Y' || choice == 'y');
}

int main() {
    setlocale(LC_ALL, "RU");

    try {
        int main_choice;

        do {
            show_main_menu();
            std::cin >> main_choice;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (main_choice == 0) {
                std::cout << "Выход из программы.\n";
                break;
            }
            else if (main_choice == 1) {
                std::string input_filename;
                std::cout << "Введите имя входного BMP файла: ";
                std::cin >> input_filename;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                BMP input_image;
                input_image.read(input_filename);

                size_t lastdot = input_filename.find_last_of(".");
                std::string base_name = (lastdot != std::string::npos) ?
                    input_filename.substr(0, lastdot) : input_filename;

                bool another_rotation = true;
                while (another_rotation) {
                    int rotation_choice;
                    show_rotation_menu();
                    std::cin >> rotation_choice;
                    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                    double rotation_angle;
                    std::cout << "Введите угол поворота (в градусах): ";
                    std::cin >> rotation_angle;
                    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                    double angle_rad = rotation_angle * M_PI / 180.0;
                    double cos_theta = cos(angle_rad);
                    double sin_theta = sin(angle_rad);

                    print_transformation_matrix(cos_theta, sin_theta,
                        input_image.bmp_info_header.width / 2.0,
                        input_image.bmp_info_header.height / 2.0);

                    switch (rotation_choice) {
                    case 1:
                        affine_rotation_cropped(input_image, angle_rad, base_name);
                        break;
                    case 2:
                        affine_rotation_full(input_image, angle_rad, base_name);
                        break;
                    case 3:
                        affine_rotation_multithreaded(input_image, angle_rad, base_name);
                        break;
                    case 4:
                        affine_rotation_tiled(input_image, angle_rad, base_name);
                        break;
                    default:
                        std::cout << "Неверный выбор!\n";
                        break;
                    }

                    another_rotation = ask_another_rotation();
                }
            }
            else {
                std::cout << "Неверный выбор!\n";
            }

        } while (true);
    }
    catch (const std::exception& e) {
        std::cerr << "\n!!! ОШИБКА: " << e.what() << " !!!" << std::endl;
        return 1;
    }

    return 0;
}
