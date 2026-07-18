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
    // 0 長要求は TinyUSB からは到達しない想定（proc_read10 は残り転送量がある間しか呼ばない）。
    // 万一来た場合は安全側に倒して stall させる。SCSI 的には転送長0は合法な no-op なので
    // 厳密には 0 を返す手もあるが、生セクタを外部に明け渡す以上 fail-closed を優先する。
    if (count == 0) return false;
    if (lba >= sector_count) return false;     // 開始位置が既に範囲外
    return count <= sector_count - lba;        // 引き算なのでラップしない
}
