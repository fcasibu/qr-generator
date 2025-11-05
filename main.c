#include <correct.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define QR_SIZE 21

struct bit_stream {
    uint8_t bits[256];
    int length;
};

struct qr_matrix {
    uint8_t data[QR_SIZE][QR_SIZE];
    uint8_t reserved[QR_SIZE][QR_SIZE];
};

int char_to_alphanum(char c) {
    static const char *alpha_numeric =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

    for (int i = 0; alpha_numeric[i] != '\0'; ++i) {
        if (alpha_numeric[i] == c)
            return i;
    }

    return -1;
}

void append_bits(struct bit_stream *bs, uint16_t value, int num_bits) {
    for (int i = num_bits - 1; i >= 0; --i) {
        bs->bits[bs->length++] = (value >> i) & 1;
    }
}

void bits_to_bytes(const struct bit_stream *bs, uint8_t *bytes, int num_bytes) {
    for (int i = 0; i < num_bytes; ++i) {
        uint8_t byte_value = 0;

        for (int j = 0; j < 8; ++j) {
            int bit_index = i * 8 + j;

            if (bit_index < bs->length) {
                byte_value |= (bs->bits[bit_index] << (7 - j));
            }
        }

        bytes[i] = byte_value;
    }
}

bool is_numeric(const char *input) {
    int len = strlen(input);

    for (int i = 0; i < len; ++i) {
        if (!isdigit(input[i]))
            return false;
    }

    return true;
}

bool is_alphanumeric(const char *input) {
    int len = strlen(input);

    for (int i = 0; i < len; ++i) {
        if (char_to_alphanum(input[i]) == -1)
            return false;
    }

    return true;
}

void encode_numeric(struct bit_stream *bs, const char *input) {
    int i = 0;
    int input_len = strlen(input);

    while (i + 2 < input_len) {
        int bits = (input[i] - '0') * 100 + (input[i + 1] - '0') * 10 +
                   (input[i + 2] - '0');
        append_bits(bs, bits, 10);
        i += 3;
    }

    int remaining = input_len - i;
    if (remaining == 2) {
        int bits = (input[i] - '0') * 10 + (input[i + 1] - '0');
        append_bits(bs, bits, 7);
    } else if (remaining == 1) {
        int bits = (input[i] - '0');
        append_bits(bs, bits, 4);
    }
}

void encode_alphanumeric(struct bit_stream *bs, const char *input) {
    int i = 0;
    int input_len = strlen(input);

    while (i + 1 < input_len) {
        int bits =
            (char_to_alphanum(input[i]) * 45) + char_to_alphanum(input[i + 1]);
        append_bits(bs, bits, 11);
        i += 2;
    }

    int remaining = input_len - i;

    if (remaining == 1) {
        append_bits(bs, char_to_alphanum(input[i]), 6);
    }
}

void encode_byte(struct bit_stream *bs, const char *input) {
    int len = strlen(input);
    for (int i = 0; i < len; ++i) {
        append_bits(bs, input[i], 8);
    }
}

void encode_data(struct bit_stream *bs, const char *input) {
    int input_len = strlen(input);

    if (is_alphanumeric(input)) {
        append_bits(bs, 0b0010, 4);
        append_bits(bs, input_len, 9);
        encode_alphanumeric(bs, input);
    } else if (is_numeric(input)) {
        append_bits(bs, 0b0001, 4);
        append_bits(bs, input_len, 10);
        encode_numeric(bs, input);
    } else {
        append_bits(bs, 0b0100, 4);
        append_bits(bs, input_len, 8);
        encode_byte(bs, input);
    }

    append_bits(bs, 0, 4);

    int remaining = 8 - (bs->length % 8);
    if (remaining != 8) {
        append_bits(bs, 0, remaining);
    }

    static const uint8_t PADS[2] = {0xEC, 0x11};
    int pad_index = 0;
    while (bs->length <
           152) { // only supporting version 1 with error correction level L
        append_bits(bs, PADS[pad_index], 8);
        pad_index ^= 1;
    }
}

