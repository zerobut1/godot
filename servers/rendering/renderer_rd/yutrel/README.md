# Yutrel renderer bootstrap

当前实现只清理 Viewport 的最终 RenderTarget，颜色为 `Color(0.15, 0.05, 0.25, 1.0)`。
`yutrel` 只接受 Vulkan，不创建 Forward+ 或 Mobile 场景渲染器。

## 接入路径

`main.cpp` 解析命令行和项目配置 → `RendererCompositorRD` 创建 `RenderYutrel` →
`RendererViewport` / `RendererSceneCull` → `RendererSceneRenderRD::render_scene()` →
`RenderYutrel::_render_scene()` → `TextureStorage` 清屏 → 原有窗口合成。

当前继承 `RendererSceneRenderRD`，保留共享 RD 初始化、资源存储、Canvas 和窗口合成。
天空及部分后处理 Shader 仍会被初始化，但不参与 Yutrel 场景绘制。
这是临时桥接边界；几何、材质、灯光、Gizmo、HLSL 编译和三角形绘制尚未实现。
几何实例仍保留合法的创建、更新和释放流程，暂时不提交绘制。

## 手动运行

在引擎仓库根目录使用根目录 `AGENTS.md` 的 SCons 命令构建。测试项目位于：

```text
D:\Project\godot\godot-yutrel-sandbox
```

项目包含 `main.tscn`（Node3D 和当前 Camera3D），默认仍为 Forward+，驱动已改为 Vulkan。
通过命令行选择 Yutrel：

```powershell
.\bin\godot.windows.editor.dev.x86_64.yutrel.console.exe `
  --path D:\Project\godot\godot-yutrel-sandbox `
  --rendering-method yutrel --rendering-driver vulkan --verbose
```

预期为深紫色窗口。调整尺寸、最小化/恢复、关闭并重新运行，画面应保持正常。
日志应包含 `Yutrel`、`independent scene renderer created` 和 `first viewport clear completed`。
添加 `--editor` 可检查编辑器启动及渲染器名称；3D Gizmo 和网格尚不会显示。
RenderDoc 中可查找 `Yutrel: Clear viewport` 标签。

## 自动回归

需要实际 Vulkan 设备和窗口，不能使用 `--headless` 代替。

```powershell
New-Item -ItemType Directory -Force .\bin\yutrel-validation | Out-Null
.\bin\godot.windows.editor.dev.x86_64.yutrel.console.exe `
  --path D:\Project\godot\godot-yutrel-sandbox `
  --rendering-method yutrel --rendering-driver vulkan `
  --script "$PWD\misc\yutrel\verify_clear.gd" `
  -- "$PWD\bin\yutrel-validation"
```

脚本读取 Viewport 的角落和中心像素，验证清屏颜色、三个窗口尺寸、网格创建/删除以及三次场景重载。
成功时输出 `Yutrel verification: PASS` 并返回 0；截图写入 `bin/yutrel-validation/clear.png`。

2026-09-19：Windows / RTX 5070 Ti 上通过构建、上述回归、重复启动退出、编辑器启动和 Forward+ 对照检查。
命令行的 `yutrel + dummy`、项目设置的 `yutrel + d3d12` 均被拒绝并返回 1。
本机 Vulkan loader 另有 RenderDoc layer 路径失效/重复注册日志，Forward+ 对照中也出现，未修改机器配置。
