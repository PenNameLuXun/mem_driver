# 内存归属分析学习版

这个仓库提供一个“驱动 + 用户态”配合的最小学习实现：

- 驱动负责底层查询与结构化返回
- 用户态负责进程枚举、符号解析、展示与聚合归因

## 目录

- `shared/memattrib_shared.h`
  驱动和用户态共用的 IOCTL/结构体协议
- `driver/memattrib_driver.c`
  最小 WDM 驱动，支持单地址查询和分批枚举虚拟内存区域
- `driver/memattrib.inf`
  用于测试安装的 INF 样例
- `user/main.cpp`
  控制台工具，负责进程枚举、符号解析、摘要展示
- `user/CMakeLists.txt`
  用户态构建脚本

## 系统架构

```text
+---------------------------+            DeviceIoControl / IOCTL             +---------------------------+
| User Mode: MemAttribCli   | <-------------------------------------------> | Kernel Mode: MemAttrib    |
|                           |                                               | Driver                    |
| - 进程枚举                |                                               |                           |
| - 符号解析                |                                               | - 地址空间查询            |
| - 聚合归因                |                                               | - 虚拟内存区域枚举        |
| - 控制台展示              |                                               | - 结构化结果返回          |
+---------------------------+                                               +---------------------------+
```

## 职责分工

| 模块 | 职责 |
|---|---|
| 驱动 | 接收 PID/地址请求，进入目标进程地址空间，调用 `ZwQueryVirtualMemory`，返回结构化结果 |
| 用户态 | 枚举进程、调用驱动、做符号解析、做聚合统计、输出分析结果 |
| 共享头 | 定义 IOCTL、请求结构、响应结构，保证驱动与用户态协议一致 |

## 设计思路

### 驱动做什么

驱动只做内核态更适合做的事情：

- 接收 `PID + 地址` 或 `PID + 起始地址 + 批大小`
- 进入目标进程地址空间
- 调用 `ZwQueryVirtualMemory`
- 返回统一的 `MEMATTRIB_REGION_INFO`
- 对 `MEM_IMAGE` / `MEM_MAPPED` 尝试补充映射文件路径

这样用户态不需要自己处理内核细节，只面对结构化结果。

### 用户态做什么

用户态负责分析层：

- 进程枚举：`CreateToolhelp32Snapshot`
- 驱动调用：`DeviceIoControl`
- 符号解析：`DbgHelp` + `SymFromAddr`
- 聚合归因：
  - 按内存类型汇总
  - 按保护属性汇总
  - 按映射文件路径汇总
- UI 展示：控制台输出

## 驱动接口

| IOCTL | 输入 | 输出 | 作用 |
|---|---|---|---|
| `IOCTL_MEMATTRIB_QUERY_REGION` | `MEMATTRIB_REGION_REQUEST` | `MEMATTRIB_REGION_INFO` | 查询指定地址属于哪个虚拟内存区域 |
| `IOCTL_MEMATTRIB_SNAPSHOT_REGIONS` | `MEMATTRIB_SNAPSHOT_REQUEST` | `MEMATTRIB_SNAPSHOT_RESPONSE` | 分批遍历一个进程的虚拟内存区域 |

### `IOCTL_MEMATTRIB_QUERY_REGION`

输入：

- `MEMATTRIB_REGION_REQUEST`

输出：

- `MEMATTRIB_REGION_INFO`

用途：

- 查询某个地址归属到哪个虚拟内存区域

### `IOCTL_MEMATTRIB_SNAPSHOT_REGIONS`

输入：

- `MEMATTRIB_SNAPSHOT_REQUEST`

输出：

- `MEMATTRIB_SNAPSHOT_RESPONSE`

用途：

- 从起始地址开始，分批遍历一个进程的虚拟内存区域

## 用户态示例

列出进程：

```powershell
MemAttribCli.exe --list
```

查看某个进程的聚合摘要：

```powershell
MemAttribCli.exe --pid 1234 --summary
```

查看某个进程所有区域：

```powershell
MemAttribCli.exe --pid 1234 --regions
```

查询某个地址并尝试做符号解析：

```powershell
MemAttribCli.exe --pid 1234 --addr 0x7ff712340000
```

## 构建建议

## 环境依赖

### 必备软件

- Visual Studio 2022 Community 或更高版本
- Windows Driver Kit (WDK) 10.0.26100
- CMake 3.21+

### 安装命令

安装 WDK：

```powershell
winget install --id Microsoft.WindowsWDK.10.0.26100 --exact --accept-package-agreements --accept-source-agreements
```

给 VS2022 增加驱动开发组件：

```powershell
Start-Process -FilePath "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" `
  -ArgumentList 'modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" --channelId VisualStudio.17.Release --productId Microsoft.VisualStudio.Product.Community --add Component.Microsoft.Windows.DriverKit --add Component.Microsoft.Windows.DriverKit.BuildTools --passive --norestart' `
  -Verb RunAs -Wait
```

如果驱动编译时提示缺少 Spectre 相关库，再补这个组件：

```powershell
Start-Process -FilePath "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" `
  -ArgumentList 'modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" --channelId VisualStudio.17.Release --productId Microsoft.VisualStudio.Product.Community --add Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64.Spectre --passive --norestart' `
  -Verb RunAs -Wait
```

### 驱动

仓库已经附带了一个 VS/WDK 工程：

- `learn_win_driver.sln`
- `driver/MemAttribDriver.vcxproj`

使用方式：

1. 安装 Visual Studio 2022 和对应版本的 WDK
2. 打开 `learn_win_driver.sln`
3. 选择 `x64` 和 `Debug` 或 `Release`
4. 编译 `MemAttribDriver`

