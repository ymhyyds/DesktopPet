# DesktopPet 编译运行说明

## 一、环境准备

| 依赖 | 版本要求 | 说明 |
|---|---|---|
| Qt | 6.2 及以上 | 需要 Core / Gui / Widgets / Network / Sql 模块 |
| CMake | 3.20 及以上 | |
| 编译器 | MSVC 2019/2022 (推荐) 或 MinGW | Windows 原生桌宠，建议用 MSVC |
| SQLite 驱动 | Qt 自带的 QSQLITE driver | 不需要单独安装 sqlite3，Qt 安装时默认带，确认勾选了 "Qt SQL" 组件即可 |

在 Qt 官方安装器（Qt Maintenance Tool）里，确认勾选了：
- MSVC 2019/2022 64-bit 套件
- Qt 5 Compatibility Module（一般不需要，但如果报缺失模块可以勾上）
- 对应版本的 Sources（可选，调试时有用）

## 二、目录结构检查清单

在编译前确认目录结构完整（按本对话七轮交付的文件放好）：

```
DesktopPet/
├── core/            (第二轮)
├── pet/             (第三轮)
├── ai/              (第四轮)
├── memory/          (第五轮)
├── interaction/      (第六轮)
├── tray/            (第六轮)
├── chat/            (第六轮)
├── characters/
│   └── default/
│       ├── config.json   (本轮提供)
│       ├── idle.gif       ← 需要你自己准备实际的 GIF 素材
│       ├── happy.gif
│       ├── angry.gif
│       ├── shy.gif
│       └── sleep.gif
├── resources/
│   └── resources.qrc (第一轮)
├── database/         (空目录即可，运行时自动建库文件)
├── CMakeLists.txt    (第一轮)
└── main.cpp          (第二轮)
```

⚠️ **重要**：`characters/default/` 下的 5 个 gif 文件需要你自己准备实际的图片素材，
代码里只负责"加载和播放"，不负责生成动画内容。如果暂时没有素材，
可以先随便找几张静态图转成单帧 gif 占位，保证程序能跑起来，后续再替换成正式的 Q 版人物动画。

## 三、配置 DeepSeek API Key

任选一种方式：

**方式一（推荐）：设置环境变量**

Windows PowerShell（当前用户永久生效）：
```powershell
[System.Environment]::SetEnvironmentVariable("DEEPSEEK_API_KEY", "你的key", "User")
```
设置后需要重新打开终端 / 重启 IDE 才会生效。

**方式二：在可执行文件同级目录放置文件**

新建文本文件 `deepseek_api_key.txt`，第一行写入你的 key（不要有多余空格或换行）。
此文件不要提交到 Git，建议加进 `.gitignore`。

## 四、CMake 构建（命令行方式）

PowerShell 下执行（注意你的偏好是 Windows + PowerShell）：

```powershell
# 在 DesktopPet 项目根目录下
mkdir build
cd build

# 配置（把下面路径换成你本机 Qt6 安装路径下的 cmake 配置目录）
cmake .. -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64"

# 编译（Release 版本）
cmake --build . --config Release

# 把 Qt 运行时依赖 dll 拷贝到输出目录旁边（windeployqt），方便直接分发运行
cmake --build . --target deploy --config Release
```

构建产物在 `build/Release/DesktopPet.exe`（Visual Studio 生成器下，多配置目录结构是这样）。

## 五、用 Qt Creator 打开（更省心的方式）

1. 打开 Qt Creator → File → Open File or Project → 选中 `CMakeLists.txt`
2. 选择对应的 Kit（比如 "Desktop Qt 6.7.0 MSVC2019 64bit"）
3. 点击左下角绿色三角形运行即可，Qt Creator 会自动处理依赖 dll 的查找路径（调试模式下）

## 六、运行后的预期效果

- 桌面右下角会出现一个透明背景的圆形/方形 Q 版角色窗口，播放 idle 动画
- 单击角色：随机触发 happy 或 shy 动画
- 双击角色：弹出聊天窗口，输入文字回车发送
- 长按角色（鼠标按住超过 0.6 秒不放）：触发"摸头"动画反馈
- 拖拽角色到屏幕边缘：自动贴边吸附
- 系统托盘会出现图标，右键可以：打开聊天 / 切换角色 / 设置（预留） / 开机自启 / 退出
- 挂机超过 10 分钟没有任何鼠标交互：AI 会主动说一句话

## 七、常见问题排查

| 现象 | 可能原因 | 解决办法 |
|---|---|---|
| 程序启动直接退出，控制台输出"未找到任何角色包" | `characters/default/config.json` 缺失或字段不全（缺 name 或 idle 动画） | 检查 JSON 格式，确认 idle 字段非空 |
| 窗口完全是黑色方块 | gif 文件路径错误或文件损坏，QMovie 加载失败 | 检查控制台 `[AnimationManager] gif 加载失败` 日志，确认文件真实存在 |
| 双击聊天窗口发消息没反应 | 没配置 DeepSeek API Key | 检查环境变量或 `deepseek_api_key.txt`，控制台会输出"缺少 DeepSeek API Key" |
| 编译报错找不到 Qt6Config.cmake | `CMAKE_PREFIX_PATH` 没指对 Qt 安装路径 | 改成你本机实际的 Qt6 安装目录，注意是包含 `lib/cmake/Qt6` 的那一层 |
| 任务栏看不到程序图标，但托盘有图标 | 这是预期行为 | 代码里用了 `Qt::Tool` 窗口标志，桌宠窗口故意不出现在任务栏，靠托盘控制 |

## 八、后续扩展指引（对应需求里的"未来扩展预留"）

| 预留功能 | 建议落地位置 |
|---|---|
| TTS 语音 | 新增 `ai/TTSClient.*`，在 `AnimationManager::oneShotFinished` 或回复到达时触发播放 |
| 嘴型同步 | 在 `AnimationManager` 里加一个"说话中"专属动画状态，配合 TTS 播放时长切换 |
| 屏幕走路 | 新增 `pet/WalkController.*`，用 QTimer 周期性调用 `WindowManager` 提供的新接口做位移 |
| 随机睡觉 | 在 `InteractionManager` 的挂机检测基础上扩展一个更长的二级超时阈值，触发 Sleepy 状态 |
| 自动换装 | 扩展 `CharacterConfig` 增加 `outfits` 字段，`AnimationManager` 支持同状态多套资源轮换 |
| 节日事件 | 新增 `core/EventScheduler.*`，根据日期触发特定 Prompt 或临时换装 |
| 更新系统 | 新增 `core/UpdateChecker.*`，用 `QNetworkAccessManager` 请求版本接口 |
| 插件系统 | 定义 `IPetPlugin` 抽象接口 + 用 `QPluginLoader` 加载 `plugins/` 目录下的 dll |

这些都只是建议落点，不需要现在就实现，但接口预留思路是：**新功能各自成模块，通过信号/槽接进 App 的 `setupConnections()`，不直接侵入已有模块内部**。
