# 基于 Qt6 的跨嵌入式平台网络文件传输工具

作者标识：Liyiguang

代码仓库：https://github.com/LoveSeele-max/-_-.git

## 1. 项目概述

本项目采用 Qt6 设计并实现一个网络文件传输工具，设计目标是在 Windows 平台与树莓派 ARM 平台之间进行文件互传。程序基于 Qt Widgets 构建图形界面，基于 Qt Network 中的 `QTcpServer` 和 `QTcpSocket` 实现 TCP 通信。发送端支持选择单个文件、多个文件和目录；接收端支持指定保存目录、监听端口、自动创建目录结构，并对每个文件进行 SHA-256 校验。由于当前阶段未进行树莓派实机测试，运行验证部分采用另一台电脑中的 Ubuntu 虚拟机模拟 Linux/嵌入式接收端，与 Windows 主机进行互传测试。

项目实现了大文件分块传输和目录传输。大文件不会一次性读入内存，而是按默认 64KB 数据块循环读取、发送、写入，适合在树莓派这类内存较小的嵌入式平台上运行。

## 2. 功能需求实现

| 需求 | 实现情况 |
| --- | --- |
| Windows 与 Linux/ARM 文件互传 | 使用 Qt6 跨平台源码，Windows 和 Linux/ARM 编译后可互联传输 |
| Qt6 编译与 ARM 平台运行 | 提供树莓派直接编译与 Ubuntu 交叉编译步骤；本次实际验证使用 Ubuntu 虚拟机 |
| 大文件分块传输 | 默认 64KB 数据块，循环发送和接收 |
| 目录传输 | 递归遍历目录，发送目录创建帧和文件数据帧 |
| 运行日志与进度显示 | GUI 显示当前文件进度、总进度和运行日志 |
| 完整性校验 | 每个文件发送前计算 SHA-256，接收完成后校验 |

## 3. 系统设计思想

程序采用“接收端监听、发送端主动连接”的 C/S 结构。任意一台机器都可以启动接收端，也可以作为发送端连接对方，因此 Windows、Linux 虚拟机和后续树莓派环境之间都可以使用同一套协议进行双向传输。

系统分为四层：

1. 界面层：`MainWindow` 负责参数输入、文件选择、进度条和日志展示。
2. 发送层：`FileTransferClient` 负责遍历文件、连接目标主机、分块发送数据。
3. 接收层：`FileTransferServer` 负责监听端口、解析数据帧、写入文件和校验。
4. 协议层：`Protocol` 定义统一数据帧格式、文件元信息编码和路径安全检查。

这种结构把 GUI、网络传输和协议解析分开，便于调试，也便于后续扩展断点续传、传输速度统计等功能。

## 4. 通信协议设计

本项目在 TCP 字节流之上定义了固定消息头的数据帧协议。每个数据帧由 16 字节消息头和可变长度数据体组成。

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| magic | `quint32` | 4 字节 | 魔数 `QFT1`，用于识别协议 |
| version | `quint16` | 2 字节 | 协议版本，当前为 1 |
| type | `quint16` | 2 字节 | 数据帧类型 |
| payloadSize | `quint64` | 8 字节 | 数据体长度 |

数据帧类型如下：

| 类型 | 作用 |
| --- | --- |
| `PacketMkdir` | 通知接收端创建目录 |
| `PacketBeginFile` | 发送文件名、大小、SHA-256 等元信息 |
| `PacketFileChunk` | 发送文件内容块 |
| `PacketEndFile` | 标记当前文件发送结束 |
| `PacketBatchEnd` | 标记本次批量传输结束 |

TCP 是流式协议，不能保证一次 `readyRead()` 就读到一个完整业务包。因此接收端使用缓冲区保存已收到的数据：先判断是否够 16 字节消息头，再根据 `payloadSize` 判断是否够一个完整数据帧；只有完整帧到达后才进行处理。

## 5. 大文件分块传输算法

发送端算法：

