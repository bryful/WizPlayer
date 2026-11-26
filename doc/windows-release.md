# Windows x64リリースのビルド

Visual Studioの「C++によるデスクトップ開発」、CMake、Ninjaを用意します。x64 Native Tools Command Promptで、対応ソースのルートから実行します。

```bat
cmake -S . -B build/windows-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWIZPLAYER_SNES9X_SOURCE=third_party/snes9x
cmake --build build/windows-release
```

リポジトリからビルドする場合は、Snes9xリビジョン `1bcc369e89f08243e0a462882fb1f3e42e51de3a` のソースフォルダを `WIZPLAYER_SNES9X_SOURCE` に指定します。

配布ZIPには、出力された `WizPlayer.exe`・`SDL2.dll` と、WizPlayer・SDL2・Snes9x・MAME Turbo Fileの各ライセンス、第三者ライセンス表示を収録します。
