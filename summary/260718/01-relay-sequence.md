# 中継サーバ(relay) の役割をシーケンス図で整理

- Issue: #155
- 位置づけ: `relay/` 中継サーバが担う2つの中継シーンを、認知負債軽減のため図示するドキュメント。コード変更なし。

## relay とは何か

M5Stack デバイスと外部サービス（Claude / VOICEVOX / Whisper / PokeAPI / GitHub CDN）の間に立つ中継サーバ（`relay/src/server.ts`, Hono 製）。
中継の動機はシーンによって2種類ある。

| シーン | エンドポイント | 中継の主な動機 |
|--------|--------------|---------------|
| 音声対話ループ | `/stt` `/chat` `/tts` | **秘密鍵の隠蔽**（`ANTHROPIC_API_KEY` をデバイスに載せない） |
| ポケモン図鑑 | `/pokemon/info` `/sprite` `/cry` | **重い変換のオフロード**＋外部 API 保護（fair-use / キャッシュ） |

---

## ① 音声対話ループ（`/stt` → `/chat` → `/tts`）

READMEの「M3b: 聞いて→考えて→喋る」の一巡。デバイスは秘密鍵を持たず、relay の3エンドポイントだけを知る。

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant Dev as M5Stack<br/>(src/net.cpp)
    participant Relay as 中継サーバ<br/>(relay/server.ts)
    participant STT as Whisper<br/>(自前ホスト)
    participant Claude as Claude API<br/>(Haiku 4.5)
    participant VV as VOICEVOX<br/>ENGINE

    Note over Dev,Relay: デバイスは ANTHROPIC_API_KEY を持たない<br/>キーは中継サーバだけが保持

    User->>Dev: 話しかける（M5.Mic 録音）
    Dev->>Dev: PCM → WAV 化

    rect rgb(235, 245, 255)
    Note right of Dev: ① 聞く
    Dev->>Relay: POST /stt (raw WAV, ?language=ja)
    Relay->>Relay: validateAudio(WAVヘッダ/容量)
    Relay->>STT: POST /asr (multipart audio_file)
    STT-->>Relay: { text: "おはよう" }
    Relay-->>Dev: { text: "おはよう" }
    end

    rect rgb(255, 245, 235)
    Note right of Dev: ② 考える
    Dev->>Relay: POST /chat { message: "おはよう" }
    Relay->>Claude: messages.create(system+message)
    Claude-->>Relay: 応答テキスト
    Relay->>Relay: parseClaudeReply（JSON検証）
    Relay-->>Dev: { reply, expression, action }
    Dev->>Dev: 表情/アクション反映（face_logic）
    end

    rect rgb(240, 255, 240)
    Note right of Dev: ③ 喋る
    Dev->>Relay: POST /tts { text: reply, voice_id }
    alt キャッシュヒット
        Relay-->>Dev: 即 audio/wav（VOICEVOX 未呼出）
    else 初回
        Relay->>VV: POST /audio_query
        VV-->>Relay: audio_query JSON
        Relay->>Relay: adjustAudioQuery（16kHz/mono へ整形）
        Relay->>VV: POST /synthesis
        VV-->>Relay: WAV バイト列
        Relay->>Relay: ttsCache へ格納（合計16MB上限）
        Relay-->>Dev: audio/wav
    end
    end

    Dev->>User: スピーカー再生（喋る）
