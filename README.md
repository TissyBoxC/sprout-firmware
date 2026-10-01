<div align="center">

<img src="assets/brand/sprout/brand_avatar.png" alt="如此萌屋" width="180" />

# 如此萌屋 · 芽系列 · 初芽 固件

面向 ESP32-S3 N16R8 的模块化设备固件

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-ESP32--S3-E7352C?logo=espressif&logoColor=white)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-F5822A?logo=platformio&logoColor=white)](https://docs.platformio.org/)
[![C](https://img.shields.io/badge/C-FreeRTOS-00599C?logo=c&logoColor=white)](https://www.freertos.org/Documentation/RTOS_book.html)

<!-- COMMUNITY_LINKS_START: 群链接待补充。 -->
<!-- DOCUMENTATION_LINKS_START: 项目文档入口待补充。 -->

</div>

使用 ESP-IDF 与 PlatformIO 构建，构建产物统一输出到 `bgen/`。

## 职责

- 设备启动、能力探测、模块注册和故障恢复。
- 语音唤醒、录音、播放和与实时语音服务的安全连接。
- 网络、家长策略、内容缓存、设备身份和 OTA 更新。
- 摄像头、显示、触摸、蜂窝网络和电池等能力按构建选项独立增删。

设备端只保存设备身份、必要配置和缓存，不保存上游模型密钥。量产固件必须
启用安全启动、Flash 加密、签名 OTA 和看门狗，并关闭量产调试接口。

## 模块化

- 每个功能都是独立模块，可以按构建配置增删。
- 摄像头、屏幕、触摸、蜂窝网络、电池等硬件能力分别由独立模块负责。
- 关闭可选功能后，不链接对应源码和依赖，也不影响无关模块。
- 模块命名描述能力，不使用兔子、熊、猫、机器人等外观或角色名称。

## 目录

```text
components/     可按功能增删的 ESP-IDF 组件
src/            仅负责启动和组装各模块
include/        对外共享的设备级头文件
boards/         板级引脚与硬件差异配置
test/           模块和集成测试
bgen/           PlatformIO 构建输出，不提交
assets/brand/   品牌展示资源，不参与固件运行
```

## 构建

```powershell
platformio run -e esp32-s3-n16r8
```

## 技术栈与开源组件

| 技术 | 用途 | 官方文档 | 许可证 |
| --- | --- | --- | --- |
| ESP-IDF | ESP32-S3 应用框架 | [文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/) | Apache-2.0 |
| FreeRTOS Kernel | 实时任务调度 | [文档](https://www.freertos.org/Documentation/RTOS_book.html) | MIT |
| PlatformIO | 构建、测试与烧录 | [文档](https://docs.platformio.org/) | Apache-2.0 |

第三方组件保持各自许可证；本项目会保留其版权和许可证声明，不重新授权第三方代码。

构建产物统一写入 `bgen/`。该目录只用于本地构建，不进入 Git。

## 质量门禁

```powershell
platformio run -e esp32-s3-n16r8
```
