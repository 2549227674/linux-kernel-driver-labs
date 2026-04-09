# Writing Modules

## 实验目标

实现一个带模块参数、内核版本检测和运行时长统计的 Linux 内核模块。

## 知识点

- Out-of-tree 模块编译（Kbuild dual-pass Makefile）
- `module_init` / `module_exit` 生命周期
- `MODULE_LICENSE("GPL")` 与 GPL 符号导出机制
- `module_param` 与 Sysfs 参数接口
- `ktime_get_seconds()` / `time64_t` 时间统计
- `utsname()->release` 获取运行时内核版本

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/hello_version.c` | 内核模块源码 |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 构建与运行

```bash
# 编译模块
make

# 验证产物架构
file hello_version.ko
# 预期输出: ELF 32-bit LSB, ARM, relocatable

# 传输到开发板后加载
adb push hello_version.ko /root/
adb shell insmod /root/hello_version.ko
adb shell dmesg | tail

# 带参数加载
adb shell insmod /root/hello_version.ko whom=World howmany=3

# 运行时修改参数（Sysfs）
adb shell echo Han > /sys/module/hello_version/parameters/whom
adb shell cat /sys/module/hello_version/parameters/whom

# 卸载并查看存活时长
adb shell rmmod hello_version
adb shell dmesg | tail
```

## 关键设计

- `time64_t` 类型：防止 Y2038 问题（32位平台）
- `(long long)` 强转 + `%lld`：跨架构打印兼容性
- `GFP_KERNEL` 分配：`kzalloc` 可休眠，不可在原子上下文使用