void place_finder_pattern(struct qr_matrix *qr, int r, int c) {
    for (int dr = -1; dr <= 7; ++dr) {
        for (int dc = -1; dc <= 7; ++dc) {
            int row = r + dr;
            int col = c + dc;
            if (row < 0 || row >= QR_SIZE || col < 0 || col >= QR_SIZE)
                continue;
            qr->reserved[row][col] = 1;
            if (dr >= 0 && dr <= 6 && dc >= 0 && dc <= 6) {
                if (dr == 0 || dr == 6 || dc == 0 || dc == 6 ||
                    (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4))
                    qr->data[row][col] = 1;
                else
                    qr->data[row][col] = 0;
            } else {
                qr->data[row][col] = 0;
            }
        }
    }
}

void place_timing_patterns(struct qr_matrix *qr) {
    for (int i = 8; i < QR_SIZE - 8; ++i) {
        qr->data[6][i] = i % 2;
        qr->reserved[6][i] = 1;
        qr->data[i][6] = i % 2;
        qr->reserved[i][6] = 1;
    }
}

void place_dark_module(struct qr_matrix *qr) {
    qr->data[13][8] = 1;
    qr->reserved[13][8] = 1;
}

void reserve_format_areas(struct qr_matrix *qr) {
    // top-left
    for (int i = 0; i <= 8; ++i) {
        if (i != 6)
            qr->reserved[8][i] = 1;
    }
    for (int i = 0; i <= 8; ++i) {
        if (i != 6)
            qr->reserved[i][8] = 1;
    }
    // top-right
    for (int i = QR_SIZE - 8; i < QR_SIZE; ++i)
        qr->reserved[8][i] = 1;
    // bottom-left
    for (int i = QR_SIZE - 7; i < QR_SIZE; ++i)
        qr->reserved[i][8] = 1;
}

void place_data_bits(struct qr_matrix *qr, uint8_t *codewords,
                     int num_codewords) {
    int total_bits = num_codewords * 8;
    int bit_idx = 0;
    int dir = -1;

    for (int c = QR_SIZE - 1; c >= 0; c -= 2) {
        if (c == 6)
            c--;

        for (int r = 0; r < QR_SIZE; ++r) {
            int row = (dir == -1) ? QR_SIZE - 1 - r : r;

            for (int cc = 0; cc < 2; ++cc) {
                int col = c - cc;

                if (qr->reserved[row][col])
                    continue;

                if (bit_idx >= total_bits) {
                    qr->data[row][col] = 0;
                } else {
                    int byte_idx = bit_idx / 8;
                    int bit_pos = 7 - (bit_idx % 8);
                    int bit = (codewords[byte_idx] >> bit_pos) & 1;
                    qr->data[row][col] = bit;
                    bit_idx++;
                }
            }
        }

        dir *= -1;
    }
}

static inline bool mask_condition(int mask_id, int r, int c) {
    switch (mask_id) {
    case 0:
        return ((r + c) % 2) == 0;
    case 1:
        return (r % 2) == 0;
    case 2:
        return (c % 3) == 0;
    case 3:
        return ((r + c) % 3) == 0;
    case 4:
        return (((r / 2) + (c / 3)) % 2) == 0;
    case 5:
        return (((r * c) % 2) + ((r * c) % 3)) == 0;
    case 6:
        return ((((r * c) % 2) + ((r * c) % 3)) % 2) == 0;
    case 7:
        return ((((r + c) % 2) + ((r * c) % 3)) % 2) == 0;
    default:
        return false;
    }
}

void apply_mask_pattern(struct qr_matrix *qr, int mask_id) {
    for (int r = 0; r < QR_SIZE; ++r) {
        for (int c = 0; c < QR_SIZE; ++c) {
            if (qr->reserved[r][c])
                continue;
            if (mask_condition(mask_id, r, c))
                qr->data[r][c] ^= 1;
        }
    }
}

static int penalty_rule1(const uint8_t data[QR_SIZE][QR_SIZE]) {
    int score = 0;

    for (int r = 0; r < QR_SIZE; ++r) {
        int run_color = data[r][0];
        int run_len = 1;
        for (int c = 1; c < QR_SIZE; ++c) {
            if (data[r][c] == run_color) {
                run_len++;
            } else {
                if (run_len >= 5)
                    score += 3 + (run_len - 5);
                run_color = data[r][c];
                run_len = 1;
            }
        }
        if (run_len >= 5)
            score += 3 + (run_len - 5);
    }

    for (int c = 0; c < QR_SIZE; ++c) {
        int run_color = data[0][c];
        int run_len = 1;
        for (int r = 1; r < QR_SIZE; ++r) {
            if (data[r][c] == run_color) {
                run_len++;
            } else {
                if (run_len >= 5)
                    score += 3 + (run_len - 5);
                run_color = data[r][c];
                run_len = 1;
            }
        }
        if (run_len >= 5)
            score += 3 + (run_len - 5);
    }

    return score;
}

