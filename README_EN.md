# waifu2x-ncnn-vulkan GUI

This is a visual interface enhancement project based on [nihui/waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan).

This repository contains only the GUI source code, integration patch, and packaging instructions. It does not include the original algorithm implementation, model files, ncnn dependencies, or shader files from the upstream project. The algorithm and command-line core are provided by the original project; this repository does not reimplement or replace the waifu2x algorithm.

## Algorithm Sources

- The base project integrated by this GUI (algorithm inference, model loading, and command-line entry point):  
  [nihui/waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan)
- The original waifu2x algorithm project:  
  [nagadomi/waifu2x](https://github.com/nagadomi/waifu2x)
- The original waifu2x online demo page:  
  [https://waifu2x.udp.jp/](https://waifu2x.udp.jp/)

## Features

- Built with Dear ImGui (docking branch) + GLFW + OpenGL 3, keeping the interface lightweight.
- Preserves the original command-line tool `waifu2x-ncnn-vulkan-cli` and adds the GUI program `waifu2x-ncnn-vulkan-gui`.
- Supports model selection, denoise level, upscaling factor, GPU selection, TTA, thread count, output format, and other settings.
- Supports adding multiple input files, progress display, start/cancel, and before/after preview.
- Integrates with the original project through a small patch and does not modify the core waifu2x inference algorithm.

## Directory Structure

```text
.
├── README.md
├── README_EN.md
├── LICENSE
├── GUI_ARCHITECTURE.md
├── setup_gui.ps1
├── patches/
│   └── waifu2x-gui.patch      # Patch for build and a few integration points in the original project
└── src/
    ├── model_config.h         # Model path resolution and prepadding calculation
    └── gui/
        ├── main_gui.cpp       # GUI entry point and main loop
        ├── app_state.h/.cpp   # Global application state
        ├── image_codec.h      # Image encoding/decoding
        ├── processing_thread.h# Background processing thread
        ├── widgets.h          # Shared components such as the file browser dialog
        └── panels/            # Menu, settings, file list, progress, and preview
```

## Build from Source

This repository does not include the upstream source code. Clone the original project first. The patch is based on the following upstream commit:

```text
64914665c45893135c9e50c1c296170a121b9f77
```

Windows PowerShell example:

```powershell
git clone https://github.com/nihui/waifu2x-ncnn-vulkan.git
cd waifu2x-ncnn-vulkan
git checkout 64914665c45893135c9e50c1c296170a121b9f77

# Copy the GUI files from this repository into the original project and apply the integration patch
..\waifu2x-ncnn-vulkan-gui\setup_gui.ps1 -UpstreamPath .

cmake -B build -DBUILD_GUI=ON
cmake --build build --config Release
```

After building, the executables are located at:

```text
build/Release/waifu2x-ncnn-vulkan-gui.exe
build/Release/waifu2x-ncnn-vulkan-cli.exe
```

The CMake configuration fetches GLFW and Dear ImGui automatically through FetchContent, so the first build requires a network connection.

## Use the Packaged App

Download `app/waifu2x-ncnn-vulkan-gui-windows-x64.zip` from this repository, extract it, and run:

```text
waifu2x-ncnn-vulkan-gui.exe
```

Before running, make sure that:

- The graphics driver is installed and supports Vulkan.
- Microsoft Visual C++ 2015-2022 x64 Redistributable is installed on Windows.
- The `models` folder remains at the same level as the program in the extracted directory.

## License

- The GUI code and integration patch follow the original project's license.
- See the original project repository for the copyright and license of the original code and model files.

