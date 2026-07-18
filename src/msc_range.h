#pragma once
#include <stdint.h>

// USB MSC の生セクタ R/W 要求が、実際のカード範囲に収まっているかを判定する（#157）。
//
// なぜ自前で持つか: TinyUSB 側にも同種のチェックはあるが `lba + block_count > max` という
// 加算式で、32bit でラップする（例: lba=0xFFFFFFFF, count=8 → 7 となり素通りする）。
// 素通りするとセクタ0（MBR/ブートセクタ）への書き込みに化け、カードが静かに壊れる。
// 生セクタを PC に明け渡す以上、境界は上流に委ねず自分で持つ。
//
// 実装は減算で比較し、加算オーバーフローそのものを発生させない。
// 純粋関数なので native env で境界値テストできる。
inline bool msc_range_ok(uint32_t lba, uint32_t count, uint32_t sector_count) {
    if (count == 0) return false;              // 0 セクタ要求は不正
    if (lba >= sector_count) return false;     // 開始位置が既に範囲外
    return count <= sector_count - lba;        // 引き算なのでラップしない
}
