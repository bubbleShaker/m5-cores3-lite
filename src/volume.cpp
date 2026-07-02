#include "volume.h"

// レベルを [0, kVolumeMax] にクランプする内部ヘルパ。
static int clampLevel(int level) {
    if (level < 0)          return 0;
    if (level > kVolumeMax) return kVolumeMax;
    return level;
}

int volume_up(int level) {
    return clampLevel(level + 1);
}

int volume_down(int level) {
    return clampLevel(level - 1);
}

uint8_t volume_to_speaker(int level) {
    const int lv = clampLevel(level);
    // 0..kVolumeMax を 0..255 へ線形変換。kVolumeMax>0 は定数で保証されるので0除算しない。
    return static_cast<uint8_t>(lv * 255 / kVolumeMax);
}

bool volume_is_up_tap(int x, int screenW) {
    // 右半分=アップ。境界(ちょうど中央)は右扱い（>=）にして左右で漏れなく二分する。
    return x >= screenW / 2;
}
