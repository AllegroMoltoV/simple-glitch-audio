# Simple Glitch

DAW 向けのグリッチエフェクトのオーディオプラグインです。入力された音を DAW のテンポに合わせて区切り、繰り返し・無音・逆再生とビット落としで崩します。Windows x64 と Linux x86_64 向けに、VST® 3 と CLAP の形式で配布します。

A tempo-synced glitch effect for DAWs. It breaks the input into stutters, gates, reverses and bit crushing. VST® 3 and CLAP formats for Windows x64 and Linux x86_64.

音のプラグイン倉庫くんの[掲載ページ](https://audio-soko-kun.allegromoltov.jp/plugins/allegromoltov/simple-glitch-audio)から、検査を通った版を入手できます。 / Listed on [Audio Plugin Soko-kun](https://audio-soko-kun.allegromoltov.jp/plugins/allegromoltov/simple-glitch-audio?lang=en), which links only to releases that passed its checks.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/VST_Compatible_Logo_Steinberg_negative.png">
  <img src="docs/images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible" width="120">
</picture>

## 機能

入力の音を、Slice で選んだ長さの区切りに分けます。区切りごとに Amount の確率でグリッチが起き、次の 3 種類から 1 つを等しい確率で選びます。

- スタッター: 区切りの頭の 1/2、1/4、1/8 を繰り返します。
- ゲート: 区切りの後半を無音にします。
- リバース: 直前の区切りを逆再生します。

パラメーターは次の 4 つです。

| パラメーター | 範囲 | 既定値 | 内容 |
| --- | --- | --- | --- |
| Amount | 0〜100 % | 50 % | 区切りごとにグリッチが起きる確率 |
| Slice | 1/32、1/16、1/8、1/4 音符 | 1/16 | 区切りの長さ。DAW のテンポに合わせます |
| Crush | 0〜100 % | 0 % | ビット数を 16 から 4 まで減らし、サンプルを最大 16 個ずつ間引きます |
| Mix | 0〜100 % | 100 % | 処理した音と原音の混ぜ具合 |

- レイテンシーは 0 です。スタッターは区切りの頭を録りながら繰り返し、リバースは直前の区切りを使うため、先読みしません。
- 区切りの境目と繰り返しの継ぎ目に 2 ms のクロスフェードを入れ、クリックを防ぎます。
- DAW が再生中で再生位置を渡すときは、区切りの番号からグリッチを決めます。同じ位置を再生すると同じグリッチになるため、書き出しの結果が毎回そろいます。停止中や再生位置が無いときは、内部の時計で区切ります。テンポが無いときは 120 BPM とします。
- Amount と Crush が 0 % のときは、Mix の値によらず、入力をそのまま出力します。
- ステレオ音声に対応します。左右は同じ区切りで同じグリッチになります。サンプルレートは 44.1 kHz、48 kHz、96 kHz で動作を確かめています。
- 通信しません。音声やデータを外部へ送りません。

## 動作環境

- Windows 10 / 11 x64
- Linux x86_64
- VST 3 または CLAP に対応した DAW

macOS 向けには配布しません。macOS 向けの配布には、Apple の Developer ID の署名と公証が要るためです。

Linux 向けの版は、CI の pluginval と clap-validator で検証しています。Linux の DAW での動作は確かめていません。

## インストール

1. DAW を終了します。
2. [GitHub Releases](https://github.com/AllegroMoltoV/simple-glitch-audio/releases) から、環境に合う zip をダウンロードして展開します。Windows では `simple-glitch-<版>-windows-x64.zip`、Linux では `simple-glitch-<版>-linux-x86_64.zip` です。
3. Windows では、`VST3\Simple Glitch.vst3` フォルダーを、次のどちらかへコピーします。
   - 自分だけが使う場合: `%LOCALAPPDATA%\Programs\Common\VST3\`
   - すべてのユーザーが使う場合: `C:\Program Files\Common Files\VST3\`。管理者の権限が必要です。
4. Windows の CLAP に対応した DAW では、`CLAP\Simple Glitch.clap` を `%LOCALAPPDATA%\Programs\Common\CLAP\` か `C:\Program Files\Common Files\CLAP\` へコピーします。
5. Linux では、`VST3/Simple Glitch.vst3` フォルダーを `~/.vst3/` へ、`CLAP/Simple Glitch.clap` を `~/.clap/` へコピーします。
6. DAW を起動し、必要ならプラグインを再スキャンします。

インストーラーはありません。

## ダウンロードしたファイルの確かめ方

リリースのファイルには、GitHub Actions でビルドしたことを示す証明 (artifact attestation) を付けています。[GitHub CLI](https://cli.github.com/) で確かめられます。

```sh
gh attestation verify simple-glitch-<版>-<環境>.zip --repo AllegroMoltoV/simple-glitch-audio
```

リリースは不変 (immutable release) に設定しており、公開後にファイルを差し替えられません。

## ビルド

CMake 3.21 以降と C++17 のコンパイラーが必要です。Windows では Visual Studio 2022 以降を使い、CMake はインストールされている中で最も新しい Visual Studio を選びます。Linux では GCC を使います。依存する SDK は、構成の時点でコミットを固定して取得します。

```sh
# Windows
cmake -B build -A x64
# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
ctest --test-dir build -C Release
```

単体テスト `tests/glitch-test.cpp` では、入力がそのまま出ること、区切りの長さ、各グリッチの中身、境目のクリックの上限、同じ再生位置での再現性、ビット落としを確かめます。CI では、単体テストに加えて、[pluginval](https://github.com/Tracktion/pluginval) と [clap-validator](https://github.com/free-audio/clap-validator) で検証します。pluginval は VST 3 SDK の validator を内蔵しており、あわせて実行します。

## ライセンス

MIT License です。[LICENSE](LICENSE) を参照してください。

`docs/images/` の VST Compatible ロゴの画像は Steinberg Media Technologies GmbH の商標で、MIT License の対象外です。

配布するファイルに含む第三者のソフトウェアと、そのライセンスは次のとおりです。いずれも配布する zip の `THIRD_PARTY_LICENSES` にライセンス文を同梱しています。

| ソフトウェア | 用途 | ライセンス |
| --- | --- | --- |
| [CLAP](https://github.com/free-audio/clap) | CLAP の形式 | MIT |
| [clap-wrapper](https://github.com/free-audio/clap-wrapper) | CLAP から VST 3 の生成 | MIT |
| [{fmt}](https://github.com/fmtlib/fmt) | clap-wrapper の依存 | MIT |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) | VST 3 の形式 | MIT |

VST is a registered trademark of Steinberg Media Technologies GmbH.

## 更新履歴

### 1.0.0

- 最初の版です。
