// 転送専用ファーム（#157）— 本体の microSD を USB Mass Storage として PC に見せるだけの最小ファーム。
//
// 通常ファーム(main.cpp)とは env を分けている（platformio.ini の m5stack-cores3-msc 参照）。
// このファームにはシーン状態機械も Wi-Fi も無い。SD を PC に明け渡すことだけを行うので、
// PC と本体が同時に SD を掴む競合が構造的に起こらない。
//
// 使い方:
//   1. pio run -e m5stack-cores3-msc -t upload で書き込む
//   2. PC にリムーバブルドライブとして現れるので video/<name>/ をコピーする
//   3. 取り外し操作をしてから、通常ファーム(-e m5stack-cores3)に焼き戻す
//
// SPI.h / SD.h は M5Unified.h より先に include する（main.cpp と同じ制約・#148）。
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include "USB.h"
#include "USBMSC.h"
#include "sd_pins.h"    // kSdCsPin 等（通常ファームと共有・#157）
#include "msc_range.h"  // msc_range_ok（セクタ範囲の境界判定・純粋関数・native テスト済み）

// USBMSC クラスは sdkconfig の CONFIG_TINYUSB_MSC_ENABLED 配下にある。
// これが立つのは TinyUSB スタックを使うビルド、すなわち ARDUINO_USB_MODE=0 のとき。
// USB_MODE=1（ハードウェア CDC/JTAG）でこのファイルをビルドしても USB オブジェクト自体が
// 存在せずリンクに失敗するので、原因が分かる形で先に止める。
#if !CONFIG_TINYUSB_MSC_ENABLED
#error "USBMSC が無効。platformio.ini で ARDUINO_USB_MODE=0（TinyUSB）を指定すること（#157）。"
#endif

// USBMSC はコンストラクタで tinyusb_enable_interface を呼ぶため、setup() より前に
// 構築されている必要がある。関数ローカルに移すと USB 記述子の登録が間に合わない。
static USBMSC g_msc;

static uint32_t g_sectorSize  = 0;  // SD の 1 セクタ長（通常 512）
static uint32_t g_sectorCount = 0;

// PC から「LBA 番のセクタから bufsize バイトくれ」と来る。SD の生セクタ読みに取り次ぐだけ。
// FAT32 の解釈は PC 側が行うので、ここではファイルシステムを一切知らなくてよい。
//
// bufsize は 1 セクタとは限らない（CONFIG_TINYUSB_MSC_BUFSIZE=4096 なので最大 8 セクタ分が
// まとめて来る）。SD.readRAW は 1 セクタずつしか読めないため、必ず分割して回す。
static int32_t mscOnRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    // セクタ境界に揃っていない要求は扱わない。SD.readRAW がセクタ単位の API なので、
    // 中途半端なオフセットを黙って 0 埋めで返すとファイルが静かに壊れる。落とす方が安全。
    if (offset != 0 || g_sectorSize == 0 || (bufsize % g_sectorSize) != 0) return -1;
    const uint32_t count = bufsize / g_sectorSize;
    if (!msc_range_ok(lba, count, g_sectorCount)) return -1;
    uint8_t* out = static_cast<uint8_t*>(buffer);
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.readRAW(out + i * g_sectorSize, lba + i)) return -1;
    }
    return static_cast<int32_t>(bufsize);
}

// PC から「LBA 番のセクタへこれを書け」と来る。読みと対称。
static int32_t mscOnWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (offset != 0 || g_sectorSize == 0 || (bufsize % g_sectorSize) != 0) return -1;
    const uint32_t count = bufsize / g_sectorSize;
    if (!msc_range_ok(lba, count, g_sectorCount)) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.writeRAW(buffer + i * g_sectorSize, lba + i)) return -1;
    }
    return static_cast<int32_t>(bufsize);
}

// PC 側の「取り外し」操作で呼ばれる。ここで false を返すと取り外しを拒否できるが、
// 書き込みは都度 SD へ流しており保持中のキャッシュが無いので、素直に受け入れる。
static bool mscOnStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

// 画面表示は setup() の中だけで行う。loop からは一切描かない。
//
// ⚠ 重要（reviewer 指摘・#157）: CoreS3 では LCD と microSD が同じ SPI ピンを使い、
//   さらに GPIO35 が SD の MISO と LCD の D/C を兼用している（sd_pins.h 参照）。
//   MSC のコールバックは USB タスクで走るため、メインループから描画すると
//   SD 転送中にバスを奪い合い、PC が書いたデータが化けて SD に載る（静かなデータ破壊）。
//   よって MSC 開始後は描画しない。転送状況を画面に出したくなったら、コールバック全体と
//   描画全体を FreeRTOS mutex で相互排他してから行うこと。
//
// 文字列を ASCII に限っているのは、既定フォントが和文グリフを持たないため
// （和文を出すなら fonts::lgfxJapanGothic_16 への切り替えが要る・main.cpp の作法）。
// 出せない文字で潰れると原因究明が遅れるエラー経路なので、確実に読める側に倒す。
static void drawStatus(const char* line1, const char* line2) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 40);
    M5.Display.print(line1);
    if (line2) {
        M5.Display.setTextSize(1);
        M5.Display.setCursor(10, 80);
        M5.Display.print(line2);
    }
}

