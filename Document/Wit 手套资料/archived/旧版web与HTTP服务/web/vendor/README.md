# 网页第三方依赖

V1 将 Three.js/OrbitControls 固定为 `0.160.0` 并通过明确版本 CDN 加载；硬件现场需要完全离线时，将对应模块及 MIT 许可证镜像到本目录，再修改 `index.html` import map。

真实人手 GLB 已本地化在 `web/assets/`，不依赖运行时网络；模型来源和 MIT 许可证也保存在该目录。
