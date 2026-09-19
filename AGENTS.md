## 项目总述
- godot的fork项目，主要关注自定义的渲染管线
- 采用Vulkan + HLSL，不关心其他的API
- 不兼容Godot的Shader Material，只借用编辑器和运行时

## Compile Command
`scons platform=windows target=editor arch=x86_64 dev_build=yes optimize=none debug_symbols=yes precision=single vulkan=yes d3d12=no opengl3=no angle=no accesskit=no module_mono_enabled=no compiledb=yes extra_suffix=yutrel`