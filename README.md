# FileSafe

FileSafe 是一个本地运行的文件加密管理工具：把整个目录加密成一个个独立的加密文件，并通过内置的 Web 界面浏览、预览、下载、追加和删除。

- 后端：C++17 + Boost.Asio/Beast + OpenSSL，单进程 HTTP 服务
- 前端：纯 HTML/CSS/JavaScript，构建时以二进制资源形式嵌入可执行文件
- 加密：AES-256-CTR，密钥由 PBKDF2-HMAC-SHA256 派生，文件头内置 HMAC-SHA256 密码校验

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 目录加密 | 递归遍历目录，将每个文件加密为 `<存储目录>/<目录名>/<id>.bin` |
| 打开目录 | 校验密码后生成会话令牌，按文件树浏览内容 |
| 在线预览 | 文本、图片、音频、视频等按扩展名分类预览，支持 HTTP Range 分段读取 |
| 下载文件 | 支持 `download=1` 触发附件下载，并设置 `nosniff` 等安全响应头 |
| 追加文件 | 把本机路径（文件或目录）加密加入已有加密目录 |
| 删除内容 | 按相对路径删除文件或整个子目录 |
| 导出内容 | 将文件或子目录解密导出到 `<导出目录>/<库名>/` |
| 后台任务 | 加密/追加/删除/导出任务串行执行，前端轮询进度 |
| 命令行配置 | 通过 CLI11 解析端口、日志等级、存储目录与导出目录 |
| 静态页面 | Web UI（主界面与图片/音频/视频库页面）嵌入在可执行文件中，无需额外部署静态文件 |

## 项目结构

```
FileSafe/
├── CMakeLists.txt              # 顶层构建脚本：可执行目标、依赖、输出目录
├── appInfo/                    # 应用元信息（版本、图标、Windows manifest/资源）
│   ├── AppInfo.cmake           # 生成 AppInfo.h 与 Windows 资源
│   ├── config/                 # 模板：AppInfo.h.in / winRC.rc.in / winManifest.manifest.in / winLogo.ico
│   ├── include/                # AppExtension.hpp / WinInfo.h
│   └── output/                 # configure_file 生成结果（不纳入版本管理）
├── resource/                   # 静态资源 -> C++ 数组的打包工具
│   ├── CMakeLists.txt          # 构建 generate 工具并生成 data.h
│   ├── include/resource.h      # 资源查询接口（数量/索引/名称/大小/数据/存在性）
│   └── src/                    # generate.cpp 生成器、resource.cpp 运行时
├── static/html/                # 被打包进程序的 Web 界面（资源名以 /html/ 为前缀）
│   ├── index.html              # 主界面：文件树、预览、后台任务面板
│   ├── audios.html             # 音频库页面
│   ├── images.html             # 图片库页面
│   ├── videos.html             # 视频库页面
│   ├── 404.html                # 未命中页面
│   ├── favicon.ico / favicon.svg
│   ├── css/                    # index.css / audios.css / images.css / videos.css
│   ├── js/                     # index.js / audios.js / images.js / videos.js / theme.js
│   └── lib/jsmediatags.js      # 音频标签读取（第三方库）
├── src/
│   ├── main.cpp                # 入口：解析命令行参数、设置日志等级与存储/导出目录、启动 App
│   ├── Logger.hpp              # 多级别日志（DEBUG/LOG/INFO/WARN/ERROR），支持控制台与文件输出
│   ├── App.h / App.cpp         # 路由注册与各 API 处理器、静态资源分发
│   ├── Utils.h / Utils.cpp     # JSON/URL/Range/资源读取等工具
│   ├── CryptoFile.h/.cpp       # 单文件加解密流（AES-256-CTR + PBKDF2 + HMAC）
│   ├── CryptoStore.h/.cpp      # 加密仓库：目录加密、配置读写、文件读写
│   ├── EncryptQueue.h/.cpp     # 单线程后台任务队列（加密/追加/删除/导出）
│   ├── SessionStore.h/.cpp     # 会话令牌管理（30 分钟 TTL）
│   ├── cli11/CLI11.hpp         # 第三方单头文件命令行解析库（CLI11 2.4.2）
│   └── http/                   # 轻量 HTTP 框架（基于 Boost.Beast）
│       ├── HttpServer.h/.cpp   # 当前参与编译
│       └── HttpClient.h/.cpp   # 源码保留，CMake 中未参与编译
└── bin/                        # 构建产物输出目录；bin/data 为默认存储目录，bin/output 为默认导出目录
```

