#pragma once
#include <stddef.h>

// 動画選択の純粋ロジック（Issue #175）。
// SD の /video/ 直下を列挙して実機で選ばせる。ディレクトリ名は SD 上の外部入力なので、
// 区切り文字・".."・空・長すぎる名前を弾いてから "/video/<name>" を組み立てる。
// 実機・SD・M5 表示に依存しないので native テストできる（voice_select / menu と同じ流儀）。
//
// 検証規則は端末の読む側（videoOpenPack の pack= 名検証）と、素材を書く側
// （tools/video2frames.py の safe_subdir_name）に既にある作法を、列挙する側にも同じ形で置く。
// 「中途半端なパスで SD.open を呼ばせない」= video_frame_path と一貫させる。

// 1エントリのディレクトリ名の最大長（終端 '\0' を除く）。端末側の meta 用 name[32] や
// パス組み立て path[80] に必ず収まる小さめの値。/video/(7)+name+/(1)+frames.bin(10) が
// path[80] に収まることをこの上限が保証する。
static const size_t kVideoNameMax = 31;

// 保持する候補の最大件数（固定長配列で持つ・#175 設計メモ「件数上限」）。
// これを超える分は列挙側で黙って捨て、呼び出し側は count が頭打ちになることで気づく。
// 8 なのは選択画面が縦リスト（1行 21px・先頭 y=40）で、8 行なら 240px に収まるため
// （端末側 videoRenderSelect のレイアウトと対。増やすときはページングが要る）。
static const int kVideoListCap = 8;

// エントリ名が「単一のディレクトリ名」として妥当か（#175 設計メモ「エントリ名の妥当性判定」）。
// 弾く条件: null / 空 / "." / 区切り文字('/'・'\\')を含む / ".." を含む / kVideoNameMax 超え。
// videoOpenPack の pack= 名検証（strchr('/')・strchr('\\')・strstr("..")）と同じ規則にして、
// 読む側と列挙する側で「/video/ の外を開かせない」判定を揃える。
bool video_name_valid(const char* name);

// 妥当な名前から SD パス "/video/<name>" を buf に組み立てる純粋関数。
// 名前が不正 / buf が null / 収まらない（切り詰め）場合は false を返し、buf は使わせない
// （video_frame_path と同じ「中途半端な文字列は使わせない」作法）。
bool video_build_dir(char* buf, size_t buf_size, const char* name);

// 候補リスト（固定長 kVideoListCap 件）。列挙結果をここに詰め、name_at で引く。
// 選択カーソルは端末側が int で持ち、video_list_next で巡回させる（voice_select と同じ流儀）。
struct VideoList {
    char names[kVideoListCap][kVideoNameMax + 1];
    int  count;
};

// リストを空にする（列挙のやり直し・入場時に必ず呼ぶ）。
void video_list_clear(VideoList* list);

// 妥当な名前を1件追加する。成功時 true。
//   list/name が null、または名前が不正 → false（追加しない）
//   既に kVideoListCap 件で満杯 → false（溢れは捨てる。件数上限の扱いをここに閉じる）
bool video_list_add(VideoList* list, const char* name);

// index 番目の名前を返す。範囲外 / list が null は nullptr（0件時の表示分岐に使う）。
const char* video_list_name_at(const VideoList* list, int index);

// タップ X から「決定（右半分か）」を判定する純粋関数（voice_is_next_tap と同じ二分）。
// 右半分(x >= screenW/2)=決定して再生、左半分=次の候補へカーソル移動。
bool video_is_decide_tap(int x, int screenW);

// 選択カーソルを次へ1つ進める（[0, count) を巡回）。count<=0 のときは 0（候補なし時の安全値）。
// 単方向の巡回にするのは操作が「左タップ=次へ」の一方向だから。menu_move（クランプ）と違い、
// 末尾からは先頭へ戻して全件へ到達できるようにする。
int video_list_next(int index, int count);
