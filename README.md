# GSID Fix - 安卓17澎湃（HyperOS）

修复安卓17澎湃在未加密 `/data` 上安装 DSU 时的镜像映射问题。
基于 [stelios333/gsid-fix-magisk](https://github.com/stelios333/gsid-fix-magisk) 适配。

**[下载安装包](https://github.com/Maga-King/gsid-fix-android17-hyperos/releases/latest)** · [完整说明与构建方法](README-Android17.md)

- 使用实测固件的 Android 17 原生 GSID，仅补回内部存储单文件镜像的 loop 映射分支。
- 开机完成后才挂载，保留系统原版 GSID 的早期启动流程；停用模块并重启即可恢复。
- 支持 arm64；不加 Android 版本或固件安装白名单。
- 实测：安卓17澎湃 `OS4.0.0.37.XBLCNXM`、未加密 F2FS、KernelSU + mountify、SELinux Enforcing。

64 MiB 镜像创建、映射、写入、落盘校验、重新映射、删除，以及模块激活和回退已通过实机测试。
**完整 GSI 安装和启动尚未验证；不保证解决卡第二屏，也不代表适用于所有安卓17澎湃固件。**
本版不支持外置存储或拆分成多片的镜像。

这是基于原生二进制的补丁适配，并非完整 AOSP GSID 源码重编译。
保留上游提交历史、许可证及 `libfiemap_patch.patch`；新增补丁源代码位于 `native/`。
