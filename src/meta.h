#pragma once
#include <stddef.h>

// 動画 manifest（meta.txt）の 1 項目を取り出す純粋ロジック（Issue #148 / PLAN Step2a）。
// tools/video2frames.py が出力する meta.txt は「key=value」を 1 行ずつ並べたテキスト:
//   fps=8
//   frames=1234
//   width=320
//   height=240
//   sample_rate=16000
//   channels=1
// 端末はファイルを丸ごと読んでこの関数に渡すだけでよい（ArduinoJson 不要・SD/実機に非依存）。
// millis や実描画に依存しないので native テストできる。
//
// text  … meta.txt の中身全体（'\n' 区切り。末尾の改行は有っても無くてもよい）
// key   … 取り出したいキー名（例 "fps"）
// fallback … キーが見つからない/不正なときに返す既定値
// 返り値 … その key の行 "key=<数値>" の数値。見つからなければ fallback。
//
// マッチは「行頭が key に一致し、直後が '='」の時だけ（"rate" が "sample_rate" に
// 部分一致したり、"channel" が "channels" に化けたりしない）。値は atoi 相当で解釈する。
int meta_get_int(const char* text, const char* key, int fallback);

// そのキーの行が存在するか（Issue #170）。
// meta_get_str は「キーが無い」と「キーはあるが値が長すぎ/空で取り出せない」の両方で false を
// 返すため、その2つを取り違えると危ない場面がある。パック方式の判定がまさにそれで、
// `pack=<長すぎる名前>` を「pack の宣言が無い（＝旧アセット）」と読むと、遅い連番方式へ
// 黙って落ちて「なぜか遅いだけ」の状態になる（reviewer 指摘）。存在の判定はこちらで行う。
bool meta_has_key(const char* text, const char* key);

// 同じ manifest から「文字列の値」を取り出す（Issue #170）。
// パック方式の導入で `pack=frames.bin` のような非数値の項目が増えたため、meta_get_int と
// 対になる形で用意する。マッチ規則（行頭一致＋直後 '='）は meta_get_int と完全に同じ。
//
// 値は行末（'\n'）または文字列末尾まで。'\r' は値に含めない（meta.txt を Windows 側で
// 編集して CRLF になっても、端末が "frames.bin\r" を開こうとして落ちないようにするため）。
//
// buf に収まらない場合は false を返し buf は空文字にする（切り詰めたファイル名で
// SD.open を呼ばせない・video_frame_path と同じ「中途半端な文字列は使わせない」作法）。
// キーが無い場合も false／空文字。true の時だけ buf を使う契約。
bool meta_get_str(const char* text, const char* key, char* buf, size_t buf_size);
