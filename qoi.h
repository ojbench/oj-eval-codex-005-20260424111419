#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0; 
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {

    // qoi-header part

    // write magic bytes "qoif"
    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    // write image width
    QoiWriteU32(width);
    // write image height
    QoiWriteU32(height);
    // write channel number
    QoiWriteU8(channels);
    // write color space specifier
    QoiWriteU8(colorspace);

    /* qoi-data part */
    int run = 0;
    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;
    uint8_t pre_r, pre_g, pre_b, pre_a;
    pre_r = 0u;
    pre_g = 0u;
    pre_b = 0u;
    pre_a = 255u;

    for (int i = 0; i < px_num; ++i) {
        r = QoiReadU8();
        g = QoiReadU8();
        b = QoiReadU8();
        if (channels == 4) a = QoiReadU8();

// Encoding priority: RUN > INDEX > DIFF > LUMA > RGB/RGBA
        if (r == pre_r && g == pre_g && b == pre_b && a == pre_a) {
            // continue run
            run++;
            if (run == 62 || i == px_num - 1) {
                // flush run (run-1 encoded in lower 6 bits)
                uint8_t op = static_cast<uint8_t>(QOI_OP_RUN_TAG | (uint8_t)(run - 1));
                QoiWriteU8(op);
                run = 0;
            }
        } else {
            // if we had an ongoing run, flush it first
            if (run > 0) {
                uint8_t op = static_cast<uint8_t>(QOI_OP_RUN_TAG | (uint8_t)(run - 1));
                QoiWriteU8(op);
                run = 0;
            }

            int idx = QoiColorHash(r, g, b, a);
            if (history[idx][0] == r && history[idx][1] == g && history[idx][2] == b && history[idx][3] == a) {
                // INDEX
                uint8_t op = static_cast<uint8_t>(QOI_OP_INDEX_TAG | (idx & 0x3f));
                QoiWriteU8(op);
            } else {
                // Try DIFF/LUMA if alpha unchanged
                if (a == pre_a) {
                    int dr = (int)r - (int)pre_r;
                    int dg = (int)g - (int)pre_g;
                    int db = (int)b - (int)pre_b;

                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                        // DIFF
                        uint8_t op = static_cast<uint8_t>(QOI_OP_DIFF_TAG |
                            (((dr + 2) & 0x03) << 4) |
                            (((dg + 2) & 0x03) << 2) |
                            ((db + 2) & 0x03));
                        QoiWriteU8(op);
                    } else {
                        int dg_l = dg;
                        int dr_dg = dr - dg_l;
                        int db_dg = db - dg_l;
                        if (dg_l >= -32 && dg_l <= 31 && dr_dg >= -8 && dr_dg <= 7 && db_dg >= -8 && db_dg <= 7) {
                            // LUMA
                            uint8_t b1 = static_cast<uint8_t>(QOI_OP_LUMA_TAG | ((dg_l + 32) & 0x3f));
                            uint8_t b2 = static_cast<uint8_t>(((dr_dg + 8) & 0x0f) << 4 | ((db_dg + 8) & 0x0f));
                            QoiWriteU8(b1);
                            QoiWriteU8(b2);
                        } else {
                            // RGB (alpha unchanged)
                            QoiWriteU8(QOI_OP_RGB_TAG);
                            QoiWriteU8(r);
                            QoiWriteU8(g);
                            QoiWriteU8(b);
                        }
                    }
                } else {
                    // RGBA (alpha changed)
                    QoiWriteU8(QOI_OP_RGBA_TAG);
                    QoiWriteU8(r);
                    QoiWriteU8(g);
                    QoiWriteU8(b);
                    QoiWriteU8(a);
                }

                // update history with the new color
                history[idx][0] = r;
                history[idx][1] = g;
                history[idx][2] = b;
                history[idx][3] = a;
            }
        }

        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;
    }

    // qoi-padding part
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        QoiWriteU8(QOI_PADDING[i]);
    }

    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {

    char c1 = QoiReadChar();
    char c2 = QoiReadChar();
    char c3 = QoiReadChar();
    char c4 = QoiReadChar();
    if (c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') {
        return false;
    }

    // read image width
    width = QoiReadU32();
    // read image height
    height = QoiReadU32();
    // read channel number
    channels = QoiReadU8();
    // read color space specifier
    colorspace = QoiReadU8();

    int run = 0;
    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;

    for (int i = 0; i < px_num; ++i) {

// previous pixel starts at (0,0,0,255). We keep r,g,b,a as current/prev.
    r = 0u; g = 0u; b = 0u; a = 255u;
    for (int i = 0; i < px_num; ++i) {
        if (run > 0) {
            // repeat previous pixel
            run--;
        } else {
            uint8_t b1 = QoiReadU8();
            if (b1 == QOI_OP_RGB_TAG) {
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
            } else if (b1 == QOI_OP_RGBA_TAG) {
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
                a = QoiReadU8();
            } else {
                uint8_t tag = b1 & QOI_MASK_2;
                if (tag == QOI_OP_INDEX_TAG) {
                    int idx = b1 & 0x3f;
                    r = history[idx][0];
                    g = history[idx][1];
                    b = history[idx][2];
                    a = history[idx][3];
                } else if (tag == QOI_OP_DIFF_TAG) {
                    int dr = ((b1 >> 4) & 0x03) - 2;
                    int dg = ((b1 >> 2) & 0x03) - 2;
                    int db = (b1 & 0x03) - 2;
                    r = static_cast<uint8_t>(r + dr);
                    g = static_cast<uint8_t>(g + dg);
                    b = static_cast<uint8_t>(b + db);
                } else if (tag == QOI_OP_LUMA_TAG) {
                    uint8_t b2 = QoiReadU8();
                    int dg = (b1 & 0x3f) - 32;
                    int dr_dg = ((b2 >> 4) & 0x0f) - 8;
                    int db_dg = (b2 & 0x0f) - 8;
                    int dr = dr_dg + dg;
                    int dbv = db_dg + dg;
                    r = static_cast<uint8_t>(r + dr);
                    g = static_cast<uint8_t>(g + dg);
                    b = static_cast<uint8_t>(b + dbv);
                } else if (tag == QOI_OP_RUN_TAG) {
                    run = (b1 & 0x3f) + 1; // includes this pixel
                    // current pixel stays the same as previous
                    run--; // account for this iteration
                }
            }
        }

        int idx = QoiColorHash(r, g, b, a);
        history[idx][0] = r;
        history[idx][1] = g;
        history[idx][2] = b;
        history[idx][3] = a;

        QoiWriteU8(r);
        QoiWriteU8(g);
        QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
    }

        QoiWriteU8(r);
        QoiWriteU8(g);
        QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
    }

    bool valid = true;
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        if (QoiReadU8() != QOI_PADDING[i]) valid = false;
    }

    return valid;
}

#endif // QOI_FORMAT_CODEC_QOI_H_