如果本机 WDK 安装正常，输出里会生成 `MemAttribDriver.sys`。

如果你已经有现成的 WDK 工程，也可以继续使用自己的工程，只把当前源码拷进去。

命令行构建：

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\learn_win_driver.sln /t:Build /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:SupportsPackaging=false
```

构建产物位置：

```text
build-driver\Release\MemAttribDriver.sys
```

### 用户态

```powershell
cmake -S user -B build-user
cmake --build build-user --config Release
```

构建产物位置：

```text
build-user\Release\MemAttribCli.exe
```

## 运行命令

### 运行用户态程序

列出进程：

```powershell
.\build-user\Release\MemAttribCli.exe --list
```

查看某个进程的聚合摘要：

```powershell
.\build-user\Release\MemAttribCli.exe --pid 1234 --summary
```

查看某个进程所有内存区域：

```powershell
.\build-user\Release\MemAttribCli.exe --pid 1234 --regions
```

查询某个地址的归属并尝试解析符号：

```powershell
.\build-user\Release\MemAttribCli.exe --pid 1234 --addr 0x7ff712340000
```

### 驱动加载说明

当前仓库已经能成功编译出 `.sys`，但默认还没有完成签名与正式安装流程。

也就是说，用户态程序在真正运行这些查询命令前，需要先把驱动加载出来；否则会出现无法打开 `\\.\MemAttrib` 的报错。

如果只是验证用户态参数和进程枚举，`--list` 不依赖驱动。

### 重启后继续做什么

如果你已经执行过测试签名模式配置：

```powershell
bcdedit /set testsigning on
```

那么需要先重启系统，让测试签名模式真正生效。

重启完成后，建议按下面顺序继续：

1. 用管理员 PowerShell 或管理员 CMD 启动驱动

```powershell
sc start MemAttrib
```

2. 检查驱动状态

```powershell
sc query MemAttrib
```

如果驱动加载成功，应该能看到类似 `RUNNING` 的状态。

3. 运行用户态程序验证设备是否可访问

列出进程：

```powershell
.\build-user\Release\MemAttribCli.exe --list
```

查询内存归属摘要：

```powershell
.\build-user\Release\MemAttribCli.exe --pid 4 --summary
```

如果驱动已经正常加载，`--summary` / `--regions` / `--addr` 这些命令就不应再报 `failed to open \\.\MemAttrib`。

4. 如果驱动没有启动成功

可以优先检查：

- 当前系统是否真的已经进入测试签名模式
- 驱动文件是否仍在 `build-driver\Release\MemAttribDriver.sys`
- 驱动服务是否已注册为 `MemAttrib`

对应命令：

```powershell
sc query MemAttrib
sc qc MemAttrib
```

## 实验步骤

### 1. 安装依赖

先安装 WDK，并给 Visual Studio 2022 补齐驱动构建组件。

### 2. 构建驱动

执行驱动构建命令，确认生成：

```text
build-driver\Release\MemAttribDriver.sys
```

### 3. 构建用户态程序

执行 CMake 构建，确认生成：

```text
build-user\Release\MemAttribCli.exe
```

### 4. 加载驱动

将驱动签名并加载到系统中，确保用户态能够成功打开 `\\.\MemAttrib`。

### 5. 执行分析

可按下面顺序验证：

1. `MemAttribCli.exe --list`
2. `MemAttribCli.exe --pid <pid> --summary`
3. `MemAttribCli.exe --pid <pid> --regions`
4. `MemAttribCli.exe --pid <pid> --addr <address>`

## 预期输出

### 进程枚举

输出 PID 与进程名，例如：

```text
  1234  notepad.exe
  5678  explorer.exe
```

### 聚合摘要

输出按内存类型、保护属性、文件归属的汇总结果，例如：

```text
[By Type]
         MEM_IMAGE  0x1A3000
        MEM_MAPPED  0x50000
       MEM_PRIVATE  0x7C000
```

### 区域详情

输出每个区域的基址、大小、保护属性、类型、映射文件路径。

### 地址查询

输出目标地址所在区域，并尽量给出最近的符号名。

## 可提交内容建议

如果这是课程作业，建议最终提交以下内容：

- 源码目录
- README 使用说明
- 驱动与用户态模块分工说明
- 几张运行截图
- 一份简短实验总结

## 一次性命令汇总

安装 WDK：

```powershell
winget install --id Microsoft.WindowsWDK.10.0.26100 --exact --accept-package-agreements --accept-source-agreements
```

构建驱动：

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\learn_win_driver.sln /t:Build /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:SupportsPackaging=false
```

构建用户态：

```powershell
cmake -S user -B build-user
cmake --build build-user --config Release
```

运行用户态：

```powershell
.\build-user\Release\MemAttribCli.exe --list
```

## 学习重点

这个版本适合做课程作业或学习演示，重点在于分层：

- 驱动只提供可靠的底层事实
- 用户态做更复杂、更易变的分析逻辑

你后续还可以继续扩展：

- 增加线程栈归属分析
- 增加模块边界与 PE 节区归因
- 增加 VAD 级别标签
- 增加 GUI
- 增加“可疑 RWX 区域”筛选

## 限制说明

- 当前是学习用样例，不包含生产级安全控制
- 驱动直接暴露按 PID 查询能力，正式产品需要更严格的访问控制
- 符号解析依赖目标进程模块与本机符号环境，结果可能为空
- 控制台 UI 便于调试，若作业要求图形界面，可以在现有分析层外面再包一层 Qt/WPF

- 当前附带的 `.vcxproj` 是便于课程实验的最小工程模板；不同 WDK 版本的属性名可能略有差异，如果 VS 打开后提示升级或修复，按提示调整即可
