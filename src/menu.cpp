#include "menu.h"

int menu_move(int current, int count, int delta) {
    if (count <= 0) return 0;  // 項目が無い時の安全値

    // current を [0, count) に丸める（負値・範囲外でも破綻しないように）。
    int normalized = ((current % count) + count) % count;

    int next = normalized + delta;
    if (next < 0) next = 0;                 // 先頭より上へは行かない
    if (next > count - 1) next = count - 1; // 末尾より下へは行かない
    return next;
}
