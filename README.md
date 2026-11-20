# OBDcomm for Windows

OBDcomm 是 Windows 上的 BLE 串口终端，支持 Windows 10 2004 及以上版本（x64）和 Windows 11 x64。电脑需配备 BLE 适配器。

## 使用

1. 打开 Windows 蓝牙并运行程序。
2. 点击「连接」扫描设备，双击设备或选中后按 Enter 连接。
3. 在「设置」中可配置收发 UUID、编码、发送换行、接收提示音和复制 token 按钮，也可载入最大 8 MiB 的发送文件。
4. 如需启动密码，在「设置」勾选「使用密码」，输入并确认密码后保存。每次重新启动程序都需要密码。解锁后可在设置中取消勾选并保存。

设置保存在 `%LOCALAPPDATA%\OBDcomm\settings.ini`。

## 构建

安装 Visual Studio 2022 或 Build Tools 的 C++ 桌面开发组件，以及包含 C++/WinRT 头文件的 Windows 10/11 SDK。在仓库根目录运行：

```cmd
windows\build.cmd
```

输出文件为 `build\windows\OBDcomm-Windows-x64.exe`。

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。