static int penalty_rule2(const uint8_t data[QR_SIZE][QR_SIZE]) {
    int score = 0;
    for (int r = 0; r < QR_SIZE - 1; ++r) {
        for (int c = 0; c < QR_SIZE - 1; ++c) {
            uint8_t v = data[r][c];
            if (data[r][c + 1] == v && data[r + 1][c] == v &&
                data[r + 1][c + 1] == v)
                score += 3;
        }
    }
    return score;
}

static inline bool is_light(uint8_t v) { return v == 0; }

static int penalty_rule3(const uint8_t data[QR_SIZE][QR_SIZE]) {
    int score = 0;
    // Pattern: 1 0 1 1 1 0 1 with 4+ light modules on either side

    for (int r = 0; r < QR_SIZE; ++r) {
        for (int c = 0; c <= QR_SIZE - 7; ++c) {
            if (data[r][c] == 1 && data[r][c + 1] == 0 && data[r][c + 2] == 1 &&
                data[r][c + 3] == 1 && data[r][c + 4] == 1 &&
                data[r][c + 5] == 0 && data[r][c + 6] == 1) {
                bool left_ok = false;
                bool right_ok = false;

                if (c - 4 >= 0) {
                    left_ok =
                        is_light(data[r][c - 1]) && is_light(data[r][c - 2]) &&
                        is_light(data[r][c - 3]) && is_light(data[r][c - 4]);
                } else {
                    left_ok = true;
                }

                if (c + 7 + 3 < QR_SIZE) {
                    right_ok =
                        is_light(data[r][c + 7]) && is_light(data[r][c + 8]) &&
                        is_light(data[r][c + 9]) && is_light(data[r][c + 10]);
                } else {
                    right_ok = true;
                }

                if (left_ok || right_ok)
                    score += 40;
            }
        }
    }

    for (int c = 0; c < QR_SIZE; ++c) {
        for (int r = 0; r <= QR_SIZE - 7; ++r) {
            if (data[r][c] == 1 && data[r + 1][c] == 0 && data[r + 2][c] == 1 &&
                data[r + 3][c] == 1 && data[r + 4][c] == 1 &&
                data[r + 5][c] == 0 && data[r + 6][c] == 1) {
                bool up_ok = false;
                bool down_ok = false;

                if (r - 4 >= 0) {
                    up_ok =
                        is_light(data[r - 1][c]) && is_light(data[r - 2][c]) &&
                        is_light(data[r - 3][c]) && is_light(data[r - 4][c]);
                } else {
                    up_ok = true;
                }

                if (r + 7 + 3 < QR_SIZE) {
                    down_ok =
                        is_light(data[r + 7][c]) && is_light(data[r + 8][c]) &&
                        is_light(data[r + 9][c]) && is_light(data[r + 10][c]);
                } else {
                    down_ok = true;
                }

                if (up_ok || down_ok)
                    score += 40;
            }
        }
    }

    return score;
}

int penalty_rule4(const uint8_t data[QR_SIZE][QR_SIZE]) {
    int dark = 0;
    int total = QR_SIZE * QR_SIZE;

    for (int r = 0; r < QR_SIZE; ++r) {
        for (int c = 0; c < QR_SIZE; ++c) {
            if (data[r][c])
                dark++;
        }
    }

    int percent = (dark * 100 + total / 2) / total;
    int diff = percent > 50 ? percent - 50 : 50 - percent;

    return (diff / 5) * 10;
}

int compute_penalty(const uint8_t data[QR_SIZE][QR_SIZE]) {
    int score = 0;

    score += penalty_rule1(data);
    score += penalty_rule2(data);
    score += penalty_rule3(data);
    score += penalty_rule4(data);

    return score;
}