## 构建

### 依赖

- CMake ≥ 3.10，C++17 编译器
- Boost（`asio`、`process`、`filesystem`、`regex`、`json`、`beast`，需提供 CMake config 包）
- OpenSSL（`SSL`、`Crypto`）
- Windows 额外链接 `ws2_32`、`mswsock`

> 命令行解析使用仓库内置的单头文件库 `src/cli11/CLI11.hpp`（CLI11 2.4.2），无需额外安装依赖。

### 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

可执行文件会输出到 `bin/`（由 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 指定）。构建过程中会先编译资源生成器，把 `static/` 下的页面转换成 `data.h`，再链接进 `FileSafe_resource` 静态库。

> 链接选项使用了 `-static`、`-static-libstdc++`、`-static-libgcc`，因此推荐使用 MinGW/MSYS2 或支持静态链接的工具链。

## 运行

```bash
./bin/FileSafe                                  # 默认监听 127.0.0.1:8080，日志等级 info
./bin/FileSafe -p 9000                          # 指定端口
./bin/FileSafe -l info                          # 指定日志等级
./bin/FileSafe -d /path/to/data                 # 指定存储目录
./bin/FileSafe -o /path/to/output               # 指定导出目录
./bin/FileSafe -p 9000 -l warn -d /path/to/data -o /path/to/output # 组合使用
```

启动后访问 `http://127.0.0.1:<端口>/` 打开 Web 界面，`./bin/FileSafe --help` 可查看完整参数说明。

### 命令行参数

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `-p, --port <1-65535>` | HTTP 监听端口 | `8080` |
| `-l, --log-level <级别>` | 日志等级，可选 `debug`/`log`/`info`/`warn`/`error`（大小写不敏感） | `info` |
| `-d, --data-dir <路径>` | 存储目录；相对路径以程序所在目录为基准 | 程序目录下的 `data/` |
| `-o, --output-dir <路径>` | 导出目录；相对路径以程序所在目录为基准 | 程序目录下的 `output/` |
| `-h, --help` | 打印帮助信息并退出 | — |

参数校验由 CLI11 完成：端口越界、日志等级不在枚举内、或传入未知参数时，程序会打印错误信息并以非 0 退出码退出。

运行目录约定：

- 存储目录默认是程序所在目录（`argv[0]` 的父目录）下的 `data/`，可用 `-d` 覆盖
- 导出目录默认是程序所在目录下的 `output/`，可用 `-o` 覆盖
- 首次加密某个目录时，会以该源目录名作为加密库名称，若已存在同名库则拒绝

## 加密设计

### 加密库布局

```
<存储目录>/<库名>/
├── store.cfg        # 加密后的配置文件（JSON：版本、名称、创建时间、文件列表）
├── 1.bin            # 第 1 个文件（加密后的完整内容）
├── 2.bin
└── ...
```

`store.cfg` 中记录每个文件的 `id`、原始相对路径 `path` 与明文大小 `size`。文件与配置均使用同一密码独立加密，因此可以单独读取某个文件。

### 单文件格式（`CryptoFile`）

```
+--------+---------+--------+----------------+-------------------------+
| CFS1   | salt    | IV     | verify         | ciphertext              |
| 4 B    | 16 B    | 16 B   | 32 B           | AES-256-CTR 密文流      |
+--------+---------+--------+----------------+-------------------------+
```

