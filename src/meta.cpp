#include "meta.h"
#include <cstring>
#include <cstdlib>

// "key=" の行を探し、値の先頭を指すポインタを返す（見つからなければ nullptr）。
// 3 つの公開関数が同じ走査規則を共有するための内部ヘルパ。ここを 1 箇所に集めておかないと、
// 「meta_get_int だけ部分一致を防いでいる」といった食い違いが後から入り込む。
static const char* meta_find_value(const char* text, const char* key) {
    if (!text || !key || !*key) return nullptr;

    const size_t key_len = strlen(key);
    const char* p = text;  // 常に「行の先頭」を指す

    while (*p) {
        // 行頭が "key=" で始まるか。行頭一致＋直後 '=' を要求することで、
        // 別キーへの部分一致（"rate" ⊂ "sample_rate" 等）を防ぐ。
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            return p + key_len + 1;  // '=' の次
        }
        // 次の行頭へ進む。改行が無ければ最終行なので終了。
        const char* nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return nullptr;
}

int meta_get_int(const char* text, const char* key, int fallback) {
    const char* v = meta_find_value(text, key);
    if (!v) return fallback;
    return atoi(v);  // 改行/末尾で自然に止まる
}

bool meta_has_key(const char* text, const char* key) {
    return meta_find_value(text, key) != nullptr;
}

bool meta_get_str(const char* text, const char* key, char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return false;
    buf[0] = '\0';  // 失敗時に呼び出し側が前回の中身を掴まないよう、先に空にしておく

    const char* v = meta_find_value(text, key);
    if (!v) return false;

    // 値の終端は改行か文字列末尾。CR は含めない（CRLF の meta.txt 対策）。
    size_t n = 0;
    while (v[n] && v[n] != '\n' && v[n] != '\r') n++;
    if (n == 0) return false;         // "key=" だけの行は値なしとして扱う
    if (n >= buf_size) return false;  // 切り詰めた値は使わせない（buf は空のまま）
    memcpy(buf, v, n);
    buf[n] = '\0';
    return true;
}
