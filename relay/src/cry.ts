// 中継サーバ /pokemon/cry の「純粋ロジック」。child_process（FFmpeg 実行）に依存せず vitest で完結する。
// 役割は3つ: (1) 鳴き声 OGG の URL 組み立て (2) FFmpeg 引数の生成 (3) 生 PCM への標準 WAV ヘッダ付与。
//
// FFmpeg は「OGG をデコードして 16kHz/mono/16bit の生 PCM にする」トラスト境界として server.ts 側に置く。
// ここは「生 PCM バイト列 → デバイスが parse_wav_header で読める WAV バイト列」への純粋変換だけを担う。

// 出力音声フォーマット。デバイスの playRaw 経路（既存 TTS と同一）に合わせる。
export const SAMPLE_RATE = 16000;
export const CHANNELS = 1;
export const BITS_PER_SAMPLE = 16;

// 標準 WAV ヘッダ長。fmt(16) + 各チャンクヘッダを含む固定 44 バイト（src/wav.h の write_wav と対称）。
export const WAV_HEADER_SIZE = 44;

// 出力 WAV の上限。デバイスの kMaxWav=512KB バッファ以内に収める門番。
// 鳴き声は ~30-64KB 程度なので十分な余裕がある。
export const MAX_CRY_BYTES = 512 * 1024;

// 末尾スラッシュの有無を吸収してベース URL を正規化する（他モジュールと同じ作法）。
function normalizeBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

// 鳴き声 OGG の URL を組み立てる。既定ベースは PokeAPI/cries の "latest"（全世代カバー）。
export function cryUrl(baseUrl: string, id: number): string {
  return `${normalizeBase(baseUrl)}/${id}.ogg`;
}

// FFmpeg の引数配列を返す純粋関数。
// stdin(pipe:0) の OGG を 16kHz/mono/16bit の「生 PCM(s16le)」として stdout(pipe:1) へ吐かせる。
// -f wav をパイプ出力するとシーク不能でヘッダのサイズ欄が壊れる（0xFFFFFFFF）ため、
// あえて生 PCM を出させ、ヘッダは writeWavHeader で自前生成する。
export function ffmpegArgs(): string[] {
  return [
    "-loglevel",
    "error", // 進捗ログを抑え stderr はエラー時のみに絞る。
    "-i",
    "pipe:0", // 入力は stdin（OGG バイト列）。
    "-ar",
    String(SAMPLE_RATE),
    "-ac",
    String(CHANNELS),
    "-f",
    "s16le", // 出力はヘッダ無しの生 16bit LE PCM。
    "pipe:1", // 出力は stdout。
  ];
}

// リトルエンディアンで書き込むヘルパ。DataView を使い明示的にバイト順を固定する。
function writeString(view: DataView, offset: number, str: string): void {
  for (let i = 0; i < str.length; i++) {
    view.setUint8(offset + i, str.charCodeAt(i));
  }
}

// 生 16bit PCM に標準 44 バイト WAV ヘッダを被せる純粋関数。
// src/wav.h の write_wav と同じ標準フォーマットのみ出力し、parse_wav_header と往復一致する。
// バイト順は全フィールド・PCM 本体ともリトルエンディアン。
export function writeWavHeader(pcm: Uint8Array): Uint8Array<ArrayBuffer> {
  const dataLen = pcm.length;
  const byteRate = (SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE) / 8;
  const blockAlign = (CHANNELS * BITS_PER_SAMPLE) / 8;

  const out = new Uint8Array(WAV_HEADER_SIZE + dataLen);
  const view = new DataView(out.buffer);

  // RIFF チャンク: "RIFF" + (ファイル全体 - 8) + "WAVE"
  writeString(view, 0, "RIFF");
  view.setUint32(4, 36 + dataLen, true); // 36 = 残りヘッダ長(fmt+data ヘッダ)
  writeString(view, 8, "WAVE");

  // fmt サブチャンク: PCM(1) / チャンネル / サンプルレート / バイトレート / ブロックアライン / ビット深度
  writeString(view, 12, "fmt ");
  view.setUint32(16, 16, true); // fmt チャンク本体長(PCM は 16)
  view.setUint16(20, 1, true); // audio format = 1 (PCM)
  view.setUint16(22, CHANNELS, true);
  view.setUint32(24, SAMPLE_RATE, true);
  view.setUint32(28, byteRate, true);
  view.setUint16(32, blockAlign, true);
  view.setUint16(34, BITS_PER_SAMPLE, true);

  // data サブチャンク: "data" + PCM 本体長 + PCM 本体
  writeString(view, 36, "data");
  view.setUint32(40, dataLen, true);
  out.set(pcm, WAV_HEADER_SIZE);

  return out;
}
