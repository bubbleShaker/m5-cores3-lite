#include "voice_select.h"

namespace {

// 画面表示名と、中継 /tts に渡す VOICEVOX の speaker id の対。
struct VoiceOption {
    int         speaker;  // VOICEVOX ENGINE の話者番号
    const char* name;     // 画面に出す日本語名
};

// VOICEVOX 同梱の代表話者を厳選（id は VOICEVOX ENGINE の話者番号）。
// 先頭を既定(ずんだもん=3)にして、従来の無指定時と同じ声を初期状態にする。
constexpr VoiceOption kVoices[] = {
    { 3,  "ずんだもん" },
    { 2,  "四国めたん" },
    { 8,  "春日部つむぎ" },
    { 10, "雨晴はう" },
    { 13, "青山龍星" },
};
constexpr int kCount = static_cast<int>(sizeof(kVoices) / sizeof(kVoices[0]));

// index を [0, kCount) に巡回で正規化する（負値も剰余が負にならないよう補正）。
int norm(int index) {
    int i = index % kCount;
    if (i < 0) i += kCount;
    return i;
}

}  // namespace

int voice_option_count() {
    return kCount;
}

int voice_speaker_at(int index) {
    return kVoices[norm(index)].speaker;
}

const char* voice_name_at(int index) {
    return kVoices[norm(index)].name;
}

bool voice_is_next_tap(int x, int screenW) {
    // 右半分=次へ。境界(ちょうど中央)は右扱い(>=)にして左右で漏れなく二分する。
    return x >= screenW / 2;
}

int voice_next(int index) {
    return norm(index + 1);
}

int voice_prev(int index) {
    return norm(index - 1);
}
