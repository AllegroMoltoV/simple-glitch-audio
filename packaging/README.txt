Simple Glitch / シンプルグリッチ
================================

Tempo-synced glitch effect: stutters, gates, reverses and bit crushing. Stereo audio effect in VST® 3 and CLAP formats for Windows x64 and Linux x86_64.
入力の音を DAW のテンポに合わせて区切り、繰り返し・無音・逆再生とビット落としで崩すステレオの音声エフェクトです。Windows x64 と Linux x86_64 向けの VST® 3 と CLAP の形式で配布します。

Source code and verification / ソースコードと検証:
  https://github.com/AllegroMoltoV/simple-glitch-audio

Install / 導入
--------------

1. Close your DAW. / DAW を終了します。
2. Windows: copy the folder "VST3\Simple Glitch.vst3" to one of:
   Windows では、「VST3\Simple Glitch.vst3」フォルダーを次のどちらかへコピーします。
     %LOCALAPPDATA%\Programs\Common\VST3\   (only you / 自分だけ)
     C:\Program Files\Common Files\VST3\     (all users, needs administrator / 全ユーザー。管理者権限が必要)
3. Windows, for CLAP hosts: copy "CLAP\Simple Glitch.clap" to one of:
   Windows の CLAP に対応した DAW では、「CLAP\Simple Glitch.clap」を次のどちらかへコピーします。
     %LOCALAPPDATA%\Programs\Common\CLAP\
     C:\Program Files\Common Files\CLAP\
4. Linux: copy the folder "VST3/Simple Glitch.vst3" to ~/.vst3/ and "CLAP/Simple Glitch.clap" to ~/.clap/.
   Linux では、「VST3/Simple Glitch.vst3」フォルダーを ~/.vst3/ へ、「CLAP/Simple Glitch.clap」を ~/.clap/ へコピーします。
5. Start your DAW and rescan plug-ins if needed. / DAW を起動し、必要ならプラグインを再スキャンします。

No installer and no network access. The plug-in does not send any audio or data anywhere.
インストーラーはありません。プラグインは通信しません。音声やデータを外部へ送りません。

Parameters / パラメーター
------------------------

Amount  0 to 100 %. The chance of a glitch in each slice. / 区切りごとにグリッチが起きる確率
Slice   1/32, 1/16, 1/8 or 1/4 note, synced to the DAW's tempo. / 区切りの長さ。DAW のテンポに合わせます
Crush   0 to 100 %. Fewer bits (16 down to 4) and a lower sample rate. / ビット数と標本化の間引き
Mix     0 to 100 %. Processed sound against the original. / 処理した音と原音の混ぜ具合

Each glitch is a stutter (the slice head repeats), a gate (the second half is silent) or a reverse
(the previous slice plays backwards). The latency is 0. While the DAW plays, the same position always
gives the same glitches, so exports are repeatable.
グリッチはスタッター (区切りの頭の繰り返し)、ゲート (後半の無音)、リバース (直前の区切りの逆再生) の
いずれかです。レイテンシーは 0 です。DAW の再生中は、同じ位置で同じグリッチになり、書き出しの結果がそろいます。

Licenses / ライセンス
--------------------

Simple Glitch is released under the MIT License (LICENSE.txt).
Third-party licenses are in THIRD_PARTY_LICENSES.

VST is a registered trademark of Steinberg Media Technologies GmbH.
