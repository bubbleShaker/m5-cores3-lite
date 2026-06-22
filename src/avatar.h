#pragma once
#include <cstdint>
#include <string>

// まばたきアニメーションのタイミング定数（ハード非依存・テストからも参照する）。
//   kBlinkIntervalMs … まばたき1周期の長さ（この周期の先頭でだけ瞬きする）
//   kBlinkDurationMs … 瞬き動作（開→閉→開）にかける時間
constexpr uint32_t kBlinkIntervalMs = 3000;
constexpr uint32_t kBlinkDurationMs = 150;

// 口パク1周期の長さ（短い＝速く開閉する。まばたきより速い 200ms ≒ 5Hz）。
constexpr uint32_t kMouthCycleMs = 200;

// 経過時間(ms)から目の開き具合を 0.0(完全に閉じ)〜1.0(完全に開き) で返す純粋関数。
// millis() に依存させず引数で時間を受けることで、実機なしで単体テストできる。
//   周期の先頭 kBlinkDurationMs だけ「開→閉→開」の三角波、残りは 1.0 で開いたまま。
float eye_openness(uint32_t elapsed_ms);

// 経過時間(ms)と「喋っているか」から口の開き具合を 0.0(閉)〜1.0(開) で返す純粋関数。
//   speaking == false → 常に 0.0（閉じたまま）
//   speaking == true  → kMouthCycleMs 周期の「閉→開→閉」三角波で開閉
// speaking を引数にすることで、後段の対話レイヤーが応答中だけ true を渡せる。
float mouth_openness(uint32_t elapsed_ms, bool speaking);

// アバターが取りうる表情の語彙（単一の真実）。API・描画はこの enum に依存する。
enum class Expression { Neutral, Happy, Thinking, Sad, Surprised };

// 一時的な表情をこの時間だけ保ち、過ぎたら Neutral に戻す。
constexpr uint32_t kExpressionHoldMs = 4000;

// API の "expression" 文字列を Expression に変換する純粋関数。
// 未知の文字列は Neutral にフォールバックする（堅牢性）。
Expression parse_expression(const std::string& name);

// 要求された表情を、要求からの経過時間に応じて返す純粋関数（自動復帰の状態機械）。
//   requested == Neutral        → 常に Neutral
//   elapsed < kExpressionHoldMs → requested を保持
//   elapsed >= kExpressionHoldMs → Neutral に戻る
Expression active_expression(Expression requested, uint32_t elapsed_since_request_ms);

// 表情の「見た目スタイル」（パラメトリック・ドット絵）。描画はこの値に従う。
//   EyeStyle  … 目の形（通常 / 見開き / 細め）
//   BrowShape … 眉の形（水平 / 上げ / ハの字 / 片眉上げ）
//   MouthShape… 口の形（横線 / 笑顔 / 口角下 / 丸）
enum class EyeStyle   { Normal, Wide, Squint };
enum class BrowShape  { Flat, Raised, Worried, Quizzical };
enum class MouthShape { Line, Smile, Frown, Round };

struct FaceStyle {
    EyeStyle   eye;
    BrowShape  brow;
    MouthShape mouth;
};

// Expression を見た目スタイルに変換する純粋関数（単一の真実）。
// 「どんな形にするか」だけ決め、「何ピクセルで描くか」は main.cpp の責務に分離する。
FaceStyle face_style(Expression e);
