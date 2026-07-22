#include "video_list.h"
#include <string.h>
#include <stdio.h>

bool video_name_valid(const char* name) {
    if (name == nullptr) return false;
    const size_t len = strlen(name);
    if (len == 0 || len > kVideoNameMax) return false;
    // "." は「今のディレクトリ」を指すので単一名として無効。".." は下の strstr でまとめて弾く。
    if (strcmp(name, ".") == 0) return false;
    // 区切り文字を含むと /video/ の外へ抜けうる。".." は親への遡上。読む側(videoOpenPack)と同じ規則。
    if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) return false;
    return true;
}

bool video_build_dir(char* buf, size_t buf_size, const char* name) {
    if (buf == nullptr || buf_size == 0) return false;
    if (!video_name_valid(name)) { buf[0] = '\0'; return false; }
    const int written = snprintf(buf, buf_size, "/video/%s", name);
    // 負値（エンコードエラー）も切り詰めも弾く。中途半端なパスで SD.open を呼ばせない。
    if (written < 0 || written >= static_cast<int>(buf_size)) { buf[0] = '\0'; return false; }
    return true;
}

void video_list_clear(VideoList* list) {
    if (list == nullptr) return;
    list->count = 0;
}

bool video_list_add(VideoList* list, const char* name) {
    if (list == nullptr) return false;
    if (!video_name_valid(name)) return false;
    if (list->count >= kVideoListCap) return false;  // 満杯：溢れは捨てる（件数上限をここに閉じる）
    // 検証済みなので kVideoNameMax に収まる。終端を必ず入れる（strncpy の切り詰めに備える）。
    char* dst = list->names[list->count];
    strncpy(dst, name, kVideoNameMax);
    dst[kVideoNameMax] = '\0';
    list->count++;
    return true;
}

const char* video_list_name_at(const VideoList* list, int index) {
    if (list == nullptr) return nullptr;
    if (index < 0 || index >= list->count) return nullptr;
    return list->names[index];
}

bool video_is_decide_tap(int x, int screenW) {
    // 右半分=決定。境界(ちょうど中央)は右扱い(>=)にして左右で漏れなく二分する（voice_select と同じ）。
    return x >= screenW / 2;
}

int video_list_next(int index, int count) {
    if (count <= 0) return 0;  // 候補なし時の安全値
    int i = index % count;
    if (i < 0) i += count;     // 範囲外・負値でも巡回で正規化してから進める（堅牢性）
    i = (i + 1) % count;
    return i;
}