int choose_best_mask(const struct qr_matrix *qr) {
    int best_mask = 0;
    int best_score = -1;

    for (int mask = 0; mask < 8; ++mask) {
        uint8_t temp[QR_SIZE][QR_SIZE];

        for (int r = 0; r < QR_SIZE; ++r) {
            for (int c = 0; c < QR_SIZE; ++c)
                temp[r][c] = qr->data[r][c];
        }

        for (int r = 0; r < QR_SIZE; ++r) {
            for (int c = 0; c < QR_SIZE; ++c) {
                if (qr->reserved[r][c])
                    continue;
                if (mask_condition(mask, r, c))
                    temp[r][c] ^= 1;
            }
        }

        int score = compute_penalty(temp);

        if (best_score == -1 || score < best_score) {
            best_score = score;
            best_mask = mask;
        }
    }

    return best_mask;
}

void place_format_info(struct qr_matrix *qr, int mask_id) {
    uint16_t format_info = (0b01 << 3) | (mask_id & 0x7);
    uint16_t generator = 0x537;
    uint16_t code = format_info << 10;

    for (int i = 14; i >= 10; --i) {
        if (code & (1 << i))
            code ^= generator << (i - 10);
    }

    uint16_t final = ((format_info << 10) | code) ^ 0x5412;

    // top-left
    int bits[15];
    for (int i = 0; i < 15; ++i)
        bits[i] = (final >> (14 - i)) & 1;
    // horizontal row8
    int idx = 0;
    for (int c = 0; c <= 8; ++c) {
        if (c == 6)
            continue;
        qr->data[8][c] = bits[idx++];
        qr->reserved[8][c] = 1;
    }
    // vertical col8
    for (int r = 7; r >= 0; --r) {
        if (r == 6)
            continue;
        qr->data[r][8] = bits[idx++];
        qr->reserved[r][8] = 1;
    }
    // top-right
    for (int c = QR_SIZE - 1; c >= QR_SIZE - 8; --c) {
        qr->data[8][c] = bits[14 - (QR_SIZE - 1 - c)];
        qr->reserved[8][c] = 1;
    }
    // bottom-left
    for (int r = QR_SIZE - 1; r >= QR_SIZE - 7; --r) {
        qr->data[r][8] = bits[14 - (QR_SIZE - 1 - r)];
        qr->reserved[r][8] = 1;
    }
}

void save_qr_png(const struct qr_matrix *qr, const char *file_name, int scale) {
    int quiet_zone = 4;
    int total_size = QR_SIZE + (quiet_zone * 2);
    int img_size = total_size * scale;

    uint8_t image[img_size * img_size];
    memset(image, 255, sizeof(image));

    for (int r = 0; r < QR_SIZE; ++r) {
        for (int c = 0; c < QR_SIZE; ++c) {
            int start_y = (r + quiet_zone) * scale;
            int start_x = (c + quiet_zone) * scale;

            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    int y = start_y + sy;
                    int x = start_x + sx;
                    image[y * img_size + x] = qr->data[r][c] ? 0 : 255;
                }
            }
        }
    }

    stbi_write_png(file_name, img_size, img_size, 1, image, img_size);
}

void generate_qr(const char *input, const char *file_name) {
    struct bit_stream bs = {0};
    encode_data(&bs, input);

    uint8_t bytes[19] = {0};
    bits_to_bytes(&bs, bytes, 19);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_8_4_3_2_0, 0, 1, 7);

    uint8_t encoded[26];
    correct_reed_solomon_encode(rs, bytes, 19, encoded);

    struct qr_matrix qr = {0};

    place_finder_pattern(&qr, 0, 0);
    place_finder_pattern(&qr, 0, QR_SIZE - 7);
    place_finder_pattern(&qr, QR_SIZE - 7, 0);
    place_timing_patterns(&qr);
    place_dark_module(&qr);
    reserve_format_areas(&qr);
    place_data_bits(&qr, encoded, 26);

    int chosen_mask = choose_best_mask(&qr);
    apply_mask_pattern(&qr, chosen_mask);
    place_format_info(&qr, chosen_mask);

    save_qr_png(&qr, file_name, 5);

    correct_reed_solomon_destroy(rs);
}

int main() {
    generate_qr("ハルウララ", "byte.png");
    generate_qr("0123456789", "numeric.png");
    generate_qr("HELLO WORLD+-/123$%", "alpha.png");

    return 0;
}
