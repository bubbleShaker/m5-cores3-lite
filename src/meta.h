#pragma once

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
