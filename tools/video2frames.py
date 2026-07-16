#!/usr/bin/env python3
"""動画 → JPEGフレーム列 + WAV + meta.txt へ変換（SDカード再生用・PLAN.md Step1）。

役割は「YouTube 等の動画を、CoreS3(ESP32-S3) が microSD から drawJpg + playRaw で
再生できる素材に PC 側で事前変換する」こと。ESP32-S3 単体では H.264/VP9・DRM の
生ストリームを復号できないため、重い復号・デコードは PC 側で済ませてしまう設計。

内部は外部コマンドを起動するだけの薄いラッパ:
  1. yt-dlp   … 動画をローカルに 1 本ダウンロード（URL の代わりにローカル動画も可）
  2. ffmpeg   … 320x240 のアスペクト維持レターボックス JPEG 連番へ変換
  3. ffmpeg   … 音声を 16bit PCM WAV へ抽出
  4. meta.txt … 端末が読む manifest（fps/frames/width/height/sample_rate/channels）

manifest を JSON でなく key=value テキストにしているのは、端末側(Step2)で
ArduinoJson 依存を増やさず 1 行ずつ読めるようにするため。fps は端末の
video_frame_at(elapsed_ms, fps, frame_count) にそのまま渡す時間軸の基準になる。

出力レイアウト（--outdir 配下に <name>/ を作る。SD へはこの <name>/ ごと置く）:
  <name>/frame_00001.jpg …   … 連番 JPEG
  <name>/audio.wav           … 抽出した WAV
  <name>/meta.txt            … manifest

商標注意: 変換した動画アセットはリポジトリにコミットしない（.gitignore の video/ 済み。
ポケモン素材と同じ「実行時のみ・非コミット」方針）。

使い方:
  python tools/video2frames.py https://youtu.be/Xbt0EqXOAjw --name sample
  python tools/video2frames.py ./local.mp4 --name sample --fps 12 --sample-rate 16000
  # 生成物は既定で ./video/sample/ に出る。--outdir で SD のマウント先を直接指定してもよい。
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# Windows の既定 stdout(cp932) だと meta.txt 生成ログの日本語が化ける。UTF-8 に固定。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

# 画面は setRotation(1) で 320x240。フレームはこのサイズに合わせて出す（レターボックス）。
SCREEN_W = 320
SCREEN_H = 240


def build_scale_filter(width: int, height: int) -> str:
    """アスペクト維持で width×height に収め、余白を黒でレターボックスする ffmpeg フィルタ。

    force_original_aspect_ratio=decrease で「はみ出さないよう縮小」→ pad で中央寄せ黒帯。
    こうすると元動画の比率が変わらず、端末は常に固定サイズの JPEG を貼れる。
    """
    return (
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black"
    )


def build_frame_cmd(src: str, out_dir: str, fps: int, width: int, height: int, quality: int) -> list:
    """動画 → JPEG 連番へ変換する ffmpeg コマンドを組み立てる（起動はしない・テスト可能に分離）。

    -qscale:v は JPEG 品質（2=高画質/大, 31=低画質/小）。容量と画質のトレードオフで後で調整。
    出力は frame_00001.jpg から始まる 5 桁ゼロ詰め連番。
    """
    pattern = os.path.join(out_dir, "frame_%05d.jpg")
    return [
        "ffmpeg", "-y", "-i", src,
        "-vf", f"fps={fps},{build_scale_filter(width, height)}",
        "-qscale:v", str(quality),
        pattern,
    ]


def build_audio_cmd(src: str, out_path: str, sample_rate: int, channels: int) -> list:
    """動画 → 16bit PCM WAV を抽出する ffmpeg コマンドを組み立てる。

    -vn で映像を捨て、pcm_s16le（符号付き16bit リトルエンディアン）に固定。
    端末の playRaw / parse_wav_header はこの素直な PCM を前提にしている。
    """
    return [
        "ffmpeg", "-y", "-i", src,
        "-vn",
        "-acodec", "pcm_s16le",
        "-ar", str(sample_rate),
        "-ac", str(channels),
        out_path,
    ]


def render_meta(fps: int, frames: int, width: int, height: int,
                sample_rate: int, channels: int) -> str:
    """端末が読む manifest（key=value テキスト）を生成する純粋関数。

    端末は 1 行ずつ '=' で割って読む想定。fps と frames は video_frame_at にそのまま渡す。
    """
    lines = [
        f"fps={fps}",
        f"frames={frames}",
        f"width={width}",
        f"height={height}",
        f"sample_rate={sample_rate}",
        f"channels={channels}",
    ]
    return "\n".join(lines) + "\n"


def require_tool(name: str) -> None:
    """外部ツールの存在を先に確認し、無ければ導入方法を添えて即エラーにする。"""
    if shutil.which(name) is None:
        sys.exit(f"エラー: '{name}' が見つからない。導入してから再実行して。"
                 f"（例: yt-dlp は pipx install yt-dlp / ffmpeg は apt install ffmpeg）")


def safe_subdir_name(name: str) -> str:
    """--name を「単一のディレクトリ名」に限定して検証する（パストラバーサル防止）。

    os.path.join('video', name) は name が絶対パスや '..' を含むと video/ の外へ抜ける。
    区切り文字・'..'・絶対パスを弾き、SD の /video/ 配下だけに書けるよう保証する。
    """
    if not name or name in (".", "..") or os.path.isabs(name) or "/" in name or "\\" in name:
        sys.exit(f"エラー: --name は区切り文字や '..'・絶対パスを含まない単一名にして: {name!r}")
    return name


def run_step(cmd: list, what: str) -> None:
    """外部コマンドを起動し、失敗時は生のトレースバックでなく日本語エラーで止める。

    require_tool の丁寧なエラーと体験を揃えるため、CalledProcessError を握って sys.exit へ寄せる。
    """
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(f"エラー: {what} に失敗（終了コード {e.returncode}）: {' '.join(cmd)}")


def download_source(url_or_path: str, work_dir: str) -> str:
    """URL なら yt-dlp でダウンロード、ローカルファイルならそのまま使う。返り値は動画パス。"""
    # http(s) 以外は「もう手元にある動画」とみなし、ダウンロードを省く（再変換が速い）。
    if not url_or_path.startswith(("http://", "https://")):
        if not os.path.isfile(url_or_path):
            sys.exit(f"エラー: 入力が URL でもファイルでもない: {url_or_path}")
        return url_or_path

    require_tool("yt-dlp")
    out_tmpl = os.path.join(work_dir, "source.%(ext)s")
    # bestvideo+bestaudio を狙い、無ければ best にフォールバック。現在の YouTube は
    # progressive(best 単体) が 360p 上限や不在のことがあるため、分離ストリームを
    # ffmpeg（既に必須依存）でマージさせる方が堅い。--no-part で中間 .part を残さない。
    run_step(
        ["yt-dlp", "-f", "bestvideo+bestaudio/best", "--no-part", "-o", out_tmpl, url_or_path],
        "yt-dlp によるダウンロード",
    )
    # マージ後の最終出力（source.<ext>）を拾う。中間ファイル（.part/.f137.mp4 等）は
    # --no-part とマージにより残らない想定だが、動画拡張子だけに絞って誤検出を避ける。
    for fn in sorted(os.listdir(work_dir)):
        if fn.startswith("source.") and fn.rsplit(".", 1)[-1].lower() in ("mp4", "mkv", "webm", "mov"):
            return os.path.join(work_dir, fn)
    sys.exit("エラー: yt-dlp のダウンロード結果が見つからない。")


def count_frames(out_dir: str) -> int:
    """生成された frame_*.jpg の枚数を数える（meta の frames に使う）。"""
    return sum(1 for fn in os.listdir(out_dir)
               if fn.startswith("frame_") and fn.endswith(".jpg"))


def main() -> int:
    ap = argparse.ArgumentParser(description="動画 → JPEGフレーム列 + WAV + meta.txt（SD再生用）")
    ap.add_argument("source", help="YouTube 等の URL、またはローカル動画ファイル")
    ap.add_argument("--name", required=True, help="出力サブディレクトリ名（SD の /video/<name>/ に対応）")
    ap.add_argument("--outdir", default="video", help="出力先の親ディレクトリ（既定: ./video。SD 直指定も可）")
    ap.add_argument("--fps", type=int, default=10, help="フレームレート（既定: 10。端末の実測で調整）")
    ap.add_argument("--width", type=int, default=SCREEN_W, help=f"フレーム幅（既定: {SCREEN_W}）")
    ap.add_argument("--height", type=int, default=SCREEN_H, help=f"フレーム高（既定: {SCREEN_H}）")
    ap.add_argument("--quality", type=int, default=5, help="JPEG 品質 2=高画質/大 〜 31=低画質/小（既定: 5）")
    ap.add_argument("--sample-rate", type=int, default=16000, help="音声サンプルレート（既定: 16000）")
    ap.add_argument("--channels", type=int, default=1, help="音声チャンネル数 1=mono 2=stereo（既定: 1）")
    args = ap.parse_args()

    require_tool("ffmpeg")

    name = safe_subdir_name(args.name)
    out_dir = os.path.join(args.outdir, name)
    os.makedirs(out_dir, exist_ok=True)
    # 前回の変換で残った frame_*.jpg を必ず消す。ffmpeg -y は個別上書きするだけなので、
    # 今回が前回より短いと古い連番が残り、count_frames が frames を過大計上してしまう
    # （端末の video_frame_at が存在しない別動画のフレームを指す原因になる）。
    for fn in os.listdir(out_dir):
        if fn.startswith("frame_") and fn.endswith(".jpg"):
            os.remove(os.path.join(out_dir, fn))

    # yt-dlp のダウンロードは一時領域で行い、変換後は消す（生動画はコミットも常駐もさせない）。
    with tempfile.TemporaryDirectory() as work_dir:
        src = download_source(args.source, work_dir)

        print(f"[1/3] フレーム抽出 → {out_dir}/frame_*.jpg")
        run_step(build_frame_cmd(src, out_dir, args.fps, args.width, args.height, args.quality), "フレーム抽出")

        audio_path = os.path.join(out_dir, "audio.wav")
        print(f"[2/3] 音声抽出 → {audio_path}")
        run_step(build_audio_cmd(src, audio_path, args.sample_rate, args.channels), "音声抽出")

    frames = count_frames(out_dir)
    meta = render_meta(args.fps, frames, args.width, args.height, args.sample_rate, args.channels)
    meta_path = os.path.join(out_dir, "meta.txt")
    with open(meta_path, "w", encoding="utf-8") as f:
        f.write(meta)

    print(f"[3/3] manifest → {meta_path}")
    print(f"完了: {frames} フレーム / {args.fps}fps。SD の /video/{name}/ にこのディレクトリごと置く。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
