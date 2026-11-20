# OBDcomm BLE for Windows

[简体中文](README.zh-CN.md)

OBDcomm is a Bluetooth Low Energy (BLE) terminal for Windows 10 version 2004 or later and Windows 11 on x64. It requires a BLE adapter.

## Download and use

1. Download the Windows x64 executable from [GitHub Releases](https://github.com/carkang/OBDcomm-BLE-Windows/releases) or build from source using the instructions below.
2. Turn on Bluetooth and select **Connect** to scan. Double-click a device or select it and press Enter to connect.
3. Use **Settings** to configure the receive and send UUIDs, encoding, line endings, receive sounds, and the Copy token button. You can also load files up to 8 MiB for transmission.
4. To lock the app at startup, enable **Require a password at startup**, enter and confirm your password, and save. To turn it off, unlock the app, clear the checkbox, and save.

Settings are stored in `%LOCALAPPDATA%\OBDcomm\settings.ini`.

## Interface languages

The app reads the Windows display language at startup. English, Simplified Chinese, Spanish, and Japanese are available. English is the default for other Windows display languages. Restart the app after changing the Windows display language.

## Build from source

Install Visual Studio 2022 or Build Tools with **Desktop development with C++** and a Windows 10 or 11 SDK that includes C++/WinRT. From the repository root, run the following command.

```cmd
windows\build.cmd
```

The executable is created at `build\windows\OBDcomm-Windows-x64.exe`.

## License

[MIT](LICENSE)
