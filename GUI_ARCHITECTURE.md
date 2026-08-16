# waifu2x-ncnn-vulkan GUI 架构设计文档

## 概述

将原命令行工具 waifu2x-ncnn-vulkan 改造为可视化界面操作，基于 Dear ImGui + GLFW + OpenGL 3 技术栈。保留原有 CLI 功能，新增 `waifu2x-ncnn-vulkan-gui.exe` 图形界面程序。

## 技术选型

| 组件 | 技术 | 理由 |
|------|------|------|
| GUI 框架 | Dear ImGui (docking 分支) | 轻量、ncnn 生态常用、即时模式渲染 |
| 窗口系统 | GLFW 3.4 | 跨平台窗口创建、输入处理、OpenGL 上下文 |
| 渲染后端 | OpenGL 3.2+ | 不干扰 ncnn Vulkan compute 管线，独立 GPU 上下文 |
| 推理引擎 | ncnn + Vulkan | 复用原有 `Waifu2x` 类，零修改 |

## 架构图

```
┌──────────────────────────────────────────────────────────┐
│                     main_gui.cpp                          │
│               (GLFW + ImGui 主循环 + 事件消费)              │
├──────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ settings     │  │ file_list    │  │ progress      │  │
│  │ _panel.h     │  │ _panel.h     │  │ _panel.h      │  │
│  │              │  │              │  │               │  │
│  │ 模型/降噪/缩放│  │ 文件列表      │  │ 进度条/开始/取消│  │
│  │ GPU/线程/格式 │  │ 添加/移除     │  │               │  │
│  │ 输出目录      │  │ 状态跟踪      │  │               │  │
│  └──────────────┘  └──────────────┘  └───────────────┘  │
│                                                          │
│  ┌──────────────┐  ┌──────────────────────────────────┐  │
│  │ main_menu    │  │ preview_window.h                  │  │
│  │ _bar.h       │  │ (前后对比 / OpenGL 纹理渲染)        │  │
│  └──────────────┘  └──────────────────────────────────┘  │
├──────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────┐    │
│  │ processing_thread.h (后台工作线程)                 │    │
│  │                                                  │    │
│  │  ncnn::create_gpu_instance()                     │    │
│  │  Waifu2x::load() → process() → encode_image()    │    │
│  │  ncnn::destroy_gpu_instance()                    │    │
│  │                                                  │    │
│  │  • 单线程顺序处理 decode → process → encode       │    │
│  │  • atomic 进度计数器 (UI 无锁读取)                 │    │
│  │  • mutex 事件队列 (UI 每帧消费)                    │    │
│  │  • atomic 取消标志 (每图前检查)                    │    │
│  └──────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────┤
│  共享模块                                                 │
│  ┌────────────┐ ┌──────────────┐ ┌───────────────┐      │
│  │ app_state  │ │ model_config │ │ image_codec   │      │
│  │ .h         │ │ .h           │ │ .h            │      │
│  │            │ │              │ │               │      │
│  │ 全局状态    │ │ 模型路径解析  │ │ 图像编解码     │      │
│  │ 设置/文件   │ │ prepadding   │ │ decode_image  │      │
│  │ 进度/预览   │ │ 参数/模型文件 │ │ encode_image  │      │
│  └────────────┘ └──────────────┘ └───────────────┘      │
│  ┌────────────┐                                          │
│  │ widgets.h  │  (文件浏览对话框/共享 UI 组件)             │
│  └────────────┘                                          │
└──────────────────────────────────────────────────────────┘

        ┌──────────────┐
        │   Waifu2x    │  ← 原有类，零修改
        │   waifu2x.h  │
        │   waifu2x.cpp│
        └──────────────┘
```

## 线程模型

```
UI 线程 (main)                      处理线程 (background)
     │                                    │
     │ start(state)                        │
     │──────────────────────────────────>  │
     │                                    │ ncnn::create_gpu_instance()
     │                                    │ Waifu2x::load(model)
     │                                    │
     │                              for each file:
     │                                    │   check cancel flag (atomic)
     │  poll progress (atomic)  <──────── │   decode image
     │  pop events  <─────────────────── │   Waifu2x::process()
     │  update UI                         │   encode image
     │                                    │   push FileCompleted event
     │                                    │
     │                              push AllCompleted
     │                                    │ ncnn::destroy_gpu_instance()
     │                                    │
     ▼                                    ▼
```

线程安全机制：
- **进度**: `std::atomic<int>` — UI 线程每帧无锁读取
- **取消**: `std::atomic<bool>` — worker 每张图前检查
- **事件**: `std::mutex` + `std::queue` — 非阻塞 pop
- **隔离**: worker 启动时复制 `ProcessingSnapshot`，与 UI 状态解耦

## 文件映射

```
src/
├── main.cpp                    # [修改] CLI 入口，调用 model_config
├── model_config.h              # [新增] 模型路径解析和 prepadding 计算
├── waifu2x.h / waifu2x.cpp    # [不变] 核心推理类
├── wic_image.h / webp_image.h  # [不变] 图像编解码
├── filesystem_utils.h          # [不变] 文件系统工具
├── CMakeLists.txt              # [修改] 添加 GUI 目标 BUILD_GUI
├── deps_codec.cmake            # [修改] 目标名更新
│
└── gui/                        # [新增] GUI 模块
    ├── main_gui.cpp            # GUI 入口，主循环
    ├── app_state.h / .cpp      # 全局应用状态
    ├── image_codec.h           # 图像编解码函数(从 main.cpp 提取)
    ├── processing_thread.h     # 后台处理线程
    ├── widgets.h               # 文件浏览对话框等共享组件
    └── panels/
        ├── main_menu_bar.h     # 菜单栏 (File/View/Help)
        ├── settings_panel.h    # 设置面板 (模型/降噪/缩放/GPU/...)
        ├── file_list_panel.h   # 文件列表面板 (添加/状态)
        ├── progress_panel.h    # 进度面板 (进度条/开始/取消)
        └── preview_window.h    # 预览窗口 (前后对比)
```

## 数据流

```
用户操作 → AppState 更新 → 渲染帧
    │
    ▼
 [Start] 按钮
    │
    ▼
 ProcessingSnapshot(复制 AppState) → ProcessingThread::run()
    │
    ▼
 每文件: decode → Waifu2x::process → encode
    │
    ├── atomic progress_current_  → UI 进度条
    ├── push FileStarted/Completed  → UI 文件状态
    └── push AllCompleted/Error    → UI 完成/错误提示
```

## 构建

```bash
# 配置 (自动获取 GLFW + Dear ImGui)
cmake -B build -DBUILD_GUI=ON
cmake --build build --config Release

# 输出
build/Release/waifu2x-ncnn-vulkan-gui.exe  # GUI 程序
build/Release/waifu2x-ncnn-vulkan-cli.exe  # CLI 程序(兼容)
```

## 关键设计决策

1. **Dear ImGui + GLFW** 而非 Qt：保持依赖轻量，不引入大型框架
2. **OpenGL 后端** 而非 Vulkan：避免与 ncnn Vulkan compute 共享设备，减少复杂度
3. **单工作线程** 替代三阶段流水线：简化进度报告和取消逻辑，GUI 场景下吞吐差异可忽略
4. **Waifu2x 类零修改**：最大化复用，降低回归风险
5. **Header-only 面板**：所有面板用 `inline` 函数实现，无需额外的 .cpp 文件
