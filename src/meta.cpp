#include "meta.h"
#include <cstring>
#include <cstdlib>

int meta_get_int(const char* text, const char* key, int fallback) {
    if (!text || !key || !*key) return fallback;

    const size_t key_len = strlen(key);
    const char* p = text;  // 常に「行の先頭」を指す

    while (*p) {
        // 行頭が "key=" で始まるか。行頭一致＋直後 '=' を要求することで、
        // 別キーへの部分一致（"rate" ⊂ "sample_rate" 等）を防ぐ。
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            return atoi(p + key_len + 1);  // '=' の次から数値。改行/末尾で自然に止まる。
        }
        // 次の行頭へ進む。改行が無ければ最終行なので終了。
        const char* nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return fallback;
}