```text
打开文件
计算文件 SHA-256
发送 BeginFile 元信息帧
while 文件未结束:
    读取 64KB 数据块
    封装为 FileChunk 数据帧
    写入 TCP socket
    更新当前文件进度和总进度
发送 EndFile 结束帧
```

接收端算法：

```text
收到 BeginFile:
    创建父目录
    打开目标文件
    初始化 SHA-256 计算器
收到 FileChunk:
    将数据块写入磁盘
    将数据块加入 SHA-256 计算
    更新接收进度
收到 EndFile:
    关闭文件
    比较接收大小与声明大小
    比较 SHA-256
    校验通过后记录接收完成
```

该算法的优点是内存占用稳定。无论文件大小是几十 MB 还是几 GB，程序每次只处理一个小块，适合嵌入式平台。

## 6. 目录传输算法

目录传输使用 `QDirIterator` 递归遍历目录树。发送目录时，程序会保留被选中目录的根目录名。例如选择 `photos` 目录后，接收端保存目录中会生成：

```text
photos/
├── a.jpg
├── b.jpg
└── sub/
    └── c.jpg
```

处理流程：

1. 发送端先为根目录和所有子目录发送 `PacketMkdir` 帧。
2. 对每个普通文件生成相对路径，例如 `photos/sub/c.jpg`。
3. 按普通文件流程发送元信息、文件块和结束帧。
4. 接收端根据相对路径创建目录，并写入文件。

为了安全，接收端不允许绝对路径、盘符路径或包含 `../` 的路径，防止恶意发送端覆盖保存目录之外的文件。

## 7. 关键代码说明

### 7.1 协议编码与路径检查

协议相关代码位于 `src/protocol.cpp`。`encodeHeader()` 和 `decodeHeader()` 负责消息头编码与解析；`normalizeRelativePath()` 和 `safeDestinationPath()` 负责路径清理与安全检查。

### 7.2 发送端

发送端代码位于 `src/filetransferclient.cpp`。`collectTransferItems()` 负责收集文件和目录；`sendOneFile()` 负责发送单个文件；`sendFrame()` 负责把业务数据封装成协议帧写入 TCP。

### 7.3 接收端

接收端代码位于 `src/filetransferserver.cpp`。`socketReadyRead()` 负责从 TCP 字节流中拆出完整协议帧；`handleFrame()` 根据帧类型执行创建目录、打开文件、写入文件块、完成校验等操作。

### 7.4 GUI

界面代码位于 `src/mainwindow.cpp`。程序左侧为接收端配置，右侧为发送端配置，下方显示当前文件进度、总进度和运行日志。发送操作运行在 `QThread` 中，避免大文件传输时界面长时间无响应。

## 8. Qt6 编译过程

### 8.1 Windows 平台编译

安装 Qt6 后，使用 Qt Creator 打开 `NetTransferTool.pro` 或 `CMakeLists.txt`，选择 Desktop Qt 6.x 编译套件并构建。

命令行方式：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

如果 CMake 找不到 Qt6，需要指定 Qt6 安装路径：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.6.3\msvc2019_64
cmake --build build --config Release
```

如果使用 Qt6 MinGW 版本，而命令行默认使用 Visual Studio 生成器，可以显式指定 Ninja 和 MinGW。下面的路径按实际安装目录替换：

```powershell
set PATH=<Qt安装目录>\bin;<MinGW安装目录>\bin;<Ninja安装目录>;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt安装目录> -DCMAKE_CXX_COMPILER=<MinGW安装目录>\bin\g++.exe -DCMAKE_MAKE_PROGRAM=<Ninja安装目录>\ninja.exe
cmake --build build --parallel
```

如果项目路径包含中文，建议将临时构建目录放到 `C:\tmp` 这类纯英文路径，避免 Qt 的 `moc` 自动生成文件阶段受到路径编码影响。

### 8.2 树莓派本机编译

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools
cmake -S . -B build
cmake --build build -j4
./build/NetTransferTool
```

### 8.3 Ubuntu 交叉编译

交叉编译过程包括安装 ARM 工具链、同步树莓派 sysroot、配置 Qt6、编译 Qt6、部署 Qt6 到树莓派、使用 CMake 交叉编译本项目。详细步骤见 `docs/qt6_raspberrypi_build.md`。

