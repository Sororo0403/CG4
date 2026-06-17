# CG4 評価課題1（演出王 Part3）

## 作品概要

HitEffect っぽい炎の拡散エフェクトを作成しました。
スペースキーを押すと、画面中央付近に炎の発光、リング状の広がり、火花、煙を組み合わせた GPU パーティクルエフェクトが発生します。

## 操作方法

- Space: 炎のヒットエフェクトを発生
- Esc: アプリケーションを終了

## 実装内容

- GPU パーティクルによる半透明エフェクト
- 加算合成パーティクルとアルファ合成パーティクルの分離
- `flame_spread_atlas.png` を使った 2 x 2 アトラステクスチャ表現
- ソフト円、リング、火花、煙の形状を組み合わせた演出
- 拡散方向、速度、サイズ、寿命にばらつきを持たせた炎の広がり

## 使用アセット

- `app/resources/textures/flame_spread_atlas.png`

未使用テクスチャは削除済みです。

## ビルド方法

Visual Studio で `CG4.slnx` を開き、次の構成でビルドしてください。

- Configuration: `Debug`
- Platform: `x64`

コマンドで確認する場合は、Visual Studio の MSBuild から次を実行します。

```bat
MSBuild.exe CG4.slnx /p:Configuration=Debug /p:Platform=x64
```

## 実行ファイル

Debug x64 ビルド後の実行ファイルは次に出力されます。

```text
generated/outputs/x64/Debug/CG4/CG4.exe
```

## 補足

アプリ起動時にリポジトリルートをアセットルートとして設定しているため、Visual Studio から実行しても `app/resources/textures/flame_spread_atlas.png` が読み込まれます。