- 密钥派生：`PKCS5_PBKDF2_HMAC(password, salt, 100000 次, SHA-256)` 输出 32 字节密钥
- 加密算法：`AES-256-CTR`，计数器初始值为文件头中的 16 字节 IV
- 密码校验：`HMAC-SHA256(key, salt)` 写入 `verify` 字段，读取时用 `CRYPTO_memcmp` 比对
- 读写分块：1 MiB 一块流式处理，避免整文件载入内存
- 随机访问：CTR 模式下按块号推进 IV 计数并定位到对应密文偏移，实现 `seek`，从而支持 HTTP Range 请求

> 注意：CTR 模式未做完整性认证（MAC），`verify` 仅用于校验密码是否正确；如需防篡改需额外引入 AEAD（如 AES-GCM）方案。

## HTTP API

所有接口返回 JSON（`Content-Type: application/json`）；文件下载接口返回原始字节流。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/stores` | 列出存储目录下所有加密库名称 |
| POST | `/api/encrypt` | 提交目录加密任务，body：`{ "dir": "...", "password": "..." }`，返回任务 `id` |
| GET | `/api/tasks` | 查询任务列表与进度（`state`、`filesDone/filesTotal`、`bytesDone/bytesTotal`、`elapsed` 等） |
| POST | `/api/open` | 打开加密库，body：`{ "dir": "<库名>", "password": "..." }`，返回 `token` 与文件列表 |
| POST | `/api/add` | 追加文件/目录，body：`{ "token": "...", "target": "子目录", "path": "本机路径" }` |
| POST | `/api/delete` | 删除文件或目录，body：`{ "token": "...", "path": "相对路径" }` |
| POST | `/api/export` | 导出文件或目录到 `<导出目录>/<库名>/`，body：`{ "token": "...", "path": "相对路径" }` |
| GET | `/api/files?token=...` | 获取当前会话下的文件列表 |
| GET | `/api/file?token=...&id=...&download=0\|1` | 读取文件内容，支持 `Range` 头（206/416） |
| GET | `*` | 其余路径按嵌入资源分发，未命中返回 404 页面 |

任务提交接口返回 `202 Accepted`，前端通过 `/api/tasks` 轮询进度；任务完成后队列回调会刷新对应库的会话文件列表。

## 运行时行为说明

- **日志**：由 `Logger.hpp` 提供，分 `DEBUG`/`LOG`/`INFO`/`WARN`/`ERROR` 五级，输出到控制台并带彩色级别标记与线程号。默认等级 `INFO`，可用 `-l/--log-level` 调整；日志内容为中文，覆盖启动退出、HTTP 监听与请求、任务入队/执行/完成/失败、加密库扫描、加密/追加/删除/导出、会话创建与过期清理等关键节点。此外 `Logger` 还支持关闭控制台输出、单文件/按日期多文件写入（默认关闭）。
- **会话**：令牌为 16 字节随机值的十六进制串，有效期 30 分钟；过期需重新输入密码打开。密码仅保存在内存会话中，不落盘。
- **任务队列**：单工作线程串行执行加密/追加/删除/导出，避免同一库被并发修改；失败任务最多保留 20 条，超出后按顺序清理。
- **静态资源**：`static/html` 下的文件在构建时嵌入程序，资源名以 `/html/` 为前缀，运行时通过 `resource_get` / `resource_exists` 按名称读取；Debug 构建还会定义 `RESOURCE_PATH` 以便直接访问源目录。
- **前端**：`index.html` + `js/index.js` 实现目录树、预览器、后台任务面板与主题切换；`audios.html` / `images.html` / `videos.html` 分别配 `js/audios.js`、`js/images.js`、`js/videos.js` 提供媒体库浏览，音频标签由 `lib/jsmediatags.js` 读取。主题与音量偏好记录在 `localStorage`。

## 开发提示

- `appInfo/output` 由 CMake 生成，不纳入版本管理
- 修改 `static/` 下的页面后重新构建即可刷新嵌入资源（`CONFIGURE_DEPENDS` 会触发资源重新生成）