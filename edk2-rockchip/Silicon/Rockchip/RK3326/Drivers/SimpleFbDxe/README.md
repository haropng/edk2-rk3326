# RK3326 SimpleFbDxe

MIPI DSI 480×640 竖屏 GOP 驱动，基于 RK3399 `FrameBufferBltLib` 模式。

## 工作原理

```
U-Boot (6.1 BSP)                    EDK2 DXE
──────────────                      ────────
VOPB WIN1 ARGB8888                   SimpleFbDxe
480×640 32bpp                        ├─ 启用 CRU 时钟
framebuffer @ 动态地址               ├─ 读 VOPB 寄存器取 fb 地址/宽度
                                     ├─ AllocatePages 保护 fb 不被覆盖
                                     ├─ FrameBufferBltLib (Blt)
                                     ├─ GOP 480×640 PixelBlueGreen…
                                     └─ WriteBackInvalidateDataCacheRange
```

## 关键设计决策

| 问题 | 方案 | 原因 |
|------|------|------|
| 无 shadow buffer | GOP fb = VOPB 物理 buffer | 32bpp 格式匹配，零拷贝 |
| Blt 库 | FrameBufferBltLib | RK3399 模式，避免手动字节序 bug |
| fb 地址 | 读 VOPB WIN1_YRGB_MST (0xA0) | U-Boot 动态分配，非固定地址 |
| fb 保护 | `AllocatePages(AllocateAddress)` | 阻止其他驱动覆盖 fb 内存 |
| Cache | `WriteBackInvalidateDataCacheRange` 全帧 | 系统内存类型，DMA 需要 clean |
| 时钟 | CRU gate 启用 + reset 解除 | UEFI handoff 可能 gate 掉 VOPB/DSI |
| VOPB 寄存器 | 只读不写 | CFG_DONE 机制不工作 (PX30 未知寄存器布局) |

## U-Boot 配合修改

`6.1-rksdk/u-boot/drivers/video/drm/rockchip_display.c`:

```c
// display_logo() & display_bmp():
logo->bpp = 32;                          // 强制 32bpp
crtc_state->src_rect.w = hdisplay;       // 面板全宽
crtc_state->src_rect.h = vdisplay;       // 面板全高
crtc_state->crtc_rect = (0,0,hdisplay,vdisplay); // 1:1 满屏
crtc_state->dma_addr = logo->mem;        // 无 offset
dst_size = max(logo_size, 480*640*4);    // 最小全屏 buffer
```

## GraphicsConsole 窄屏修复

`MdeModulePkg/.../GraphicsConsole.c` `InitializeGraphicsConsoleTextMode()`:

- 移除 `ASSERT(MaxColumns >= 80)` — 480px/8=60列 < 80
- DeltaX 钳位到 0 防下溢
- 始终提供 ≥2 text modes

## 已知限制

| 限制 | 说明 |
|------|------|
| UiApp Forms | 480px 下崩溃 (HiiDatabase buf overflow) — 绕开：直接启动 Shell |
| CFG_DONE | PX30 VOPB 寄存器写入不生效 — 完全依赖 U-Boot 预设 |
| 横屏 | 需 U-Boot 中配 VOPB 为 640×480 |
