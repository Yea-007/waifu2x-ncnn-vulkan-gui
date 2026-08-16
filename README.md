# > English version: [README_EN.md](README_EN.md)
# waifu2x-ncnn-vulkan GUI

这是一个基于 [nihui/waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan) 的可视化界面增强项目。

本仓库只包含 GUI 界面代码、集成补丁和打包所需的说明，不包含原项目的算法实现、模型文件、ncnn 依赖和着色器文件。算法与命令行核心均来自原项目，本仓库不重新实现或替换 waifu2x 算法。

## 算法来源

- 本 GUI 所集成的基础项目（算法推理实现、模型加载、命令行入口）：  
  [nihui/waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan)
- waifu2x 原始算法项目：  
  [nagadomi/waifu2x](https://github.com/nagadomi/waifu2x)
- waifu2x 原始在线演示页面：  
  [https://waifu2x.udp.jp/](https://waifu2x.udp.jp/)

## 项目特点

- 使用 Dear ImGui（docking 分支）+ GLFW + OpenGL 3 构建，界面轻量。
- 保留原命令行程序 `waifu2x-ncnn-vulkan-cli`，新增图形界面程序 `waifu2x-ncnn-vulkan-gui`。
- 支持模型选择、降噪等级、放大倍数、GPU 选择、TTA、线程数、输出格式等设置。
- 支持添加多个输入文件、显示处理进度、开始/取消任务、前后对比预览。
- 仅通过少量补丁接入原项目，不修改 waifu2x 的核心推理算法。

## 目录结构

```text
.
├── README.md
├── LICENSE
├── GUI_ARCHITECTURE.md
├── setup_gui.ps1
├── patches/
│   └── waifu2x-gui.patch      # 对原项目构建和少量接入点的补丁
└── src/
    ├── model_config.h         # 模型路径解析与 prepadding 计算
    └── gui/
        ├── main_gui.cpp       # GUI 入口与主循环
        ├── app_state.h/.cpp   # 全局应用状态
        ├── image_codec.h      # 图像编解码
        ├── processing_thread.h# 后台处理线程
        ├── widgets.h          # 文件浏览对话框等共享组件
        └── panels/            # 菜单、设置、文件列表、进度、预览
```

## 从源码构建

本仓库不包含原项目源码，构建前请先克隆原项目。补丁基于以下原项目提交生成：

```text
64914665c45893135c9e50c1c296170a121b9f77
```

Windows PowerShell 示例：

```powershell
git clone https://github.com/nihui/waifu2x-ncnn-vulkan.git
cd waifu2x-ncnn-vulkan
git checkout 64914665c45893135c9e50c1c296170a121b9f77

# 将本仓库的 GUI 文件复制到原项目，并应用集成补丁
..\waifu2x-ncnn-vulkan-gui\setup_gui.ps1 -UpstreamPath .

cmake -B build -DBUILD_GUI=ON
cmake --build build --config Release
```

构建完成后，可执行文件位于：

```text
build/Release/waifu2x-ncnn-vulkan-gui.exe
build/Release/waifu2x-ncnn-vulkan-cli.exe
```

`CMakeLists.txt` 会自动通过 FetchContent 获取 GLFW 和 Dear ImGui，因此首次构建需要网络连接。

## 直接使用打包程序

下载仓库中的 `app/waifu2x-ncnn-vulkan-gui-windows-x64.zip`，解压后运行：

```text
waifu2x-ncnn-vulkan-gui.exe
```

运行前请确认：

- 显卡驱动已安装并支持 Vulkan。
- Windows 已安装 Microsoft Visual C++ 2015-2022 x64 运行库。
- 解压目录中的 `models` 文件夹与程序保持在同一层级。

## 许可证

- GUI 代码和集成补丁与原项目保持一致，使用原项目的 LICENSE。
- 原项目及模型文件的版权和许可请参见原项目仓库。
