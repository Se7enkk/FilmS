# FilmS

Windows 版胶片模拟软件，移植自 [SpektraLab](https://github.com/JamesQiu2005/SpektraLab)（macOS Metal/Vulkan）。

## 功能

- **胶片模拟**：Portra 400、Provia 100F、Velvia 50、Gold 200 等多款胶片预设
- **RAW 解码**：基于 LibRaw，支持 CR3、ARW、NEF 等常见格式
- **片边（Overscan）**：Vulkan compute shader 实时渲染，含胶片名、片门孔、背纸
- **日期后背**：LCD 数码管风格，自动读取 EXIF 拍摄日期，支持自定义
- **显影前/后调节**：曝光、色温、色调、RGB 通道、曲线、亮度/对比度/饱和度
- **对比原图**：按住空格或按钮查看原始照片
- **导出**：TIFF 16-bit / PNG / JPEG

## 构建依赖

- CMake >= 3.25
- Qt 6.5+ (MinGW 或 MSVC)
- Vulkan SDK (headers + glslang)
- LibRaw 0.22.x
- C++17 编译器 (MinGW 13+ 或 MSVC 2022)

## 编译

```powershell
# 1. 安装依赖到 deps/ 目录
# 2. 配置
cmake -B build/qt -S . -G "MinGW Makefiles"
# 3. 编译
cmake --build build/qt --target SpektraLabQt
```

## 致谢

- 原项目：[SpektraLab](https://github.com/JamesQiu2005/SpektraLab) by JamesQiu2005
- 胶片模拟算法参考 Film Simulation 开源社区

## 协议

GPL v3，详见 [LICENSE](LICENSE)。
