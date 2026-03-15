{
  "targets": [
    {
      "target_name": "fluidsynth_addon",
      "sources": ["fluidsynth_addon.cpp"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": ["-std=c++17"],
      "conditions": [
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
              "MACOSX_DEPLOYMENT_TARGET": "12.0",
              "OTHER_CFLAGS": [
                "<!@(pkg-config --cflags fluidsynth 2>/dev/null || echo '')"
              ],
              "OTHER_LDFLAGS": [
                "<!@(pkg-config --libs fluidsynth 2>/dev/null || echo '-lfluidsynth')",
                "-framework CoreAudio",
                "-framework AudioToolbox",
                "-framework CoreMIDI",
                "-framework CoreFoundation"
              ]
            }
          }
        ],
        [
          "OS=='linux'",
          {
            "cflags": [
              "<!@(pkg-config --cflags fluidsynth)"
            ],
            "ldflags": [
              "<!@(pkg-config --libs fluidsynth)"
            ],
            "libraries": [
              "<!@(pkg-config --libs fluidsynth)"
            ]
          }
        ],
        [
          "OS=='win'",
          {
            "libraries": ["-lfluidsynth"],
            "msvs_settings": {
              "VCCLCompilerTool": {
                "AdditionalOptions": ["/std:c++17"]
              }
            }
          }
        ]
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ]
    }
  ]
}