## 9. 运行测试与验证

本次实际运行测试没有使用树莓派实机，而是在另一台电脑中启动 Ubuntu 虚拟机作为接收端或发送端，用来验证跨系统网络传输流程。测试过程中发现，如果虚拟机使用 NAT 网络，Ubuntu 虚拟机 IP 与 Windows 主机不在同一网段，发送端容易出现连接超时。将虚拟机网络模式改为桥接模式后，Ubuntu 虚拟机与 Windows 主机处于同一局域网，Windows 端可以直接通过虚拟机 IP 地址连接接收端，文件传输能够顺利完成。

### 测试一：Windows 发送文件到 Ubuntu 虚拟机

1. Ubuntu 虚拟机启动程序，左侧接收端端口设置为 `45454`，保存目录设置为 `~/NetTransferReceived`。
2. Windows 端启动程序，目标 IP 输入 Ubuntu 虚拟机的局域网 IP，端口输入 `45454`。
3. Windows 端选择普通文件并点击发送。
4. Ubuntu 虚拟机保存目录中出现同名文件。
5. 接收端日志显示“文件接收完成”，说明文件传输和校验流程正常。

### 测试二：Ubuntu 虚拟机发送文件到 Windows

1. Windows 端启动接收端，端口设置为 `45454`。
2. Ubuntu 虚拟机端输入 Windows 主机 IP 和端口。
3. Ubuntu 虚拟机端选择文件并发送。
4. Windows 保存目录中出现接收文件，日志显示接收完成。

### 测试三：大文件分块传输

选择较大的文件进行传输，观察当前文件进度条持续变化，说明程序没有一次性加载整个文件，而是按照分块方式连续发送。由于程序接收端已经自动校验 SHA-256，日志无错误即说明内容一致。

### 测试四：目录传输

选择一个包含多级子目录和空目录的目录进行传输。接收完成后检查目录结构是否一致，空目录是否创建成功。

### 测试问题与解决

测试中遇到过两类网络连接问题：

1. Windows 端发送时出现 `The proxy type is invalid for this operation`，原因是 Qt 在 Windows 上可能读取系统代理或 VPN 配置，影响普通 TCP socket 连接。解决方法是在程序启动时和发送端 socket 连接前设置 `QNetworkProxy::NoProxy`，使局域网文件传输不经过系统代理。
2. Windows 端连接 Ubuntu 虚拟机时出现 `Socket operation timed out`，原因是虚拟机最初使用 NAT 网络，虚拟机 IP 与 Windows 主机不在同一局域网，Windows 无法直接连接虚拟机监听端口。将虚拟机网络模式改为桥接模式后，Ubuntu 获得与 Windows 主机同网段的局域网 IP，再使用该 IP 和监听端口 `45454` 进行连接，传输成功。

这说明跨平台网络传输测试不仅需要程序监听端口正确，还需要保证两端网络互通、防火墙放行对应端口，并避免代理配置影响局域网 TCP 连接。

## 10. 运行效果截图

报告中可补充以下截图：

1. Windows 端程序主界面截图。
2. Ubuntu 虚拟机端程序主界面截图。
3. Windows 向 Ubuntu 虚拟机发送文件时的进度截图。
4. Ubuntu 虚拟机保存目录中接收成功的文件截图。
5. 目录传输后的目录结构截图。

## 11. 总结

本项目完成了一个基于 Qt6 的跨平台网络文件传输工具。程序使用 TCP 作为传输基础，在应用层设计了带消息头的数据帧协议，实现了文件元信息传输、分块传输、目录递归传输和 SHA-256 完整性校验。当前阶段已通过 Windows 与 Ubuntu 虚拟机之间的互传测试，验证了跨系统网络传输、分块传输和目录传输流程。由于 Qt6 支持 Windows 和 Linux/ARM 平台，同一套源码后续可继续在树莓派 ARM 平台上编译运行，以完成真实嵌入式设备验证。