// SD が使えない場合でも USB は必ず開始する。
// early return して USB.begin() に到達しないと CDC(COM ポート)すら列挙されず、
// PC からは「無反応な壊れたデバイス」に見えて切り分けが困難になる。
// mediaPresent(false) のまま開始すれば「メディア未挿入のカードリーダ」として正しく見える。
//
// ⚠ 描画は必ず USB.begin() より前に済ませること（reviewer 指摘・#157）。
//   USB.begin() が返った時点で TinyUSB タスクが動き出し、ホストは列挙が済み次第
//   READ10 を投げてくる。その後に fillScreen（320x240 で約 30ms バスを占有）を走らせると、
//   MSC コールバックの SD アクセスと同一 SPI バスを奪い合う窓ができる。
static void beginUsbWithoutMedia(const char* line1, const char* line2) {
    drawStatus(line1, line2);
    g_msc.mediaPresent(false);
    USB.begin();
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    drawStatus("USB MSC mode", "mounting microSD...");

    // INQUIRY の名前は成功/失敗どちらの経路でも名乗れるよう、分岐より前に設定しておく。
    // エラー時こそ PC 側で「どのデバイスが無反応なのか」を識別できた方が切り分けに役立つ。
    g_msc.vendorID("M5Stack");
    // ⚠ productID は最大16文字。ちょうど16文字だと NUL 終端されず、フレームワーク側の
    //   tud_msc_inquiry_cb の strlen が溢れる。伸ばすときは15文字以内に収めること。
    g_msc.productID("CoreS3 microSD");
    g_msc.productRevision("1.0");

    SPI.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    if (!SD.begin(kSdCsPin, SPI, 25000000)) {
        beginUsbWithoutMedia("SD ERROR", "microSD not found. Reinsert and reboot.");
        return;
    }

    g_sectorSize  = SD.sectorSize();
    g_sectorCount = SD.numSectors();
    // sectorSize は USBMSC::begin に uint16_t で渡す。0xFFFF を超える値を切り捨てると
    // 0 に化けて begin が黙って失敗するので、ここで弾く。
    if (g_sectorSize == 0 || g_sectorSize > 0xFFFFu || g_sectorCount == 0) {
        g_sectorSize = 0;  // コールバックが誤って通らないよう無効化しておく
        beginUsbWithoutMedia("SD ERROR", "Unsupported sector geometry.");
        return;
    }

    g_msc.onRead(mscOnRead);
    g_msc.onWrite(mscOnWrite);
    g_msc.onStartStop(mscOnStartStop);
    g_msc.mediaPresent(true);

    // begin は block_size/block_count/コールバックのいずれか未設定なら false を返す。
    // 戻り値を見ないと「画面は ready なのに PC からは見えない」という切り分け困難な症状になる。
    if (!g_msc.begin(g_sectorCount, static_cast<uint16_t>(g_sectorSize))) {
        g_sectorSize = 0;
        beginUsbWithoutMedia("MSC ERROR", "USBMSC begin failed.");
        return;
    }

    // 描画を先に済ませてから USB を開始する（上記 beginUsbWithoutMedia のコメント参照）。
    // 逆順にすると、ホストの初回 READ10 と fillScreen が競合する窓ができる。
    char detail[96];
    const uint32_t mib = static_cast<uint32_t>((static_cast<uint64_t>(g_sectorCount) * g_sectorSize) >> 20);
    snprintf(detail, sizeof(detail), "%lu MiB / %lu sectors x %lu B",
             static_cast<unsigned long>(mib),
             static_cast<unsigned long>(g_sectorCount),
             static_cast<unsigned long>(g_sectorSize));
    drawStatus("USB MSC ready", detail);

    USB.begin();
}

void loop() {
    // 何もしない。上記のとおり LCD と SD が同一 SPI バスのため、ここで描画すると
    // USB タスクの SD 転送とバスを奪い合ってデータを壊す。転送は USB タスク側で進む。
    //
    // M5.update() も意図的に呼んでいない（消し忘れではない）。転送中に反応させたい
    // ボタン操作が無く、呼べば I2C ポーリングが増えるだけのため。
    delay(200);
}
