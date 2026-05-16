# Qt6 树莓派 ARM 编译说明

本文档给出两种方式：树莓派本机直接编译，以及在 Ubuntu x86_64 主机上交叉编译。因为本项目已经改为 Qt6，Windows 和树莓派端都建议使用 Qt6 构建。

## 方案一：树莓派本机直接编译

适合演示和调试，步骤最简单。

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools

cd ~/NetTransferTool
cmake -S . -B build
cmake --build build -j4
./build/NetTransferTool
```

如果你的树莓派系统仓库没有 `qt6-base-dev`，可以升级 Raspberry Pi OS，或者使用 Qt 官方在线安装器/源码编译安装 Qt6。

如果是无桌面的 Raspberry Pi OS Lite，需要安装桌面环境或使用 X11 转发，否则 Qt Widgets 程序无法显示窗口。

## 方案二：Ubuntu 主机交叉编译 Qt6

推荐环境：

- Ubuntu 22.04/24.04 x86_64
- Raspberry Pi OS 64-bit
- Qt 6.6 或更新版本
- 交叉工具链 `aarch64-linux-gnu-g++`

### 1. 安装主机依赖

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build perl python3 git rsync pkg-config \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
  libfontconfig1-dev libfreetype6-dev libx11-dev libxext-dev libxrender-dev \
  libxcb1-dev libx11-xcb-dev libxcb-glx0-dev libxcb-keysyms1-dev \
  libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev \
  libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev libxcb-render-util0-dev
```

### 2. 准备树莓派 sysroot

假设树莓派 IP 为 `192.168.1.20`，用户名为 `pi`。

```bash
mkdir -p ~/rpi/sysroot/usr
export RPI_IP=192.168.1.20

rsync -avz --delete pi@$RPI_IP:/lib ~/rpi/sysroot/
rsync -avz --delete pi@$RPI_IP:/usr/include ~/rpi/sysroot/usr/
rsync -avz --delete pi@$RPI_IP:/usr/lib ~/rpi/sysroot/usr/
```

同步完成后，需要修正 sysroot 中指向绝对路径的符号链接。Qt 源码目录中通常带有脚本：

```bash
python3 qtbase/bin/sysroot-relativelinks.py ~/rpi/sysroot
```

### 3. 下载 Qt6 源码

下面以 Qt 6.6.3 为例，版本可以按实际需要调整。

```bash
cd ~
wget https://download.qt.io/archive/qt/6.6/6.6.3/single/qt-everywhere-src-6.6.3.tar.xz
tar xf qt-everywhere-src-6.6.3.tar.xz
cd qt-everywhere-src-6.6.3
```

### 4. 准备交叉编译 toolchain 文件

新建 `~/rpi/qt6-rpi-toolchain.cmake`：

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT "$ENV{HOME}/rpi/sysroot")
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH "$ENV{HOME}/rpi/sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

### 5. 配置并编译 Qt6

```bash
mkdir -p ~/rpi/qt6-build
cd ~/rpi/qt6-build

~/qt-everywhere-src-6.6.3/configure \
  -release \
  -opensource -confirm-license \
  -nomake examples \
  -nomake tests \
  -skip qtwebengine \
  -prefix /usr/local/qt6pi \
  -extprefix ~/rpi/qt6pi \
  -hostprefix ~/rpi/qt6pi-host \
  -- \
  -DCMAKE_TOOLCHAIN_FILE=~/rpi/qt6-rpi-toolchain.cmake

cmake --build . --parallel
cmake --install .
```

安装完成后，`~/rpi/qt6pi` 中是目标平台 Qt 库，`~/rpi/qt6pi-host` 中是主机工具。

### 6. 部署 Qt6 到树莓派

```bash
rsync -avz ~/rpi/qt6pi/ pi@$RPI_IP:/usr/local/qt6pi/
```

树莓派上设置运行环境：

```bash
echo 'export PATH=/usr/local/qt6pi/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/qt6pi/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### 7. 交叉编译本项目

```bash
cd ~/NetTransferTool
cmake -S . -B build-rpi \
  -DCMAKE_TOOLCHAIN_FILE=~/rpi/qt6-rpi-toolchain.cmake \
  -DCMAKE_PREFIX_PATH=~/rpi/qt6pi
cmake --build build-rpi --parallel
file build-rpi/NetTransferTool
```

`file build-rpi/NetTransferTool` 应显示 ARM/AArch64 目标架构。随后复制到树莓派运行：

```bash
scp build-rpi/NetTransferTool pi@$RPI_IP:~/NetTransferTool/
ssh pi@$RPI_IP
cd ~/NetTransferTool
./NetTransferTool
```

## Windows 与树莓派互传测试

1. 树莓派运行 `./NetTransferTool`，启动接收端，端口设为 `45454`。
2. Windows 端运行同一项目的 Windows 编译版本，目标 IP 填树莓派 IP，端口填 `45454`。
3. 添加文件或目录，点击“发送”。
4. 树莓派保存目录中应出现相同目录结构，日志显示 SHA-256 校验通过。
5. 反方向测试时，Windows 启动接收端，树莓派填写 Windows IP 后发送文件。
