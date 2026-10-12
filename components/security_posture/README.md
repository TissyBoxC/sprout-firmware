# security_posture

量产固件在 `app_main` 的最前面校验硬件安全姿态：只有同时启用安全启动
（Secure Boot）和 Flash 加密，镜像才允许继续启动。任一缺失时记录稳定的
日志错误码并进入复位循环，设备永远不会进入模块、配网或联网逻辑，因此未
签名或可读密钥的固件无法泄露设备密钥与儿童数据。

## 构建开关

- `CONFIG_FEATURE_SECURITY_POSTURE`：是否链接本模块，默认开启。
- `CONFIG_SPROUT_PRODUCTION_BUILD`：量产镜像开关。开启后缺少安全启动或
  Flash 加密会直接拒绝运行；开发与出厂烧录镜像保持关闭。

量产镜像必须同时烧录签名密钥并启用 Flash 加密，再打开
`CONFIG_SPROUT_PRODUCTION_BUILD`，否则镜像只会在复位循环中拒绝启动。
