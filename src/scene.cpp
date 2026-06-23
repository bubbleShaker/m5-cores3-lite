#include "scene.h"

int next_scene(int current, int count) {
    if (count <= 0) return 0;  // シーンが無い時の安全値

    // current を [0, count) に正規化する（負値・範囲外でも破綻しないように）。
    // C++ の % は負の被除数で負を返し得るので、+count して再度 % で正に畳む。
    int normalized = ((current % count) + count) % count;

    return (normalized + 1) % count;  // 次へ進み、末尾なら先頭へ折り返す
}
