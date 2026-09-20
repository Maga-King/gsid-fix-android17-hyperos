# GSID Fix：安卓17澎湃（HyperOS）适配

原项目：https://github.com/stelios333/gsid-fix-magisk

本次使用连接手机导出的 Android 17 原生 `/system/bin/gsid` 制作二进制补丁，
并未重新编译完整 AOSP GSID，也没有继续使用原项目 Android 14 的 GSID。
原生 Binder、厂商 vold 接口、启动清理逻辑和动态依赖保持不变。

## 适配范围

- 实测系统：安卓17澎湃（HyperOS），Android 17 / API 37，OS4.0.0.37.XBLCNXM，arm64，KernelSU + mountify。
- `/data`：未加密 F2FS，直接挂载 `/dev/block/sda15`。
- 支持内部 `/data/gsi/` 的单文件镜像。普通 F2FS 大镜像也可以是单文件。
- 本版不处理 FAT32/外置存储或拆成多片的镜像；遇到多片会报错退出。
- 按用户要求，不加 Android 版本、固件或哈希安装白名单。
  这不等于已验证所有 Android 17 ROM；当前包仍携带本机固件的原生程序。

## 安装与回退

在 KernelSU/Magisk 管理器安装 `GSID-Fix-Android17-HyperOS-v17.1-local.zip`。
模块带 `skip_mount`，不提供 `system/bin/gsid` 的早期覆盖。
`service.sh` 等 `sys.boot_completed=1` 后，将补丁程序复制到独立 tmpfs，再临时绑定到原路径。
若 GSID 正忙则跳过；若 15 秒 Binder 健康检查失败则撤销本次绑定。
tmpfs 避免直接从 `/data` 绑定文件所继承的 `nosuid` 阻止 init 切换到 gsid 域。
不需要 SELinux permissive，也不添加 sepolicy 放行规则。

查看激活日志：`/data/adb/modules/GSIDFix/activation.log`。
停用模块并重启可恢复系统原版。需要本次开机立即恢复时，在没有 DSU 安装任务时执行：

```sh
su -c 'sh /data/adb/modules/GSIDFix/rollback.sh'
```

若日志显示 GSID 正忙，等任务结束后可手动执行：

```sh
su -c 'sh /data/adb/modules/GSIDFix/service.sh'
```

## 已完成验证

SELinux Enforcing，init 启动的 `u:r:gsid:s0` 进程；使用独立 IImageService 测试目录，
未调用 openInstall、enableGsi，也没有重启：

1. 原版负对照：64 MiB 镜像可创建，映射报 `/data must be mounted on top of device-mapper`。
2. 补丁版：镜像创建、初始零数据、loop 映射、块写入、fsync、读回均通过。
3. 卸载映射后，直接读取 backing file 验证数据一致；再次映射读回一致。
4. 原生解除映射、删除镜像通过；临时挂载与测试目录清理，恢复原版 GSID。
5. 已测试打包后的 service.sh 激活与 rollback.sh 回退，系统程序哈希分别匹配补丁版和原版。

完整 GSI 安装和启动尚未验证。也没有复现或证明用户此前卡第二屏的确切原因。

## 构建

`evidence/gsid.android17` 为从设备导出的原生输入。
Linux/WSL 安装 pyelftools、capstone，配置 Android NDK r29。
从实测固件提取未修改的原生输入；下面的指令布局仅针对上述固件，构建器不是通用自动适配器。

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r29
python3 -m venv .venv
. .venv/bin/activate
pip install pyelftools capstone
mkdir -p evidence
adb pull /system/bin/gsid evidence/gsid.android17
adb pull /system/lib64/libbinder_ndk.so evidence/libbinder_ndk.so
python3 build_native.py
sh build_probe.sh
python3 package_android17.py
```

仅重新打包仓库内已测试的补丁程序，无需 NDK：`python3 package_android17.py`。
仓库不包含个人设备日志、GitHub 凭据或本地测试目录。

`build_native.py` 检查目标指令布局，从原 ELF 解析函数地址，编译 freestanding C/汇编载荷。
复用重复的 GNU property PT_NOTE 为新增 RX PT_LOAD，保留原段地址、动态重定位及依赖。
把 MapImageDevice 的“不能用 device-mapper”分支转到新增 loop fallback，再返回原生析构/返回路径。
源码：`native/loop_fallback.c`、`native/entry.S`；测试：`native/image_probe.cpp`。
构建哈希仅作为审计记录，不用作安装/启动校验。