```

### 押さえる点
- **中継の存在理由**: デバイスに秘密鍵を置かず、`ANTHROPIC_API_KEY` を持つのは中継サーバだけ。
- **中継サーバ＝翻訳＆整形役**: デバイスは relay の3エンドポイントしか知らない。上流が何でもデバイスから見た形は一定（純粋ロジック `stt.ts`/`chat.ts`/`tts.ts` が整形を担う）。
- **`/tts` のキャッシュ分岐**: 定型文は2回目以降 VOICEVOX を叩かず即返し、体感遅延を消す。

---

## ② ポケモン図鑑（`/pokemon/info` → `/sprite` → `/cry`）

非力なデバイスの代わりに、中継サーバが「上流の生データ」を「即使える形」へ変換する。

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant Dev as M5Stack<br/>(src/pokemon.cpp)
    participant Relay as 中継サーバ<br/>(relay/server.ts)
    participant Poke as PokeAPI<br/>(pokeapi.co)
    participant CDN as GitHub raw CDN<br/>(sprites/cries)
    participant FF as FFmpeg<br/>(子プロセス)

    User->>Dev: 図鑑番号を選ぶ（例: 25）

    rect rgb(255, 250, 235)
    Note right of Dev: ① 情報（info）
    Dev->>Relay: GET /pokemon/info/25
    Relay->>Relay: parsePokemonId（1..1025 検証）
    alt キャッシュヒット
        Relay-->>Dev: 即 コンパクトJSON
    else 初回
        par 2本を並行取得
            Relay->>Poke: GET /pokemon/25
        and
            Relay->>Poke: GET /pokemon-species/25
        end
        Poke-->>Relay: 生JSON（~30KB × 2）
        Relay->>Relay: extractPokemonInfo（実機向けに削る）
        Relay->>Relay: infoCache へ格納
        Relay-->>Dev: コンパクトJSON（名前/分類/説明）
    end
    Dev->>Dev: 図鑑テキスト描画
    end

    rect rgb(235, 245, 255)
    Note right of Dev: ② 姿（sprite）
    Dev->>Relay: GET /pokemon/sprite/25
    alt キャッシュヒット
        Relay-->>Dev: 即 RGB565 バイト列
    else 初回
        Relay->>CDN: GET 25.png
        CDN-->>Relay: PNG
        Relay->>Relay: sharp デコード＋リサイズ（トラスト境界/展開爆弾対策）
        Relay->>Relay: rgbaToRgb565（96x96 RGB565 へ）
        Relay->>Relay: spriteCache へ格納
        Relay-->>Dev: RGB565 バイト列（18432B 固定）
    end
    Dev->>Dev: スプライト描画（クロマキー透過）
    end

    rect rgb(240, 255, 240)
    Note right of Dev: ③ 鳴き声（cry）
    Dev->>Relay: GET /pokemon/cry/25
    alt キャッシュヒット
        Relay-->>Dev: 即 audio/wav
    else 初回
        Relay->>CDN: GET 25.ogg
        CDN-->>Relay: OGG
        Relay->>FF: OGG →（stdin/stdout パイプ）
        Note over Relay,FF: 一時ファイルなし・タイムアウト/出力上限で門番
        FF-->>Relay: 16kHz/mono/16bit 生PCM
        Relay->>Relay: writeWavHeader（device の parse_wav_header と往復一致）
        Relay->>Relay: cryCache へ格納
        Relay-->>Dev: audio/wav
    end
    Dev->>User: スピーカー再生（鳴く）
    end
```

### 押さえる点
- **中継サーバ＝重い変換の肩代わり役**。デバイスは変換ロジックを一切持たない。

| エンドポイント | 上流の生データ | 変換後（デバイス向け） | 変換の担い手 |
|---|---|---|---|
| `/info` | PokeAPI JSON ~30KB×2 | コンパクト JSON | `extractPokemonInfo` |
| `/sprite` | PNG | RGB565 バイト列 18432B 固定 | `sharp` + `rgbaToRgb565` |
| `/cry` | OGG | WAV（16kHz/mono） | `FFmpeg` + `writeWavHeader` |

- **全エンドポイントにキャッシュ**: PokeAPI の fair-use ポリシー順守＋レイテンシ削減。キーが検証済み id（1..1025）に閉じるのでメモリ有界（プロセス再起動でクリア、永続化しない＝ライセンス方針にも合致）。
- **音声対話ループとの違い**: あちらは「秘密鍵の隠蔽」、こちらは「変換負荷のオフロード＋外部 API 保護」が中継の動機。
