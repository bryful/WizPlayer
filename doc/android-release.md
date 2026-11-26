# Androidリリースのビルド

JDK 17以上とAndroid SDK Platform 35、Build Tools 36.0.0、NDK 30.0.16248370、CMake 4.1.2を用意します。Android SDKの場所を `ANDROID_HOME` または `android/local.properties` の `sdk.dir` に設定します。

`android` ディレクトリで次を実行します。

```powershell
$snes9x = (Resolve-Path ../third_party/snes9x).Path
.\gradlew.bat :app:assembleRelease "-PwizplayerSnes9xSource=$snes9x"
```

対応ソースアーカイブでは `third_party/snes9x` に同梱したソースを使います。リポジトリからビルドする場合は、同じリビジョンのSnes9x（`1bcc369e89f08243e0a462882fb1f3e42e51de3a`）の実際の絶対パスを指定します。macOS／Linuxでは `./gradlew` を使用します。出力は `android/app/build/outputs/apk/release/app-release-unsigned.apk` です。

`cmake/bugfix_patches.cmake` に列挙した7本の修正IPSを `.patch` から取り込みます。対応ソースアーカイブにはこの7本が入っています。

配布用APKはAndroid SDK Build Toolsの `apksigner sign` でWizPlayerのリリース鍵を使って署名し、`apksigner verify` で検証します。署名済みAPKと対応ソースを `_dl/<version>/Android` に、利用者向け説明書を `_dl/<version>/README.MD` に配置します。署名鍵とパスワードは配布物から除外します。更新版も同じ鍵で署名します。
