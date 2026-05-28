# Qt6 网络文件传输工具

作者标识：Liyiguang

这是一个基于 Qt6 Widgets 和 Qt Network 的跨平台网络文件传输工具，可在 Windows 与树莓派 ARM 平台之间互传文件和目录。

## 功能

- Windows、Linux、树莓派 ARM 使用同一套源码编译。
- 支持接收端监听端口，发送端输入目标 IP 和端口后传输。
- 支持单文件、多文件和目录传输。
- 支持空目录创建。
- 大文件按默认 64KB 分块发送，接收端边收边写入磁盘。
- 每个文件使用 SHA-256 校验，防止传输损坏。
- 接收端进行相对路径安全检查，避免 `../` 形式的目录穿越。

## 项目结构

```text
.
├── NetTransferTool.pro          # Qt qmake 工程文件
├── CMakeLists.txt               # CMake 工程文件
├── src/
│   ├── main.cpp                 # 程序入口
│   ├── mainwindow.*             # GUI 界面与交互
│   ├── protocol.*               # 自定义传输协议
│   ├── filetransferclient.*     # 发送端逻辑
│   └── filetransferserver.*     # 接收端逻辑
└── docs/
    ├── final_report.md          # 课程报告初稿
    └── qt6_raspberrypi_build.md # Qt6 树莓派编译说明
```

## Windows 编译

推荐使用你电脑中已安装的 Qt6 套件。

### Qt Creator

1. 打开 `NetTransferTool.pro` 或 `CMakeLists.txt`。
2. 选择 Desktop Qt 6.x 编译套件。
3. 点击“构建”和“运行”。

### CMake

如果 Qt6 已加入 Qt Creator 或系统环境，通常可以直接执行：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

如果你安装的是 Qt6 MinGW 版本，而命令行默认用了 Visual Studio 生成器，需要显式使用 Ninja/MinGW。下面的路径按本机实际 Qt 安装目录替换：

```powershell
set PATH=<Qt安装目录>\bin;<MinGW安装目录>\bin;<Ninja安装目录>;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt安装目录> -DCMAKE_CXX_COMPILER=<MinGW安装目录>\bin\g++.exe -DCMAKE_MAKE_PROGRAM=<Ninja安装目录>\ninja.exe
cmake --build build --parallel
```

如果项目路径包含中文，建议把临时构建目录放到 `C:\tmp` 这类纯英文路径；Qt 的 `moc` 自动生成阶段在某些 Windows 环境下会对中文输出路径不稳定。

## 树莓派直接编译

树莓派系统仓库中如果提供 Qt6，可直接安装开发包：

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools
cmake -S . -B build
cmake --build build -j4
./build/NetTransferTool
```

如果系统仓库没有合适的 Qt6 版本，可以使用 Qt 在线安装器，或参考 `docs/qt6_raspberrypi_build.md` 从源码编译。

## 使用方法

1. 在接收端机器启动程序，设置保存目录和监听端口，点击“启动接收”。
2. 在发送端机器启动程序，输入接收端 IP 和端口。
3. 添加文件或目录，点击“发送”。
4. 接收端日志显示“文件接收完成”后，在保存目录中检查结果。

两台机器需要处在同一局域网，且防火墙允许监听端口通过。
