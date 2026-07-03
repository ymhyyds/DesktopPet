# DesktopPet 项目上下文

> 最后更新: 2026-07-03 (会话25 — 模型启动缩放默认 1.0 + 用户调整持久化)
> 项目路径: `C:\Users\余敏涵\Desktop\DesktopPet`

---

## 项目概览

一个基于 Qt6 + Live2D Cubism SDK 的桌面宠物程序，**纯 Live2D 模式**。

- 🦊 桌宠窗口（无边框透明、左键拖拽、贴边吸附、右下角固定）
- 🎨 Live2D 模型渲染（Cubism SDK 5 R6，QOpenGLWidget 集成）
- 📐 **窗口紧贴模型包围盒** — 窗口尺寸由模型实际绘制区域的顶点包围盒计算，kBaseSize=200，**外加动画安全边距 ×1.02 放大（会话13 从 ×2.1 下调）**
- 👀 **头部/眼球追踪** — 头部追踪（转头）或眼球追踪（眼睛转），全局光标跟随，光标离开窗口仍跟随
- 🎭 AI 情绪表情驱动（EmotionAnalyzer → 实时模型表情）
- 💬 聊天对话（DeepSeek API，流式输出，猫娘人设）
- 🔄 角色切换（托盘菜单选择，自动扫描新模型）
- 🖱 鼠标交互（左键拖拽、右键互动菜单、**双击开聊天需在模型区域内才触发**、**松手才触发摸摸头**、点击身体播放随机动画）
- ❤️ 好感度系统（右键互动累积，满值 300，QSettings 持久化）
- 🗣️ **TTS 双后端：PowerShell SAPI + VoiceVox 二次元语音（热切换）**
- 🔎 **对话搜索：SQLite LIKE 模糊匹配，逐条浏览**
- 📤 **对话导出：.txt / .json / .html 三种格式**
- 🌤️ 时间/天气感知（实时时间注入，wttr.in 天气）
- 😴 深夜就寝提醒（0:00~6:00 检查系统空闲，TTS + 浮动文字提醒）
- 🖱️ **点击穿透模式** — `WA_TransparentForMouseEvents`，鼠标穿透到桌面
- ⌨️ **全局快捷键** — Ctrl+Alt+P/C/R/S 隐藏/唤出/切换角色/截图
- 📸 **截图分享** — 截取 Live2D 模型（无背景），保存 PNG + 自动复制到剪贴板
- 🔄 **自动扫描新模型** — QFileSystemWatcher + 2秒轮询，`characters/` 目录变化自动识别
- 📋 **剪贴板粘贴为文件** — 截图后文件夹 Ctrl+V 直接贴出 PNG 文件
- 🔄 **Physics/Pose 感知头部角度** — 转头时头发/衣服/配饰物理跟随摆动
- 🖱️ **点击触发随机动画** — 点模型身体播放随机非 Idle 动作；支持自动注册未引用 .motion3.json
- 💬 **说话气泡** — AI 回复时在模型头顶显示漫画风格气泡（6秒后淡出，支持颜色自定义，移动时跟随）

---

## 环境配置

| 工具 | 路径 |
|------|------|
| **Visual Studio** | `C:\Program Files\Microsoft Visual Studio\18\Community` (VS 2026, v143 工具集) |
| **CMake** | VS 内置 `.../CMake/CMake/bin/cmake.exe` (4.3.1) |
| **Qt 6.11.1** | `E:\QT\6.11.1\msvc2022_64` |
| **Live2D SDK** | `E:\Project\Live2D_SDK` (Cubism 5 R6) |
| **DeepSeek API** | `https://api.deepseek.com/chat/completions`，模型 `deepseek-v4-flash` |
| **编译器** | MSVC 19.44.35228.0 |

### 编译方式

```bash
# 构建
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build "C:/Users/余敏涵/Desktop/DesktopPet/build" --config Release

# 部署 Qt DLL
cmake --build build --target deploy --config Release

# 启动
start "" "build/Release/DesktopPet.exe"

# 关闭
powershell -command "Get-Process DesktopPet -EA 0 | Stop-Process -Force"

# 快速构建+启动
powershell -command "Get-Process DesktopPet -EA 0 | Stop-Process -Force"
"C:/Program Files/.../cmake.exe" --build build --config Release 2>&1 | tail -3
start "" "build/Release/DesktopPet.exe"
```

> 首次配置：`cmake .. -G "Visual Studio 18 2026" -A x64 -T v143 -DCMAKE_PREFIX_PATH="E:\QT\6.11.1\msvc2022_64" -DCMAKE_POLICY_VERSION_MINIMUM=3.5`

---

## 模块架构

```
DesktopPet/
├── main.cpp              # 入口
├── CMakeLists.txt        # 构建配置

├── core/
│   ├── App.h/.cpp        # ★ 组装所有模块 + 信号/槽连接 + 天气 + 深夜提醒 + 自动扫描角色
│   ├── GlobalHotkeyManager  # ★ 全局快捷键（Windows RegisterHotKey + nativeEventFilter）
│   └── WindowManager     # 窗口几何管理：贴边吸附、置顶、初始位置

├── pet/
│   ├── PetWindow         # ★ 桌宠窗口：透明/拖拽/右键互动/浮动文字/好感度/喂食/全屏置顶开关/点击穿透/气泡跟随
│   ├── Live2DView        # ★ QOpenGLWidget: Live2D 渲染 + 模型区域命中检测 + isPointInModelArea + 超级采样截图 + 头部追踪状态持久
│   ├── ModelController   # ★ ★ 自定义控制器：眼动追踪 + 情绪表情 + 包围盒 + 位置偏移 + 用户参数覆写 + Physics/Pose 顺序 + 随机动作 + 自动注册未引用动作 + 循环控制
│   ├── CharacterLoader   # 角色包扫描（config.json + .model3.json），支持自动检测
│   ├── AffectionSystem   # ★ 好感度系统（支持加减，满值 300）
│   ├── PetAnimationState # 情绪状态枚举
│   ├── SpeechBubble      # 💬 漫画风格说话气泡（6秒淡出，颜色自定义，跟随窗口移动）
│   └── FloatingTextWidget # ★ 自绘制浮动文字（QPainter translate + opacity，无窗口移动）
│   └── ParticleEffect    # [已删除 2026-07-01 因性能问题]

├── ai/
│   ├── DeepSeekClient    # DeepSeek API 异步客户端（流式）
│   ├── PromptManager     # ★ 人格 Prompt + 实时时间/天气注入
│   ├── EmotionAnalyzer   # 关键词 → 情绪映射
│   ├── TtsEngine         # ★ TTS 双后端：SAPI（PowerShell）/ VoiceVox（HTTP API）
│   └── VoiceVoxClient    # ★ VoiceVox HTTP API（说话人查询 + 语音合成）

├── memory/
│   └── MemoryDB          # SQLite 持久化 + 搜索方法 + 全部消息加载 + clearMessages()

├── interaction/
│   └── InteractionManager # 鼠标事件 → 单击/双击/长按/挂机（doubleClicked 带位置 QPoint）

├── chat/
│   └── ChatWindow        # ★ ★ 聊天界面：搜索 + 导出 + 清除 + 全屏恢复修复 + 背景图

├── tray/
│   └── TrayManager       # 系统托盘 + 角色切换子菜单 + 截图 + 刷新角色 + 模型参数（已移除"打开聊天"菜单项）

├── settings/
│   ├── SettingsWindow    # ★ 设置面板（可缩放+滚动）：TTS 全控制 + 全屏置顶 + 聊天背景 + 截图路径 + 尺寸滑块 + 气泡颜色
│   ├── CropDialog        # ★ 图片裁剪对话框（拖拽调整/微调 + 遮罩预览 + 滚轮缩放）
│   ├── ParameterWindow   # ★ ★ 模型参数独立窗口（独立于设置的浮动窗口）
│   └── ParameterPanel    # ★ ★ ★ 参数滑块列表（平铺靠左，中文名，支持合并参数和多索引控制）

├── characters/
│   ├── default/          # 默认角色（neko/Hiyori），modelScale: 2.0
│   │   ├── config.json
│   │   └── Hiyori/
│   └── xiaoyue/          # 小越角色（canvas 1×2.25）
│       ├── xiaoyue.model3.json
│       ├── 面捕丢失.motion3.json
│       └── 待机动画124.motion3.json

├── database/             # SQLite 数据库文件（运行时生成）
└── resources/
```

---

## ★ ★ 核心改动：Idle 主动交叉淡入淡出闭环（2026-07-01）

### 问题演变

| 阶段 | 方案 | 结果 |
|------|------|------|
| 原 SDK 行为 | `_isLoop` 默认 false（5 R6）+ `Create()` 不读 Loop 标志 | 所有动作播一次就停 |
| #会议7 修复 | Idle `SetLoop(true)` | SDK 时间回绕 `while(time>duration) time-=duration`，`time==endTime` 时参数从末帧瞬跳首帧 → **每 4 秒卡顿** |
| #会议8 第一版 | `_isLoop=false` + `SetFadeInTime(0.5f)` | 卡顿消失，但动画**能看到重播**（从首帧重启） |
| #会议8 最终版 | **主动交叉淡入淡出闭环** | ✅ 零卡顿 + ✅ 零重播感 |

### 最终方案

**放弃 SDK 回绕循环，也不等动画结束再重启。改为在结束前主动启动下一个 Idle，利用 SDK 的淡出/淡入做交叉混合。**

#### 关键参数

```cpp
// PreloadMotionGroup - Idle 动作配置
tmpMotion->SetLoop(false);          // SDK 不自带循环
tmpMotion->SetFadeInTime(0.5f);     // 淡入 0.5 秒（新 Idle 渐入）
tmpMotion->SetFadeOutTime(0.5f);    // 淡出 0.5 秒（旧 Idle 退出）

m_idleDuration = tmpMotion->GetDuration(); // 从 .motion3.json 读取实际时长
```

#### ModelParamUpdate 主动重启逻辑

```
1. IsFinished() + GetCurrentPriority()==0
   → 兜底启动 Idle（首次加载 / Tap 播完后的首次 Idle）
   → m_inProactiveIdleLoop = true
   → m_lastIdleRestartTime = _userTimeSeconds

2. 非 IsFinished（有动作在播）且 m_inProactiveIdleLoop == true
   → 检查 elapsed = _userTimeSeconds - m_lastIdleRestartTime
   → 若 elapsed >= m_idleDuration - 0.5s (结束前 0.5 秒)
     → StartMotion(Idle, 0, PriorityForce)
     → SDK 自动给旧 Idle 设淡出、新 Idle 进队列
     → m_lastIdleRestartTime 更新，下一轮继续

3. Tap 开始时（startRandomTapMotion / startMotionByGroup 非 Idle 组）
   → m_inProactiveIdleLoop = false
   → 主动重启检测不会误触
```

#### 交叉过渡时序

```
旧 Idle:   ████████████████████████████░░░░░    (0.5s 淡出)
新 Idle:                       ░░░░░████████████████████████████
时间轴:    ↑--- 3.5s ---↑-0.5s-↑
                      结束前 0.5s 启动        旧 Idle 完全退出
```

#### 与 SDK 回绕对比

| 方案 | 行为 | 缺陷 |
|------|------|------|
| `_isLoop=true` SDK 回绕 | 参数在 `time==endTime` 时瞬跳（`CorrectEndPoint` 只对 `time<endTime` 生效） | 每 4 秒视觉卡顿 |
| 主动交叉闭环 | 结束前 0.5s PriorityForce 预启动，旧→新自然混合 | ✅ 无条件平滑 |

---

## ★ 鼠标命中检测：只响应模型实际绘制区域（2026-07-02）

### 问题

窗口四周有 55% 动画安全边距，点击边距区域不应触发摸摸头/随机动画/打开聊天。

### 检测原理

```
  ┌─ 宠物窗口 ──────────────────┐
  │  ← 动画安全边距 →              │  ← 点击 → 无反应
  │    ┌─ 模型区域 ────┐          │
  │    │ 头部 (30%)    │ ← 点击 → 摸摸头
  │    │ 身体 (70%)    │ ← 点击 → 随机动画
  │    └──────────────┘          │
  │  ← 动画安全边距 →              │  ← 点击 → 无反应
  └──────────────────────────────┘
```

### 实现

**`Live2DView.cpp` 中的 `computeModelArea()` 静态辅助函数：**

```cpp
// Y 方向：投影 Scale(H/W, 1.0) + ScaleRelative(1/2.1)
//   → 画布高度 = height / 2.1，距顶 = height × 0.55/2.1   ✅ 固定值
// X 方向：宽高比修正后画布宽度 = min(W,H) / 2.1（居中），保守取窄边防越界
static constexpr float kEnlargeFactor = 2.1f;
static constexpr float kModelFrac = 1.0f / kEnlargeFactor;       // ~0.476
static constexpr float kMarginFrac = 0.55f / kEnlargeFactor;     // ~0.262

outY0 = height * kMarginFrac;
outH  = height * kModelFrac;
const float xSize = min(width, height) * kModelFrac;
outX0 = (width - xSize) * 0.5f;
outW  = xSize;
```

**Y 方向用 `height * 0.55/2.1`** 而非 `min(W,H)/2.1`，因为投影的 `Scale(H/W, 1.0)` 分支（canvasWidth ≤ 1 时）在 Y 方向始终是固定比例。

**X 方向用 `min(W,H)/2.1`** 居中，偏保守（避免宽高比修正后越界）。

### 公开接口

`Live2DView::isPointInModelArea(QPoint)` — 供 `App.cpp` 在双击时调用，只有点击在模型区域内才打开聊天。

### 涉及的代码

| 文件 | 改动 |
|------|------|
| `Live2DView.h/.cpp` | 新增 `computeModelArea()` 静态辅助 + `isPointInModelArea()` 公开方法；`mousePressEvent` 改用同一套碰撞逻辑 |
| `InteractionManager.h/.cpp` | `doubleClicked` 信号携带位置 `doubleClicked(const QPoint &pos)` |
| `App.cpp` | 双击连接改为 lambda，先调 `isPointInModelArea` 通过后才打开聊天 |
| `TrayManager.cpp` | 移除"打开聊天"菜单项（双击桌宠即可打开），保留 `openChatRequested` 信号（托盘图标准双击仍可打开） |

---

## ★ 浮动文字定位：相对于模型区域顶部（2026-07-02）

### 改动

`PetWindow::showFloatingText()` 中的定位从窗口顶部改为模型区域顶部：

```cpp
// 旧：定位在窗口顶部上方
QPoint globalTopCenter = mapToGlobal(QPoint(width() / 2, 0));
int y = globalTopCenter.y() - label->height() - 10;

// 新：定位在模型区域上方
const int modelTopY = static_cast<int>(height() * kMarginFrac);  // kMarginFrac = 0.55/2.1
QPoint globalModelTopCenter = mapToGlobal(QPoint(width() / 2, modelTopY));
int y = globalModelTopCenter.y() - label->height() + 30;  // +30 = 伸入模型区域 30px
```

最终值 `+30`（文字底部伸入模型区域 30px），视觉上紧贴模型顶部。

---

## 窗口尺寸与动画安全边距

### 问题

窗口原本按模型静止时的顶点包围盒裁剪（`getModelBounds`），动画播放时抬手、甩尾等超出包围盒的顶点被视口裁掉。

### 解决方案：窗口放大 + 投影补偿

**原则：模型实际绘制像素尺寸不变，但窗口四周多出透明空间。**

```
const double animMarginRatio = 0.55;  // 每边 55% 空余 → 窗口放大 2.1 倍
// 窗口宽高比不变：(modelW + modelW*0.55*2) / (modelH + modelH*0.55*2) = modelW/modelH

const float animMarginCompensation = 1.0f / (1.0f + 0.55f * 2.0f);  // 1/2.1 ≈ 0.476
projection.ScaleRelative(compensation, compensation);
// ↑ 用 ScaleRelative（矩阵乘法）而非 Scale（直接赋值），保留宽高比修正
```

- **`PetWindow::applyTightSize()`** — `modelW/H` 按比例放大
- **`ModelController::ModelOnUpdate()`** — `ScaleRelative` 缩小投影，抵消窗口放大

### 注意

`CubismMatrix44::Scale(x, y)` 直接覆盖 `_tr[0]` 和 `_tr[5]`，不能做累乘。
必须用 `ScaleRelative(x, y)` 做 `_tr = scaleMatrix * _tr`。

### 模型缩放与窗口尺寸计算

`applyTightSize` 窗口尺寸公式：
```
basePx = kBaseSize × m_currentModelScale  (kBaseSize = 200)
modelH = basePx
modelW = basePx × (tightW / tightH)
newW = modelW × 2.1
newH = modelH × 2.1
```

默认角色 `config.json` 中 `modelScale: 2.0`，因此初始窗口 ≈ 599×840（Hiyori）。
设置中可用尺寸滑块调整（范围 0.3~10.0）。

---

## 截图功能

### 流程

```
App::takeScreenshot()
  → Live2DView::grabModelOnly()
      → m_screenshotMode = true
      → setUpdatesEnabled(false)
      → resize(2x)              // 临时调大 widget → resizeGL 同步更新 viewport
      → repaint()               // 同步渲染到高分辨率 FBO
      → grabFramebuffer()       // 读取 2x 像素
      → resize(原)              // 恢复原始尺寸
      → setUpdatesEnabled(true)
      → m_screenshotMode = false
      → update()
      → scaled(2x, SmoothTransformation)  // 平滑缩放 = 超级采样抗锯齿
      → 清理透明像素 RGB 残留
      → 返回 QImage(Format_ARGB32)
  → 保存 PNG（保留透明背景）
  → 剪贴板：QMimeData(图片位图 + 文件路径)
```

---

## 核心信号/槽连接图

```
InteractionManager::doubleClicked(QPoint)      → App [isPointInModelArea?] → ChatWindow::showAndFocus
InteractionManager::idleTimeout                → App → DeepSeekClient::sendMessage
ChatWindow::userMessageSent                    → App → PromptManager + MemoryDB + DeepSeekClient
ChatWindow::clearRequested                     → App → MemoryDB::clearMessages + PromptManager::clearHistory
DeepSeekClient::partialReply                   → ChatWindow::updateAssistantMessage
DeepSeekClient::replyReceived                  → App → ChatWindow + MemoryDB + EmotionAnalyzer + TtsEngine + SpeechBubble
Live2DView::headClicked (松手)                  → PetWindow::triggerPet()
Live2DView::bodyClicked (松手)                  → Live2DView::playTapMotion() (startRandomTapMotion)

SettingsWindow::ttsBackendChanged              → App → TtsEngine::setBackend
SettingsWindow::ttsVoiceChanged                → App → TtsEngine::setVoice
SettingsWindow::keepOnTopToggled               → App → PetWindow::setKeepOnTop
SettingsWindow::clickThroughToggled            → App → PetWindow::setClickThrough
SettingsWindow::modelScaleChanged              → App → PetWindow::setModelScale  (范围 0.3~10.0)
SettingsWindow::chatBackgroundImageSelected/... → App → ChatWindow::setBackgroundImage/...
SettingsWindow::bubbleBgColorChanged/...        → App → PetWindow::setSpeechBubbleBg/Border/TextColor + QSettings

TrayManager::openChatRequested                 → App → ChatWindow::showAndFocus  (仅托盘图标双击)
TrayManager::screenshotRequested               → App → App::takeScreenshot
TrayManager::switchCharacterRequested           → App → App::switchCharacter
TrayManager::refreshCharactersRequested         → App → App::doAutoScanCharacters
TrayManager::parameterWindowRequested           → App → ParameterWindow::showAndRaise

GlobalHotkeyManager::hotkeyPressed             → App → hide/show / chat / cycle / screenshot

QFileSystemWatcher::directoryChanged           → App → 500ms 防抖 → doAutoScanCharacters()
```

---

## 托盘菜单结构

```
切换角色 → default / haru / Hiyori / xiaoyue / ...
刷新角色
截图
模型参数
──────
设置
开机自启
──────
退出
```

> 已移除"打开聊天"菜单项（2026-07-02），改为双击模型区域打开。托盘图标双击仍可打开聊天。

---

## 设置窗口结构（当前）

```
DeepSeek API Key
  状态标签 + 密码输入框 + Key 保存
模型外观
  尺寸滑块 30%~1000%
聊天背景
  路径标签（一行截断 + tooltip）+ 选择图片 / 裁剪 / 清除
  透明度滑块
说话气泡颜色
  [■背景色] [■边框色] [■文字色]  并排
交互设置
  [开启点击穿透] [头部跟随鼠标] [开启语音朗读] [游戏全屏始终显示]
  语音后端: SAPI / VoiceVox
  语音选择: 下拉
  VoiceVox 地址:
  贴边吸附距离: N px
  挂机超时: N 分钟
  截图保存路径: [浏览...]
[保存]  （固定底部，不随滚动移动）
```

> 模型参数面板已从设置窗口移除，独立为 ParameterWindow。
> 设置窗口可自由缩放，内容跟随滚动

---

## ModelParamUpdate 执行顺序（关键）

```
_userTimeSeconds += deltaTimeSeconds
  ↓
LoadParameters                    // 恢复上一帧保存的状态
  ↓
[IsFinished?]                     // 动作队列是否全空
  ├─ true + GetCurrentPriority==0 → StartMotion(Idle, 0, PriorityIdle)
  │                                 m_inProactiveIdleLoop = true
  │                                 m_lastIdleRestartTime = _userTimeSeconds
  └─ false → UpdateMotion(model, dt)
              ↓
             [m_inProactiveIdleLoop?]
               └─ true + elapsed >= m_idleDuration - 0.5s
                  → StartMotion(Idle, 0, PriorityForce)  // 提前交叉淡入
                  → m_lastIdleRestartTime = _userTimeSeconds
  ↓
applyOverrideParameters()         // ★ 第1遍：头部追踪 + 情绪 + 用户覆写
  ↓
SaveParameters                    // ★ 保存（含头部角度，供物理/Pose 读取）
  ↓
OnLateUpdate()                    // Physics + Pose + Look 在此计算
  ↓                                  （物理能看到转头角度 → 头发衣服跟着摆）
第2遍恢复:
  - 眼睛回中/眼动追踪            // 防止 CubismLook 覆盖
  - 头部角度 X/Y/Z/BodyX         // 防止 Physics/Pose 覆盖
  - 情绪表情                     // 安全兜底
  - 用户手动覆写                 // 最高优先级，始终最后
  ↓
_model->Update()                  // 应用蒙皮变形
```

---

## 自动注册点击动作机制

`ModelController::loadAutoTapMotions()` 在 `SetupModel()` 末尾自动执行：

```
SetupModel()
  ├─ PreloadMotionGroup("Idle")      ← model3.json 声明
  ├─ PreloadMotionGroup("TapBody")   ← model3.json 声明
  └─ loadAutoTapMotions()            ← ★ 自动注册
       ├─ 收集 model3.json 已引用的所有 .motion3.json
       ├─ 遍历目录下 *.motion3.json
       ├─ 跳过已引用的
       └─ 未引用的 → 加载为 "TapBody_auto_N"（SetLoop=false）
```

`startRandomTapMotion()` 从以下所有动作中等概率随机选：
- model3.json 中的非 Idle 组（TapBody 等）
- 自动注册的 `TapBody_auto_N`

如果完全无可用动作 → 兜底重播 Idle 动画。

---

## 路径解析策略

所有运行时资源路径从 exe 位置推算，不依赖工作目录：

```
exe:        build/Release/DesktopPet.exe
cd ../.. → 项目根目录
             ├── characters/       ← 模型目录（m_charactersPath）
             └── database/         ← SQLite 数据库
```

**回退路径**：如果项目根找不到 `characters/`，尝试 exe 同目录下的 `characters/`。

---

## 已知踩坑记录

1. **模型画布是抽象单位**：Hiyori 的 `GetCanvasWidth/Height()` 返回 1×1.403（非像素）。小越 canvas 1×2.25。
2. **ChatWindow 初始化顺序**：`setCharacterName()` 必须在构造之后调用。
3. **★ TTS 编码**：PowerShell 进程的 stdin 必须设置 `InputEncoding = UTF8`，否则中文朗读乱码。
4. **★ 浮动文字独立窗口**：QLabel 作为子控件会被父窗口裁剪，必须创建独立顶层弹出窗口。
5. **★ VoiceVox 音频播放**：`Media.SoundPlayer.PlaySync()` 播临时 WAV，播完自动删。
6. **★ ChatWindow DB key 不匹配**：`m_characterName` 是显示名，`m_characterKey` 才是文件夹名，DB 查询必须用后者。
7. **★ 游戏全屏置顶用 Win32 API**：`SetWindowPos` + `HWND_TOPMOST`，不能用 `setWindowFlags()` 否则 OpenGL 上下文丢失。
8. **★ 裁剪持久化到 AppData**：裁剪结果存 `%APPDATA%/DesktopPetProject/DesktopPet/chat_bg_cropped.png`。
9. **★ CropDialog 坐标系统**：`m_cropRect` 是显示图坐标，paintEvent/hitTest/cropRectOnSource 各自偏移转换。
10. **★ kPaddingRatio 不再是死代码**：已改为 `animMarginRatio = 0.55` 用窗口放大+投影补偿替代。
11. **★ ★ 路径末尾必须有 `/`**：Cubism SDK 的 `MakeAssetPath()` 直接拼接，`applyCharacter()` 中必须用 `modelAbsPath += "/"` 补回。
12. **★ 截图用 repaint 而非 update**：`QOpenGLWidget` 的 `repaint()` 同步调用 `paintGL()`。
13. **★ ★ 超级采样截图通过 resize widget 实现**：临时 resize 到 2x → render → grab → resize 回原尺寸。
14. **★ glReadPixels 字节序**：手动 glReadPixels 时需用 `GL_BGRA` 匹配小端序。
15. **★ 剪贴板 QMimeData**：`setImageData` 提供 CF_DIB，`setUrls` 提供 CF_HDROP，两者互不冲突。
16. **★ ★ QFileSystemWatcher 漏事件**：Windows 上可能漏，2 秒轮询做兜底。
17. **★ 自动检测角色缺少 personality**：autoDetectModel 返回的 Config 没有 personality，需要手动配置。
18. **★ Haru 模型切换闪退（未修复）**：SDK 官方 Haru 模型（Cubism 3.x），auto-detect 后切换闪退。
19. **★ ★ 切换模型缺 makeCurrent 导致白屏**：`loadPendingModelIfNeeded()` 需要 GL 上下文执行 SetupModel。
20. **★ 切换角色保留当前缩放**：`applyCharacter()` 初次用 config 的 modelScale，后续保持当前值不变。
21. **★ 路径用 exe 目录推算**：`characters/` 和 `database/` 路径基于 `applicationDirPath()` 向上两级。
22. **★ ★ 用户参数覆写在情绪/眼动之后**：`applyOverrideParameters()` 第4步施加用户手动参数，优先级最高。
23. **★ ★ 模型参数面板已独立**：不再嵌入 SettingsWindow，改为 ParameterWindow 独立窗口。
24. **★ 参数合并滑块**：合并组用 `QVector<int> paramIndices` 支持一个滑块控制多个 Live2D 参数。
25. **★ showAndRaise Windows 焦点**：`QTimer::singleShot(0, ...)` 延迟激活绕过系统焦点防窃取。
26. **★ ★ 模型缩放上限在两处限制**：`SettingsWindow` 滑块(30→1000) + `PetWindow::setModelScale` qBound(0.3→10.0)，两处都要同步改。
27. **★ ★ QOpenGLWidget repaint 不是真正同步**：清覆盖后不能靠 `repaint()` 刷新模型参数，必须直接在 GL 上下调用 `ModelParamUpdate()`。
28. **★ ★ 聊天窗口全屏后无法缩小**：Qt on Windows 的 `setWindowFlags()` 导致原生窗口丢失正常几何。修复：`changeEvent()` 手动保存/恢复 `m_normalGeometry`。
29. **★ ★ Physics/Pose 必须在头部覆写之后执行**：`ModelParamUpdate()` 中先 `applyOverrideParameters()` 再 `SaveParameters()` 再 `OnLateUpdate()`，这样物理/Pose 能感知转头角度。第二遍补眼睛/情绪/用户覆写防止被覆盖。
30. **★ ★ Live2DView 需持久化 headTrackingEnabled**：`switchCharacter()` 新建 ModelController 会重置状态，Live2DView 必须存一份副本，`loadPendingModelIfNeeded()` 后自动恢复。
31. **★ ★ 动画播放时机**：鼠标事件中调用 `startMotionByGroup()` 添加到动画队列，下一帧 `ModelParamUpdate()` 的 `UpdateMotion` 才实际播放。`PriorityForce` 强制立即切换。
32. **★ ★ 点击身体 vs 头部**：身体点击检测按 Y 坐标 30% 分界线（上=头部，下=身体），**但只限模型区域内**（动画安全边距内的点击无反应）。`bodyClicked` 和 `headClicked` 互斥。
33. **★ SpeechBubble 定位**：必须 `showText()` 后再调用 `move()`/`show()`，因为 `showText()` 会 `resize()`，顺序错了位置偏差。
34. **★ 说话气泡截取前 80 字**：`PetWindow::showSpeechBubble()` 中 `text.left(80)`，过长会显示不全。
35. **★ 设置窗口 setWindowFlags 必须用 `|`**：`setWindowFlags(Qt::WindowStaysOnTopHint)` 会覆盖所有默认标志导致无边框无法缩放。必须 `setWindowFlags(windowFlags() | ...)`。
36. **★ ★ Live2D SDK 忽略 Loop 标志**：`CubismMotion::Create()` 中 `ret->_loop = (ret->_motionData->Loop > 0)` 被注释掉了。必须手动 `SetLoop()`。
37. **★ ★ 5 R6 _isLoop 默认值改为 false**：`ACubismMotion` 构造函数默认从 `true` 改 `false`（`ACubismMotion.cpp:26`）。
38. **★ ★ _isLoop 影响 GetDuration()**：`_isLoop=false` 返回 `_loopDurationSeconds`（正常时长），`_isLoop=true` 返回 -1。
39. **★ ★ _motions key lookup 一致性**：用 `_motions[name]`（传 csmString）比 `_motions[name.GetRawString()]`（传 const char*）更可靠。
40. **★ ★ GetCurrentPriority 重启保护**：`GetCurrentPriority() == 0` 防止 `IsFinished()` 误触发 Idle 重启。
41. **★ 点击动作双触发**：`Live2DView` 内部和 `PetWindow` 都连接了 `bodyClicked`，已移除 `PetWindow` 的连接。
42. **★ SettingsWindow QScrollArea**：所有设置分组需放入 scrollArea，保存按钮固定底部。
43. **★ 路径标签颜色**：原用固定色在深色主题下不可读，改为无样式表跟随系统调色板。
44. **★ csmMap key 生命周期**：`operator[](_KeyT key)` 传值，每个环节都拷贝构造 csmString。
45. **★ ★ CubismMatrix44::Scale(x,y) 直接覆盖值**：不是矩阵乘法。累加缩放必须用 `ScaleRelative(x,y)`（做 `_tr = scaleMatrix * _tr`）。
46. **★ ★ Idle 循环必须用 m_inProactiveIdleLoop 标志**：主动重启用 PriorityForce，而 Tap 也用 PriorityForce，不能仅凭优先级区分。`startRandomTapMotion` 和 `startMotionByGroup`（非 Idle 组）必须清除该标志，`ModelParamUpdate` 中只在该标志为 true 时做主动重启。
47. **★ ★ computeModelArea Y/X 算法不同**：Y 方向固定 `height × 0.55/2.1`（匹配 `Scale(H/W, 1.0)` 分支），X 方向 `min(W,H)/2.1` 居中保守。两套算法不能互换，否则竖长模型上检测区域会下移。
48. **★ 浮动文字定位相对模型区域顶部**：`showFloatingText()` 中计算 `modelTopY = height × 0.55/2.1` 作为模型区域顶部，`y = modelTopY - label->height() + 30`（伸入模型 30px）。
49. **★ 双击开聊天过命中检测**：`InteractionManager::doubleClicked` 带位置，`App` 中 lambda 先调 `Live2DView::isPointInModelArea` 通过后才打开 `ChatWindow`。
50. **★ 托盘菜单无"打开聊天"**：已移除，改为双击模型区域打开。托盘图标双击（`QSystemTrayIcon::DoubleClick`）仍保持打开聊天功能。
51. **★ default/config.json modelScale=2.0**：默认角色 modelScale 为 2.0，模型 2 倍大小。如需调小可改 config 或设置中拖动尺寸滑块。
52. **★ ★ modelScale=2.0 时模型纵向拉伸（未解决）**：default 角色 modelScale=2.0 时，模型在纵轴方向看起来被拉伸（变长），横轴无变化。详见下方【会话10 — modelScale 拉伸排查】。

---

## 会话10 — modelScale 拉伸排查（2026-07-02）

### 已解决的问题

| # | 问题 | 修复 |
|---|------|------|
| 1 | `applyTightSize` 用 `static_cast<int>` 导致不同 modelScale 下宽高比漂移 ≤1% | 改为 `qRound()` |
| 2 | `Live2DView::m_modelScale` 死代码（只写不读），误导维护者以为缩放在此处理 | 移除成员变量，`setModelScale` 保留触发射频刷新 |
| 3 | Cubism 渲染器内部 FBO 锁定在 `SetupModel` 时的占位尺寸（如 400×400），之后窗口 resize 但 FBO 不变 → 渲染到小 FBO 再拉伸到窗口 | **已移除** — `IsBlendModeEnabled=0` 实锤后 `SetRenderTargetSize` 是空操作；`resizeGL` 和 `ModelOnUpdate` 中的调用已删除 |

## 会话12 — GLFW 隐藏窗口 WS_POPUP 修复 >300% 拉伸（2026-07-02 ✅）

### 问题

模型放大超过 300% 后，窗口高度继续增长但宽度不再增长 → 模型被纵向拉伸。

### 根因

`m_hiddenGlfwWindow`（GLFW 隐藏窗口）用于 `ModelOnUpdate` 中读取窗口尺寸计算投影矩阵。
`glfwSetWindowSize()` 创建的窗口默认带 `WS_OVERLAPPEDWINDOW` 装饰，OS 对其 `SetWindowPos` 有最大高度限制。
- 当 widget 大小为 535x1680 时，GLFW 窗口实际高度被 OS 限制在 1181（宽度 535 正确）
- 投影矩阵用 `Scale(1.0, 535/1181)` 计算宽高比修正
- 但 OpenGL 视口按 widget 实际尺寸 535x1680 渲染
- 模型在 Y 方向占比大于预期 → 纵向拉伸

### 修复

`Live2DView::initializeGL()` 中创建 GLFW 窗口前添加：
```cpp
glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);  // WS_POPUP，无窗口装饰
```

这样 `glfwSetWindowSize()` 不再受 OS 窗口尺寸约束，GLFW 窗口始终与 widget 尺寸一致。

### 验证

```
修复前: ⚠ GLFW=535x1181 widget=535x1680   ← 高度不匹配
修复后: GLFW=535x1680 widget=535x1680     ← 完美匹配 ✓

---

## 会话13 — 默认 600% 缩放 + 动画边距 1%（2026-07-02 ✅）

### 改动

| 文件 | 行 | 改前 | 改后 |
|------|----|------|------|
| `characters/default/config.json` | modelScale | 2.0 | **6.0** |
| `pet/PetWindow.cpp` | animMarginRatio | 0.55 | **0.01** |
| `pet/ModelController.cpp` | animMarginCompensation | `1/(1+0.55×2)` | `1/(1+0.01×2)` |

### 默认缩放 600%

初始模型大小改为 600%（`modelScale: 6.0`），窗口按公式计算：
```
basePx = 200 × 6.0 = 1200
modelH = 1200
modelW = 1200 × tightW/tightH ≈ 382   ← Hiyori
newW/H = modelW/H × (1 + 2×marginRatio)
```

### 动画安全边距从 55% 缩到 1%

**两处同步修改**（必须一致，否则模型变形）：

- `PetWindow::applyTightSize()` 中的 `animMarginRatio`
- `ModelController::ModelOnUpdate()` 中的 `animMarginCompensation = 1/(1 + ratio×2)`

**原理**：
```
窗口放大倍数 = 1 + 2×marginRatio
投影补偿 = 1 / 窗口放大倍数
两者抵消 → 模型像素尺寸 = modelW × modelH 不变
```

**窗口尺寸演变**（600% 时）：

| marginRatio | 窗口 | 较上一版 |
|-------------|------|---------|
| 0.55 (原) | 802×2520 | — |
| 0.10 | 458×1440 | -1080 |
| 0.03 | 404×1272 | -168 |
| **0.01** | **390×1224** | **-48** |

### 模型矩阵无平移修正

调试日志确认 `modelMat[12]=0, [13]=0`（CubismModelMatrix 构造函数只调 `SetHeight(2.0)`，不设平移）。
画布原点 (0,0) 映射到 clip (0,0) = 窗口中心，画布向右上延伸。左下半个窗口为空区域。
`computeModelArea` 命中检测仍使用居中假设，未修正——保留原样不动。
```

---

## 会话11 — 实锤 IsBlendModeEnabled + 矩阵分支标准化 + 清理已完成（2026-07-02 ✅）

### 已确认：`IsBlendModeEnabled=0` → `SetRenderTargetSize` 是空操作 → 已移除

**代码链完整追溯：**

1. **默认值 `false`** — `CubismModel.cpp:31` `_isBlendModeEnabled(false)`
2. **何时变为 `true`** — `InitializeBlendMode()` 两个条件任一：
   - `csmGetOffscreenCount(model) > 0`（有离屏 Drawable）— Hiyori **没有**
   - 任一 Drawable 的混合模式不是 NormalOver/AddCompatible/MultiplyCompatible — Hiyori **全用 NormalOver**
   → **`_isBlendModeEnabled` 保持 `false`**

3. **后果链：**
   - `CubismRenderer_OpenGLES2::Initialize()` 中 `if (IsBlendModeEnabled()) { ... }` **跳过创建 `_modelRenderTargets`**
   - `SetRenderTargetSize(w, h)` 只写 `_modelRenderTargetWidth/Height` 两个成员变量
   - `BeforeDrawModelRenderTarget()` 中 `if (_modelRenderTargets.GetSize() == 0) return;` **直接返回**
   - 模型直接渲染到 Qt 当前绑定的 FBO，Cubism 内部 FBO 尺寸从未使用

4. **运行时日志 6 条均输出 `IsBlendModeEnabled=0`** ✅ 推测 A 完全证实

### 已排查但排除的因素

| 因素 | 验证方式 | 结论 |
|------|---------|------|
| DPI 缩放 | `devicePixelRatio()` 运行时日志 | `dpr=1` ✅ 无缩放 |
| Viewport vs widget 尺寸 | `glGetIntegerv(GL_VIEWPORT)` vs `width()/height()` 运行时对比 | 始终匹配 ✅ |
| GLFW 窗口 vs widget 尺寸 | `glfwGetWindowSize` vs `width()/height()` 日志 | 始终匹配 ✅ |
| 窗口 resize 等比例 | `[resize]` 日志记录每个 scale 的期望 vs 实际尺寸 | 严格等比增长 ✅ |
| 窗口宽度卡住 | 用户反馈"宽度停在300%" | 日志显示 300%=535, 400%=672, **宽度确实在涨**，需进一步确认 |

### 发现的问题与修复

#### 问题 A：ModelController 矩阵分支不一致

**症状：** `ModelOnUpdate` 中根据 `canvasWidth > 1.0f` 和窗口宽高比走两个不同分支：

```cpp
// 旧代码
if (canvasWidth > 1.0f && width < height) {
    SetWidth(2.0f);                     // 统一缩放 2.0/canvasW
    projection.Scale(1.0f, w/h);
} else {
    // Hiyori 走这 (canvas=1x1.40289)
    // 不调用 SetWidth，保持构造函数 SetHeight(2.0) → 缩放 2.0/canvasH
    projection.Scale(h/w, 1.0f);
}
```

**问题：** `else` 分支中 model matrix 的 `_tr[12]=0, _tr[13]=0`（由 `CubismUserModel::LoadModel()` line 99 重建 `CubismModelMatrix(canvasW, canvasH)` 后 `SetHeight(2.0f)` 只设 Scaling 不做居中平移）。画布原点 (0,0) 映射到视口中心 (clip 0,0)，模型视觉偏移偏右上。

**修复：** 改为始终调用 `SetWidth(2.0f)` + `Scale(1.0f, width/height)`，与标准 Cubism 示例一致。

```cpp
// 新代码（ModelController.cpp:495-497）
GetModelMatrix()->SetWidth(2.0f);           // 统一缩放 2.0/canvasW (X/Y等比例)
projection.Scale(1.0f, width / height);     // 宽高比修正 (仅在 Y 方向压缩)
```

**验证：** 修复后模型矩阵 `_tr[0]=2.0/canvasW`，`_tr[5]=2.0/canvasW`，`_tr[12/13]` 由 `SetWidth` 内部调用 `Scale` 后保持为 0（仍无显式居中，但所有模型行为一致）。

#### 问题 B：用户反馈"宽度停在300%宽度"

**结论：已由矩阵分支标准化修复。**
- resize 日志明确显示宽度随 scale 线性增长（300%=535, 400%=672），窗口尺寸不存在卡住
- 旧 `else` 分支未调用 `SetWidth(2.0)`，仅做 `Scale(h/w, 1.0)`，导致模型视觉上偏离中心且比例异常
- 无论窗口实际放大到多少，用户在视觉上感觉"模型没变大"——因为模型矩阵计算不完整
- 标准化为 `SetWidth(2.0) + Scale(1.0, width/height)` 后，模型始终居中且比例一致，此问题的根本原因已消除

### 运行时诊断日志位置

| 日志 | 位置 | 触发条件 | 内容 |
|------|------|---------|------|
| `[诊断]` | `ModelController::ModelOnUpdate` | 每120帧 | canvas尺寸+GLFW窗口+modelMat矩阵+tight bounds |
| `[resize]` | `PetWindow::applyTightSize` | 每次resize | 期望vs实际窗口尺寸+scale+aspect |
| `[每60帧]` | `Live2DView::paintGL` | 每60帧 | viewport+GLFW+widget+FBO+视口不匹配警告 |
| `resizeGL` | `Live2DView::resizeGL` | 每次GL resize | 逻辑像素+物理像素+dpr |
| `IsBlendModeEnabled` | `Live2DView::loadModel` | 模型加载时 | BlendMode 状态

---

## 会话14 — animMarginRatio 共享常量 + tight bounds 命中检测 + 模型大小归一化（2026-07-02）

### 1. animMarginRatio 统一为共享常量 ✅

| 文件 | 改动 |
|------|------|
| `PetWindow.h` | 在 `public:` 区添加 `static constexpr double kAnimMarginRatio = 0.01` |
| `PetWindow.cpp` | 用 `kAnimMarginRatio` 代替硬编码 `0.01` |
| `ModelController.cpp` | `#include "PetWindow.h"`，`animMarginCompensation` 从 `PetWindow::kAnimMarginRatio` 计算 |
| `Live2DView.cpp` | `#include "PetWindow.h"`，`computeModelArea` 用 `kAnimMarginRatio` 动态计算 |

### 2. getModelBounds 返回顶点包围盒位置 ✅

`ModelController::getModelBounds()` 新增 `outMinX, outMinY` 参数，返回顶点覆盖区域的左上角位置（画布坐标系，原点在中心）。所有调用点已更新。

### 3. 命中检测改为 tight bounds（顶点覆盖区域） ✅

之前：命中检测作用在**完整画布区域**（含透明空白），点击画布空白处也会触发摸摸头/随机动画。
之后：只响应**顶点紧贴包围盒（tight bounds）**。

**computeModelArea 改为 Live2DView 成员函数：**
```
1. 计算画布像素区域（窗口 × 投影补偿，marginRatio 从 PetWindow 共享常量）
2. 将 tight bounds 从画布坐标映射到像素坐标
   Cubism 画布坐标系：(0,0) 在中心，X→右，Y→上
   → 归一化 [0,1] + Y 翻转 → 像素坐标
```

**涉及文件：**
- `Live2DView.h` — 新增 `computeModelArea()` 声明 + 6 个缓存成员
- `Live2DView.cpp` — `computeModelArea` / `isPointInModelArea` / `mousePressEvent` 改用 tight bounds
- `ModelController.hpp` — `getModelBounds` 签名扩展

### 4. 模型大小：不跨模型归一化，各模型用自己的 idle 稳定包围盒

每个模型用自己 idle 稳定后的 tight bounds 算窗口。`modelH = basePx`，不跨模型缩放。

**采样时机：** 初始加载（T-pose）先算一次占位窗口，500 ms idle 稳定后 `stableBoundsReady` 信号用稳定 tight bounds 重算。

### 5. 调试日志（临时，可清理）

会14生成的文件（在 build/Release/ 目录）：
- `hit_debug.txt` — 每次 mouse press 打印 tight bounds 像素区 + 点击坐标 + 是否命中
- `hit_release.txt` — release 时打印 headClicked/bodyClicked 信号状态
- `pet_action.txt` — 信号槽执行日志
- `live2d_debug_log.txt` — 含 `[resize]` 行（窗口尺寸 + basePx + tight 包围盒）

---

## 会话15 — 最终总改：idle 稳定包围盒 + 模型大小归一化移除 + 命中检测修正 + 1000% 缩放（2026-07-02 ✅）

### 最终决策

| 决定 | 原因 |
|------|------|
| ❌ 不跨模型归一化（fillGM/kRefFillGM 全删除） | 用户反馈 Hiyori/Mao 缩小太明显 |
| ✅ idle 稳定包围盒（500ms 延迟读取） | T-pose 包围盒偏大，idle 稳定后更准 |
| ✅ 命中检测匹配实际投影矩阵 | 旧公式画布 Y 用 `height×comp`，实际应为 `width×comp×ch/cw` |
| ✅ 模型外观滑块上限提升到 1000% | 用户要求 |

### 改动清单

| 文件 | 改动 |
|------|------|
| `Live2DView.h:67` | 新增信号 `stableBoundsReady(double tightW, double tightH)` |
| `Live2DView.cpp:471-487` | `setupModel()` 末尾加 `QTimer::singleShot(500ms, ...)`，idle 稳定后重读 `getModelBounds()` 并发射信号 |
| `Live2DView.cpp:944-955` | **`computeModelArea()` 修复：** 画布像素范围改为匹配实际投影链 `SetWidth(2.0)→Scale(1.0,w/h)→ScaleRelative(comp,comp)` |
| `PetWindow.cpp:42-57` | 连接 `modelLoaded`（初始）+ `stableBoundsReady`（稳定后）两个信号 |
| `PetWindow.cpp:155-167` | `applyTightSize()` 删除 `fillGM/kRefFillGM` 归一化，`modelH = basePx` |
| `PetWindow.cpp:134` | `qBound(0.3, scale, 6.0)` → `qBound(0.3, scale, 10.0)`（上限 1000%） |
| `SettingsWindow.cpp:51` | `m_sizeSlider->setRange(30, 600)` → `setRange(30, 1000)` |
| `SettingsWindow.cpp:384` | `qBound(30, ..., 600)` → `qBound(30, ..., 1000)` |

### 命中检测修复详解

**投影链**（`ModelController::ModelOnUpdate`）：
```cpp
GetModelMatrix()->SetWidth(2.0f);           // 画布宽→2.0
projection.Scale(1.0f, width / height);      // 宽高比修正
projection.ScaleRelative(comp, comp);        // 动画边距补偿
```

**旧 `computeModelArea`（错误）：**
```cpp
// Y: height × comp（完全错了！实际是 width × comp × ch/cw）
const float canvasH = height * canvasFrac;
// X: min(w,h) × comp（w>h 时也错）
const float xSize = qMin(width, height) * canvasFrac;
```

**新 `computeModelArea`（正确）：**
```cpp
const float canvasW = width * comp;
const float canvasX0 = (width - canvasW) * 0.5f;
const float canvasH = canvasW * (canvasHeight / canvasWidth);  // = comp × width × ch/cw
const float canvasY0 = (height - canvasH) * 0.5f;               // 居中
```

**效果对比**（Hiyori, widget 57×204, canvas 1×1.40289）：

| | 旧（错） | 新（对） |
|--|---------|---------|
| 画布像素 Y 范围 | [2, 202]（200px） | [62.8, 141.2]（78px） |
| tightBounds 像素 Y | [16.6, 197.6]（181px） | [68.5, 139.4]（71px） |
| 误判区域 | 点击画布空白也响应 | 只响应有顶点的区域 |

### 完整流程

```
applyCharacter() → loadModel() → setupModel()
  ├─ getModelBounds() ← T-pose 包围盒
  ├─ emit modelLoaded(cw,ch,tightW,tightH)  ← PetWindow 算初始窗口
  └─ QTimer::singleShot(500ms)
       └─ getModelBounds() ← idle 稳定后的包围盒
       └─ emit stableBoundsReady(tightW,tightH)  ← PetWindow 重算窗口
```
| `Live2DView.cpp` | `setupModel()` 末尾加 `QTimer::singleShot(3000, ...)`，idle 稳定后重新读包围盒并发射信号 |
| `PetWindow.cpp` | 连接 `stableBoundsReady` → 用稳定值重算窗口；移除 `fillGM`/`kRefFillGM` 归一化，`modelH = basePx` |
| `PROJECT_CONTEXT.md` | 更新描述 |

### 最终方案

因用户反馈归一化后 Hiyori/Mao 明显变小，最终**取消归一化**。只保留 idle 稳定包围盒的改进：
- `Live2DView::setupModel()` 发射 `modelLoaded` 后启动 500 ms `QTimer::singleShot`
- idle 稳定后重新读 `getModelBounds()`，发射 `stableBoundsReady`
- `PetWindow` 用稳定 tight bounds 重算窗口（`modelH = basePx`，无归一化）
- 各模型独立视觉大小，用户通过尺寸滑块调节

---

## 会话16 — 命中检测重写：三级优先级（2026-07-02 ✅）

### 背景

旧命中检测将模型整体 tight bounds 按高度 30%/70% 硬分割为头部/身体区域，完全不考虑模型实际结构。对于不同体型的模型（如头大身小、有长发等）误判率高。

### 新方案：三级优先级

**第一优先 — model3.json HitAreas**
- 读取 `model3.json` 的 `HitAreas` 数组
- 按名称匹配 `"Head"` → 头部区域，`"Body"` → 身体区域
- 通过对应 drawable 的顶点位置（`GetDrawableVertexPositions`）计算动态 AABB
- 跟随动画变形实时变化（AABB 在每次鼠标事件时重新计算）

**第二优先 — Drawable ID 名称匹配（fallback）**
- 无 HitAreas 时遍历所有 drawable，按 ID 名称匹配常见命名规则
- Head 模式：`head` / `face` / `hair_front*` / `hair_back*` / `hairfront` / `hairback` / `hair`
- Body 模式：`body` / `bust` / `torso` / `arm*`
- 匹配到的 drawable 计算 AABB 作为命中区域

**第三优先 — 高度比例（终极 fallback）**
- 无任何匹配时沿用 tight bounds，前 **35%** = 头部，后 **65%** = 身体

### Alpha 检测

命中区域确定后，调用 `checkPixelAlpha()` 用 `glReadPixels` 读取点击位置的像素 alpha：
- alpha ≥ 30 → 视为有效点击（有模型内容）
- alpha < 30 → 过滤（透明间隙/空白区域）

### 新增/修改的代码

| 文件 | 改动 |
|------|------|
| `ModelController.hpp` | 新增 `HitAreaAABB` 结构体 + `getInteractionAreas()` 声明 |
| `ModelController.cpp` | 新增 `getInteractionAreas()` 实现（HitAreas + drawable 匹配） |
| `Live2DView.h` | 新增 `pixelToCanvas()` / `performHitTest()` / `checkPixelAlpha()` 声明 |
| `Live2DView.cpp` | 新增上述 3 方法 + 重写 `mousePressEvent` 使用新系统 |

### 核心流程

```
mousePressEvent(leftButton, m_model)
  → performHitTest(px, py)
      ├─ m_model->getInteractionAreas(head, body)     ← 读取顶点数据
      │   ├─ model3.json HitAreas → AABB              ← Priority 1
      │   └─ drawable ID 名称匹配 → AABB              ← Priority 2
      ├─ pixelToCanvas(px, py) → canvas coords        ← 转换到画布坐标
      ├─ AABB 碰撞检测（Head → Body）
      └─ Fallback: tight bounds 35%/65%               ← Priority 3
  → checkPixelAlpha(px, py)                           ← 过滤透明区域
  → 设置 m_pressInHeadArea / m_pressInBodyArea
```

---

## 会话17 — 命中检测三层漏斗修复：Body-only 模型头部失效（2026-07-02 ✅）

### 问题

部分模型（Hiyori/Rice/Wanko）点击头部无法触发摸头（`headClicked` → `triggerPet`），
点击头部位置时误触发身体动作（`bodyClicked` → `playTapMotion`）。

### 根因

`model3.json` 的 `HitAreas` 配置差异：

| 模型 | Head HitArea | Body HitArea |
|------|:-----------:|:-----------:|
| Haru | ✅ 有 | ✅ 有 |
| Mao | ✅ 有 | ✅ 有 |
| Ren | ✅ 有 | ✅ 有 |
| **Hiyori** | ❌ **无** | ✅ `"Body"` |
| **Rice** | ❌ **无** | ✅ `"Body"` |
| **Wanko** | ❌ **无** | ✅ `"Body"` |
| xiaoyue | ❌ 完全无 HitAreas 字段 | ❌ 同上 |

旧 `performHitTest` 逻辑：

```
if (hasHead || hasBody) {
    headAreas 检测 → 空跳过
    bodyAreas 检测 → 命中 → 返回 BODY  ← ⚠ 覆盖了头部点击
    return false  ← 提前退出，不走 fallback
}
// ← tight bounds fallback 被屏蔽，不可达
```

对 Hiyori 等只有 Body HitArea 的模型：
1. `hasHead=false, hasBody=true` → 进入 Phase 1
2. Body HitArea 的 AABB 覆盖全身（包含头部区域）
3. 头部点击位置命中 Body AABB → 返回 `isHead=false`
4. 松手发 `bodyClicked` 信号 → 播随机动画而非摸头

### 修复：三管齐下

#### 1. HitArea 名称别名扩展（`ModelController::getInteractionAreas`）

旧：仅匹配 `"head"` / `"body"` 严格字符串
新：支持常见别名：

```
头部: head, face, hair, atama, 头部, 脸部, 头发
身体: body, torso, bust, karada, 身体, 胴体
```

同时补充 drawable ID 名称匹配的额外模式：`neck`/`face*`/`leg`/`foot`/`skirt`/`hand` 等。

#### 2. `performHitTest` 三层漏斗结构（`Live2DView::performHitTest`）

移除了 Phase 1 的提前 `return false`，改为三层始终可达的漏斗：

```
鼠标点击
  ↓
Layer 1: HitArea AABB 检测
  ├─ headAreas 检测（最高优先级）
  │   └─ 命中 → HEAD ✅（Haru/Mao/Ren）
  ├─ bodyAreas 检测（仅当 headAreas 也存在时）
  │   └─ 命中 → BODY ✅
  ↓（未命中或 canvasOk=false）
Layer 2: Body-only 模型 → Body AABB 内 35%/65% 高度分割
  ├─ (cy > area.maxY - bodyHeight × 0.35f) → HEAD ✅
  └─ 否则 → BODY ✅
  ↓（未命中或 hasHead=false && hasBody=false）
Layer 3: tight bounds 像素 35%/65% 高度分割（始终可达）
  ├─ 前 35% → HEAD ✅
  └─ 后 65% → BODY ✅
```

#### 3. 各模型效果

| 模型 | 改前点头部 | 改后点头部 | 改后点身体 |
|------|----------|----------|----------|
| **Haru** | HEAD ✅ | 不变 ✅ | ✅ |
| **Mao** | HEAD ✅ | 不变 ✅ | ✅ |
| **Ren** | HEAD ✅ | 不变 ✅ | ✅ |
| **Hiyori** | **BODY ❌** | **HEAD ✅** | ✅ |
| **Rice** | **BODY ❌** | **HEAD ✅** | ✅ |
| **Wanko** | **BODY ❌** | **HEAD ✅** | ✅ |
| **xiaoyue** | — | Layer 3 → HEAD ✅ | ✅ |

### 改动的文件

| 文件 | 改动 |
|------|------|
| `ModelController.cpp` | `getInteractionAreas()` — HitArea 名称别名扩展 + drawable ID 匹配扩展 |
| `Live2DView.cpp` | `performHitTest()` — 三层漏斗结构，Layer 2 (Body-only 35%) + Layer 3 始终可达 |
```

---

## 会话18 — 浮动文字绑定模型头部 + 性能优化 + deltaTime 钳制（2026-07-02）

### 1. 浮动文字定位重写

**问题**：旧 `showFloatingText()` 使用硬编码 `0.55/2.1` 公式（已废弃的旧边距），文字位置与模型实际头部位置脱节。

**方案**：新增 `Live2DView::getHeadAnchorPoint()` 计算头部锚点（窗口局部坐标），三级优先级：

| 优先级 | 条件 | 锚点 |
|--------|------|------|
| Layer 1 | 存在 Head HitArea | 所有 Head HitArea AABB 聚合后顶部中心 |
| Layer 2 | 只有 Body HitArea | Body AABB 顶部（头部区域） |
| Layer 3 | 无 HitArea | tight bounds 顶部中心 |
| 兜底 | 模型未加载 | 窗口顶部中心 |

**位置公式**：
```
textX = headCenterX（水平居中）
textY = headTopY - offset - labelHeight
offset = modelPixelHeight × 0.065（动态，范围 5%~8% 取中值）
```

**同步跟踪**：
- `PetWindow::m_floatingTextTimer`（100ms）驱动 `updateFloatingTextPosition()`
- 每次 tick 调用 `Live2DView::getHeadAnchorPoint()` 读缓存
- 窗口拖拽/模型缩放/头部动画 → 位置实时更新
- 移除旧的 `QPropertyAnimation` 位移动画，替换为 timer 驱动的向上浮动（0→40px, 2s）

### 2. 头部锚点缓存化（性能关键）

**问题**：`getHeadAnchorPoint()` 每 100ms 调 `getInteractionAreas()` 遍历所有 Drawable，在事件循环中额外占用 CPU。

**方案**：新增 `Live2DView::computeAndCacheHeadAnchor()`，在 `paintGL()` 的 `ModelOnUpdate()` 之后调用：

```
paintGL → ModelOnUpdate → computeAndCacheHeadAnchor → 缓存值
                                                           ↓
getHeadAnchorPoint()  ← 浮动文字定时器(100ms)  ← 只读缓存，零遍历开销
```

- Drawable 遍历成本融入渲染帧，不额外占用事件循环
- 缓存成员：`m_cachedHeadCenterX/Y`, `m_cachedModelPixelHeight`

### 3. 移除渲染路径文件 I/O（防掉帧）

| 位置 | 移除内容 | 原因 |
|------|---------|------|
| `ModelController::ModelOnUpdate` | `[诊断]` 块（每120帧 `getModelBounds` + `live2d_debug_log.txt` 写入） | 顶点遍历 + 文件 I/O 在渲染路径中间 |
| `Live2DView::paintGL` | `[每60帧]` 块（3次 `glGetIntegerv` GL同步 + LOG_DEBUG 文件写入） | GL pipeline stall + 文件 I/O |
| `mousePressEvent` | `hit_debug.txt` 写入 | 文件 I/O 在主线程鼠标事件中 |
| `mouseReleaseEvent` | `hit_release.txt` 写入（3处） | 同上 |
| `bodyClicked` 信号 | `pet_action.txt` 写入 | 文件 I/O 在信号槽中 |
| `headClicked` 信号 | `pet_action.txt` 写入 | 同上 |

### 4. deltaTime 钳制（防物理抖动）

```cpp
// ModelParamUpdate 中传入 Physics/Pose/Look 前钳制 deltaTime
const float clampedDt = (dt < 0.008f) ? 0.008f    // 最低 8ms (~120fps 上限)
                      : (dt > 0.033f) ? 0.033f    // 最高 33ms (~30fps 下限)
                      : dt;
_updateScheduler.OnLateUpdate(_model, clampedDt);
```

- 防止帧率波动导致物理/姿态突然跳变
- `_userTimeSeconds` 和 `UpdateMotion` 仍用原始 deltaTime

### 5. 帧定时器精度

```cpp
m_frameTimer->setTimerType(Qt::PreciseTimer);
```

- 避免 Windows 上 Qt 默认的 CoarseTimer 合并导致 deltaTime 不规则波动

### 6. 帧耗时诊断（临时，可清理）

在 `ModelController::ModelOnUpdate` 中添加 `QElapsedTimer` 耗时统计，每 60 帧输出到 `perf_log.txt`：

```
实测数据（默认 Hiyori 模型 idle 状态）：
  setup=0.00ms  param=0.07ms  draw=0.10ms  cleanup=0.00ms  total=0.17ms
  → 渲染管线仅占 ~1% CPU，不在渲染侧。
```

**结论：** 瓶颈不在 CPU 渲染管线（0.17~0.75ms/帧，远低于 16.67ms 预算）。
残留卡顿可能来自 vsync 与 QTimer(16ms) 不同步导致的帧间隔波动。
下一个接手者可尝试：
- 将帧定时器从 16ms 改为 17ms（略低于 60fps 避免 vsync 竞争）
- 或禁用 vsync: `fmt.setSwapInterval(0)`

### 改动的文件

| 文件 | 改动 |
|------|------|
| `Live2DView.h` | 新增 `getHeadAnchorPoint()` + `computeAndCacheHeadAnchor()` 声明 + 缓存成员 |
| `Live2DView.cpp` | 实现锚点方法；`paintGL` 调用 `computeAndCacheHeadAnchor`；移除诊断块和文件 I/O；`PreciseTimer` |
| `PetWindow.h` | 新增 `m_activeFloatingText`(QPointer)、`m_floatingTextStartTime`、`m_floatingTextTimer`、`updateFloatingTextPosition()` |
| `PetWindow.cpp` | 重写 `showFloatingText()` 使用头部锚点；实现 `updateFloatingTextPosition()`；移除文件 I/O |
| `ModelController.cpp` | deltaTime 钳制(8~33ms)；移除 `[诊断]` 块；帧耗时统计(QElapsedTimer，每60帧 → perf_log.txt) |

---

## 会话19 — 残留卡顿修复：17ms 帧定时器 + 禁用 vsync（2026-07-02 ✅）

### 问题

会话18 诊断确认 CPU 渲染管线仅占 ~1%（0.17ms/帧），瓶颈不在渲染性能。
残留卡顿来自 **vsync 与 QTimer(16ms) 不同步**：

- QTimer 每 16ms 触发（62.5Hz）
- 显示器 vsync 每 ~16.67ms 刷新（60Hz）
- 两者周期相差 0.67ms，约每 1.5 秒累积出一个额外 tick
- 导致 `update()` 在两次 vsync 间被调用两次 → Qt 合并为一次 paint → 帧时间间隔从 16.67ms 跳到 33.33ms → 视觉卡顿

### 修复

```cpp
// Live2DView 构造函数中两处改动：

// 1. 帧定时器 16ms → 17ms（略低于 60fps，避免追赶上 vsync）
m_frameTimer->start(17); // ~59fps ✓

// 2. 禁用 vsync（QSurfaceFormat）
QSurfaceFormat fmt;
// ...其他设置...
fmt.setSwapInterval(0);  // 不等待 vsync，update() 后立即交换缓冲
setFormat(fmt);
```

### 原理

| 措施 | 效果 |
|------|------|
| 17ms 定时器 | 周期略长于 vsync（17ms > 16.67ms），不再追赶刷新信号，消除累积双触发 |
| `setSwapInterval(0)` | `update()` → `paintGL()` 后立即 swap，不被 vsync 阻塞；渲染耗时仅 0.17ms，无撕裂风险 |

### 改动的文件

| 文件 | 改动 |
|------|------|
| `Live2DView.cpp:70` | `fmt.setSwapInterval(0)` — 禁用 vsync |
| `Live2DView.cpp:85` | `m_frameTimer->start(16)` → `start(17)` |

---

## 会话20 — SpeechBubble 第二次不播放 + 浮动文字卡顿修复（2026-07-02 ✅）

### 问题

1. **SpeechBubble 第二次双击有概率完全不播放** — 动画结束后 `m_fadeAnim` 指针未清空，存在歧义竞争路径
2. **浮动文字动画卡顿** — `QGraphicsOpacityEffect` 每帧强制离屏渲染到临时 pixmap，额外合成开销
3. **TTS 阻塞主线程** — `waitForBytesWritten(1000)` 在 `replyReceived` lambda 中阻塞主事件循环 1 秒，窗口事件被延迟处理

### 触发链路

```
双击 → InteractionManager → emit doubleClicked
  → App: isPointInModelArea?
    → ChatWindow → 用户输入 → DeepSeek流式请求
      → replyReceived
        → MemoryDB.saveMessage      [<1ms]
        → EmotionAnalyzer.analyze   [<1ms]
        → TtsEngine::speak          [✗ 旧: waitForBytesWritten 阻塞1s]
        → PetWindow::showSpeechBubble
          → SpeechBubble::showText  [✗ 旧: m_fadeAnim 指针悬空]
```

### 改动详情

| 文件 | 改动 | 原因 |
|------|------|------|
| `SpeechBubble.cpp:55` | `showText` 开头加 `m_lifeTimer->stop()` | 防止过渡期间 timer 并发触发 `startFadeOut` |
| `SpeechBubble.cpp:114` | `finished` handler 中 `m_fadeAnim = nullptr` | 动画完成后清空指针，避免下次 `showText` 走入歧义路径 |
| `PetWindow.cpp:377-402` | `QGraphicsOpacityEffect` → `setWindowOpacity` | 离屏渲染 → DWM 硬件合成，消除额外渲染 Pass |
| `PetWindow.cpp:12` | 移除 `#include <QGraphicsOpacityEffect>` | 不再使用 |
| `TtsEngine.cpp:198` | `speak()` 移除 `waitForBytesWritten(1000)` | 管道写几乎瞬间完成，阻塞主线程 1 秒造成所有窗口事件延迟 |
| `TtsEngine.cpp:212` | `stop()` 移除 `waitForBytesWritten(300)` | 同上 |
| `TtsEngine.cpp:253` | `killProcess()` 移除 `waitForBytesWritten(500)` | 即将 kill 进程，不必要等待写入完成 |

---

## 会话21 — 浮动文字连续触发修复 + 触发链日志（2026-07-02 ✅）

### Bug：浮动文字 timer 被旧 label 延迟销毁误停

连续点击模型头部时，每次 `showFloatingText` 给新 label 添加 `connect(label, &QObject::destroyed, ...)` 连接。旧 label 的 `deleteLater()` 在事件循环中实际销毁时触发自身 `destroyed` 信号 → 无条件 `m_floatingTextTimer->stop()`，**停掉的是当前 label 的跟踪 timer**。

**时序：**
```
Click 1 → labelA 创建，timer 启动
Click 2 → labelA->deleteLater()，labelB 创建，timer 重启
  ...事件循环...
  labelA 被实际销毁 → destroyed → timer.stop() ← 误停 labelB 的 timer
Click 3 → timer 被重启，但 labelB 销毁又停 → 用户感知"后续点击不触发"
```

**修复：** `m_floatingTextGeneration` 递增 token，destroyed 回调只在自己 generation == 当前 generation 时才 stop timer。

### SpeechBubble showText 清理强化

`SpeechBubble::showText` 中 `m_fadeAnim->deleteLater()` → `delete m_fadeAnim`（立即删除），防止 finished 回调残留。

### 触发链日志

所有关键路径添加 `qInfo()`：

```
Live2DView::mouseReleaseEvent → [Live2DView] headClicked/bodyClicked
  → PetWindow::triggerPet → [PetWindow] triggerPet 触发
    → PetWindow::showFloatingText → [PetWindow] showFloatingText: "主人~~"
      → FloatingTextWidget::startAnimation

或 AI 回复时:
  DeepSeekClient::replyReceived → [App] replyReceived → showSpeechBubble
    → PetWindow::showSpeechBubble → [PetWindow] showSpeechBubble: "..."
      → SpeechBubble::showText → [SpeechBubble] showText: "..."
        → m_lifeTimer timeout → [SpeechBubble] startFadeOut
          → fade anim finished → [SpeechBubble] fadeOut finished → hide
```

### 其他改进

| 文件 | 改动 |
|------|------|
| `PetWindow.h` | 新增 `m_floatingTextGeneration` |
| `PetWindow.cpp` | `showFloatingText` — generation token；`triggerPet` — `qInfo`；`showSpeechBubble` — 移除冗余 `->show()` |
| `SpeechBubble.cpp` | `showText` — `delete` 代替 `deleteLater` + `move(0,0)`；`startFadeOut` — `qInfo` |
| `Live2DView.cpp` | headClicked/bodyClicked 加 `qInfo` |
| `App.cpp` | replyReceived→showSpeechBubble 加 `qInfo` |

---

## 会话22 — FloatingTextWidget 自绘制 + 卡顿/裁剪修复（2026-07-02 ✅）

### 问题

浮动文字旧实现（QLabel + setWindowOpacity + 100ms timer move）导致两个问题：

| 问题 | 根因 |
|------|------|
| **卡顿** | 每 100ms `move()` 触发 DWM 重排 + `setWindowOpacity()` 触发 DWM 合成 |
| **裁剪** | 窗口高度仅按文本高度计算，`translate(0, -offset)` 将文字移出窗口范围被 Qt 裁剪 |

### 方案：FloatingTextWidget 自绘制

**新建文件：** `pet/FloatingTextWidget.h` / `.cpp`

核心设计：

```
FloatingTextWidget (QWidget, 固定顶层窗口)
  ├─ Q_PROPERTY(qreal floatOffset)  ← QPropertyAnimation(0→40, 2s)
  ├─ Q_PROPERTY(qreal opacity)      ← QSequentialAnimationGroup(淡入→停留→淡出)
  └─ paintEvent:
       ├─ p.setOpacity(m_opacity)         ← 内部 opacity，无 DWM 合成
       ├─ p.translate(0, -m_floatOffset)  ← 内部偏移，无窗口移动
       └─ p.drawText(m_textRect, ...)     ← 纯色文本，无描边/阴影
```

### 与旧实现对比

| 维度 | 旧 QLabel 方案 | 新 FloatingTextWidget 方案 |
|------|---------------|---------------------------|
| 窗口移动 | `move()` 每 100ms | **固定不动** |
| 透明度 | `setWindowOpacity()` DWM 合成 | `QPainter::setOpacity()` 内部绘制 |
| 布局计算 | 无（QLabel 内建） | **构造时一次性** QFontMetrics |
| 头部跟踪 | 100ms timer 重定位 | **不跟踪**，仅定位一次 |
| 动画驱动 | QSequentialAnimationGroup(wind owOpacity) | QParallelAnimationGroup(floatOffset + opacity) |
| 生命周期 | deleteLater on finished | **自包含**，动画结束自动 deleteLater |
| 文字阴影 | 无 | 无（与原始一致） |

### 视觉效果还原

| 属性 | 原 QLabel | 确认 |
|------|-----------|------|
| 字号 | `font-size: 14px` | `setPixelSize(14)` ✓ |
| 字重 | `font-weight: bold` | `setBold(true)` ✓ |
| 颜色 | CSS `color: %1` | `QPainter::setPen(m_color)` ✓ |
| 字体族 | 系统默认 | `QFont()` 默认 ✓ |
| 描边/阴影 | 无 | 无 ✓ |
| 边距 | QLabel 默认紧贴文本 | `setFixedSize(m_textRect.size())` ✓ |

### 裁剪修复（关键）

文字上浮到 40px 时被 Qt 窗口边界裁剪。修复：预留 40px 底部空间。

```
窗口高度 = 文本高度 + kFloatDistance(40)
m_textRect.y = kFloatDistance (baseY = 40)

offset=0  → 文字画在 Y=40   (窗口底部，视觉起始位置)
offset=40 → 文字画在 Y=0    (窗口顶部，上浮终点)

位置推演：
  widgetPos Y = headTopY - offset - widgetHeight
              = headTopY - offset - (textHeight + 40)
  textRect.y  = 40
  文字视觉Y (offset=0):  widgetPos Y + 40 - 0  = 原位置 ✓
  文字视觉Y (offset=40): widgetPos Y + 40 - 40 = 原位置 - 40 ✓
```

### Z-order 保护

`show()` → `raise()` 确保不被 PetWindow 层级覆盖。

### 改动的文件

| 文件 | 变动 |
|------|------|
| `pet/FloatingTextWidget.h` | **新增** |
| `pet/FloatingTextWidget.cpp` | **新增** |
| `CMakeLists.txt` | 添加 `pet/FloatingTextWidget.cpp/.h` |
| `PetWindow.h` | 移除 `m_floatingTextTimer`/`m_floatingTextStartTime`/`m_floatingTextGeneration`/`updateFloatingTextPosition()` |
| `PetWindow.cpp` | `showFloatingText` 改用 FloatingTextWidget；移除 timer 逻辑；添加 `raise()` |

---

## 会话23 — 智能穿透：WA_TransparentForMouseEvents → WM_NCHITTEST (2026-07-03 ✅)

### 问题

旧穿透模式：设置中勾选"开启点击穿透" → `WA_TransparentForMouseEvents` → 整个窗口统一对鼠标透明。

新需求：**智能穿透**——模型区域可交互，透明间隙/空白边缘自动穿透到桌面。

### 根因排查

| 检查项 | 结果 |
|--------|------|
| `Qt::WindowTransparentForInput` | 全项目未使用 |
| `WA_TransparentForMouseEvents` | 仅 SpeechBubble/FloatingTextWidget 自包含浮动窗口使用（不应修改） |
| PetWindow `nativeEvent` 的 `WM_NCHITTEST` | **QOpenGLWidget 创建子级原生 HWND，WM_NCHITTEST 发往子 HWND，PetWindow 原生看不到** |

**关键发现：** `QOpenGLWidget`（Live2DView）在 Windows 上创建**子级原生 HWND**用于 OpenGL 渲染。鼠标移过 QOpenGLWidget 区域时，`WM_NCHITTEST` 直接发往子级 HWND 的 window proc，而非 PetWindow。PetWindow 的 `nativeEvent()` 从未看到 `WM_NCHITTEST` → 默认 `HTCLIENT` → 鼠标始终送达 → 穿透无效。

### 最终方案

替换 `nativeEvent`（QWidget 级，只收 PetWindow 自身 HWND 的消息）→ `QAbstractNativeEventFilter`（QApplication 级，捕获所有 HWND 的消息）：

| 维度 | 旧 `nativeEvent` | 新 `nativeEventFilter` |
|------|-----------------|----------------------|
| 作用域 | PetWindow 自己的 HWND | PetWindow + 所有子 HWND（含 QOpenGLWidget 子级） |
| QOpenGLWidget 子级 HWND | ❌ 不到达 | ✅ `IsChild()` 检测后捕获 |

### 事件流

```
WM_NCHITTEST → PetWindow::nativeEventFilter
  → HWND 范围检查：msg->hwnd 是 PetWindow 或 IsChild？
    └─ 否 → return false（其他窗口，Qt 默认处理）
    └─ 是 → ScreenToClient(winId) → /dpr → 逻辑像素
      → Live2DView::isPointInModelArea(localPos) ?
        ├─ true  → *result = HTCLIENT       [模型区域 → 正常交互]
        └─ false → *result = HTTRANSPARENT  [透明区域 → 穿透桌面]
```

### 改动的文件

| 文件 | 改动 |
|------|------|
| `pet/PetWindow.h` | 加 `#include <QAbstractNativeEventFilter>`；继承 `QAbstractNativeEventFilter`；`nativeEvent` 声明 → `nativeEventFilter` 声明 |
| `pet/PetWindow.cpp` | 构造中 `qApp->installNativeEventFilter(this)`；`nativeEventFilter()` 实现（HWND 范围过滤 + `IsChild` 检测 + `ScreenToClient` 统一坐标） |

### 已删除的旧代码

| 文件 | 删除内容 |
|------|---------|
| `pet/PetWindow.h` | `setClickThrough()` / `clickThrough()` 声明；`m_clickThrough` 成员 |
| `pet/PetWindow.cpp` | `setClickThrough()` 实现（`WA_TransparentForMouseEvents` 赋值） |
| `settings/SettingsWindow.h` | `setClickThroughEnabled()` 声明；`clickThroughToggled` 信号；`m_clickThroughCheckBox` |
| `settings/SettingsWindow.cpp` | CheckBox 创建/布局；`onSaveClicked` 中 `emit clickThroughToggled`；`setClickThroughEnabled()` 实现 |
| `core/App.cpp` | 设置面板 `setClickThroughEnabled(m_petWindow->clickThrough())` 调用；`clickThroughToggled` 信号槽连接 |

### 行为对比

| 场景 | 旧（手动穿透） | 新（智能穿透） |
|------|--------------|--------------|
| 点击模型区域 | 手动关穿透后才响应 | ✅ **自动响应**（摸头/动画/聊天） |
| 点击模型透明间隙 | 手动开穿透才穿透 | ✅ **自动穿透桌面** |
| 点击窗口空白边缘 | 手动开穿透才穿透 | ✅ **自动穿透桌面** |
| 设置面板 | 有"开启点击穿透"开关 | ✅ **已移除**（无手动开关） |
| 点击穿透与拖拽 | 穿透时无法拖拽 | ✅ **按住模型区域可拖拽** |

---

## 会话24 — 移除连续点击头部（m_petConsecutive）触发浮动文字（2026-07-03 ✅）

### 改动

删除 `PetWindow::triggerPet()` 中连续点击计数 `m_petConsecutive >= 4` 分支。

**删除的代码：**
```cpp
// PetWindow.cpp 原 241-245 行 — 已删除
if (m_petConsecutive >= 4) {
    floatText  = "主人，头好晕喵\n❤️ -1";
    floatColor = QColor(200, 180, 255);
    m_affection.add(-1);
    m_petConsecutive = 0;
}
```

### 改动的文件

| 文件 | 改动 |
|------|------|
| `pet/PetWindow.h:130` | 移除 `int m_petConsecutive = 0;` 成员变量 |
| `pet/PetWindow.cpp:232,234` | 移除 daily reset 和 `m_petConsecutive++` |
| `pet/PetWindow.cpp:241-245` | **核心：移除 `m_petConsecutive >= 4` 整个分支** |
| `pet/PetWindow.cpp:486` | 移除 `saveState()` 中的持久化 |
| `pet/PetWindow.cpp:539` | 移除 `loadState()` 中的读取 |
| `pet/PetWindow.cpp:588` | 移除 `switchCharacter()` daily reset |
| `pet/PetWindow.cpp:693,706,740` | 移除右键菜单中无关的 `m_petConsecutive = 0` 重置 |

### 保留不变

- ✅ 命中检测（HitArea / drawable / tight bounds）
- ✅ 摸头动作（`triggerPet()` 本身）
- ✅ 每日前 10 次点击好感度 +3（`m_petClickCount`）
- ✅ 表情/动画/聊天气泡
- ✅ InteractionManager 整体结构
- ✅ 双击/长按/其他交互

### 验证

`m_petConsecutive` 已从全项目彻底移除，全文搜索无匹配。

---

## 会话25 — 模型启动缩放初始化：默认 1.0 + 用户调整持久化（2026-07-03 ✅）

### 问题

首次启动时使用 `config.json` 的 `modelScale: 6.0`（default 角色），模型初始缩放过大且无法在后续启动中保持用户手动调整的值。

### 改动详情

| 文件 | 改动 | 原因 |
|------|------|------|
| `pet/PetWindow.cpp:100-103` | **`applyCharacter()`** — 移除 `m_currentModelScale = config.modelScale` 覆盖 | 不再使用 config.json 的 modelScale，改为信任 `loadState()` 设定的值（1.0 或用户保存值） |
| `pet/PetWindow.cpp:489` | **`saveState()`** — 新增 `window/modelScale` 保存 | 每次 `setModelScale()` + 常规 `saveState()` 调用时持久化当前缩放值 |
| `pet/PetWindow.cpp:502-506` | **`loadState()`** — 新增 `window/modelScale` 读取 | 有历史缩放记录则用 `qBound(0.3, saved, 10.0)` 恢复；无记录或异常则 fallback 1.0 |
| `pet/PetWindow.cpp:152` | **`setModelScale()`** — 末尾追加 `saveState()` | 用户通过设置面板调整缩放后自动保存 |

### 读取逻辑（loadState）

```cpp
const double saved = settings.value("window/modelScale", -1.0).toDouble();
m_currentModelScale = (saved > 0.0) ? qBound(0.3, saved, 10.0) : 1.0;
```

- 首次启动（无设置）→ `saved = -1.0` → 1.0 ✅
- 正常保存的值（如 3.0）→ bounded → 3.0 ✅
- 异常/非数值 → `toDouble() = 0.0` → 1.0 ✅

### 不受影响

- 缩放交互/滑块逻辑（`SettingsWindow` 不变）
- 模型 anchor/位置
- HitArea / SpeechBubble / Live2D 参数系统
- 角色切换时缩放保持（`applyCharacter` 中 `m_tightModelWidth > 0` 时不覆盖）

---

## 会话26/27 — 全屏游戏检测修复 + 智能穿透 setMask 方案（2026-07-03）

### 1. 当前智能穿透实现：双层穿透策略

**第一层：OS 级穿透（`SetWindowRgn` → `QWidget::setMask`）**

```cpp
void PetWindow::updateModelMask()
{
    float x0, y0, w, h;
    if (!m_live2DView->getModelPixelRect(x0, y0, w, h)) { clearMask(); return; }

    constexpr int kAnimSafetyPad = 8;
    QRect modelRect(qRound(x0)-8, qRound(y0)-8, qRound(w)+16, qRound(h)+16);
    modelRect = modelRect.intersected(QRect(0, 0, width(), height()));
    if (modelRect == QRect(0, 0, width(), height())) { clearMask(); return; }
    setMask(QRegion(modelRect));
}
```

- **mask 外**（窗口边缘 8px 外区域）：Windows 内核直接穿透，不发送任何鼠标消息给桌宠
- **mask 内**：走第二层

**第二层：应用级穿透（`QAbstractNativeEventFilter` + `WM_NCHITTEST`）**

```cpp
nativeEventFilter → WM_NCHITTEST → isPointInModelArea?
  ├─ true  → *result = HTCLIENT       ← 模型区域，正常交互
  └─ false → *result = HTTRANSPARENT  ← 模型外透明像素，穿透
```

- 处理 `WM_NCHITTEST` 消息，区分模型区域/透明区域
- 在 mask 内部的区域提供像素级精度

**`updateModelMask` 更新时机：**

| 触发点 | 更新时机 |
|--------|---------|
| `modelLoaded` 信号 | 模型初始加载完成 |
| `stableBoundsReady` 信号 | idle 动画稳定后（~500ms） |
| `resizeEvent` | 窗口缩放/模型缩放/角色切换 |

**各区域穿透行为：**

```
┌── PetWindow ───────────────────────────────┐
│  ← 8px 安全边距 →                          │  ← OS 级（SetWindowRgn）
│   ┌─ 模型紧贴包围盒 ───────────────────┐   │
│   │ [模型区域 (tight bounds)]          │   │  ← 应用级（WM_NCHITTEST）
│   │  ┌─ HitArea 头部 ──┐ ← 摸摸头     │   │      → HTCLIENT
│   │  │                 │              │   │
│   │  ├────────────────┤                │   │
│   │  │ HitArea 身体 ──┤ ← 随机动作    │   │
│   │  └────────────────┘               │   │
│   └────────────────────────────────────┘   │
│                                            │
└────────────────────────────────────────────┘
```

---

### 2. 已修改文件

| 文件 | 改动概要 |
|------|---------|
| `CMakeLists.txt` | `target_link_libraries` 添加 `dwmapi.lib`（`DwmGetWindowAttribute` 需要） |
| **`pet/Live2DView.h`** | 新增公开方法 `getModelPixelRect()` |
| **`pet/Live2DView.cpp`** | 实现 `getModelPixelRect()`（代理 `computeModelArea`） |
| **`pet/PetWindow.h`** | 新增 `setFullscreenOverlayEnabled()` / `fullscreenOverlayEnabled()`；新增 `updateModelMask()`；新增全屏检测成员 5 个；新增 `#include <QTimer>` |
| **`pet/PetWindow.cpp`** | **新增** `setupFullscreenDetection()` / `checkForegroundFullscreen()` / `applyFullscreenOverlay()` / `removeFullscreenOverlay()` / `onFullscreenCheckTimer()` / `updateModelMask()`；修改构造/析构/saveState/loadState/modelLoaded/stableBoundsReady/resizeEvent |
| **`core/App.cpp`** | `keepOnTopToggled` 改接 `setFullscreenOverlayEnabled`；设置初始化改读 `fullscreenOverlayEnabled()` |

---

### 3. 仍存在的问题

- **智能穿透偶发失效**（mask 设置后，偶发出现模型区域内鼠标穿透到桌面的情况，模型区域外拦截鼠标的情况更少但依然存在）
- Haru 模型切换闪退（未修复，session 15 之前已存在）
- modelScale=2.0 时纵向拉伸（未解决，session 10 已文档化）

---

### 4. 最近一次修改内容（会话26/27）

**会话26 — 全屏游戏检测 + 三层状态管理（2026-07-03）**

原问题：复选框"游戏全屏时始终显示桌宠"直接调用 `setKeepOnTop(true)` → `SetWindowPos(HWND_TOPMOST)`，无全屏检测。

修复：
```
checkbox → setFullscreenOverlayEnabled(bool)
                ↓
       200ms 定时器（onFullscreenCheckTimer）
                ↓
       checkForegroundFullscreen()
         ├─ GetForegroundWindow()
         ├─ 过滤自身/桌面/Shell/任务栏
         ├─ MonitorFromWindow → rcMonitor（完整显示器区）
         ├─ DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)
         │   └─ fallback → GetWindowRect()
         └─ 窗口覆盖显示器 + 3px 容差？
```

三层状态管理（严格分离）：
```
fullscreenOverlayEnabled   ← 复选框设置项
fullscreenGameActive       ← 检测结果（当前前台是否真正全屏）
fullscreenOverlayApplied   ← 覆盖应用状态（是否已 HWND_TOPMOST）
```

关键教训：全屏进入时**禁止调用 `raise()`/`requestActivate()`/`SetForegroundWindow()`**，
否则抢走游戏焦点 → 下一帧检测认为游戏不是前台 → 覆盖被撤销 → 循环。

**会话27/X — 智能穿透 setMask 方案 + 稳定运动包络（2026-07-03）**

第一阶段（会话27）：`WM_NCHITTEST` + `HTTRANSPARENT` 跨进程穿透不可靠 → 改用 `setMask(QRegion)` 提供 OS 级穿透保障。同时去除 `HTTRANSPARENT`，mask 内统一 `HTCLIENT`。

第二阶段（会话28/29）：发现 50ms 定时器粒度的 `updateModelMask()` 调用 `SetWindowRgn` 导致 Physics 掉帧。改为**稳定运动包络**方案：

| 参数 | 值 | 说明 |
|------|----|------|
| 更新频率 | **5Hz**（200ms） | 原来 20Hz 导致 DWM 重排 + OpenGL 渲染管线竞争 |
| 扩张边距 | **25 logical px** | 围绕各 Drawable 扩张，覆盖 Physics/Motion 摆动 |
| 扩张策略 | **即时更新** | 模型超出当前包络 3px 以上 → 立即 `SetWindowRgn` |
| 收缩策略 | **延迟 2 秒** | `m_envelopeStableTicks` ≥ 10（200ms × 10）后才收缩 |
| 防重复 | **QRegion 缓存** | `m_cachedMask == region` 时跳过 `setMask` |
| 轮询控制 | **`m_maskActive`** | 4 秒无变化后自动停止轮询，事件（headClicked/bodyClicked/resize）重新激活 |

### 当前已知问题

1. **持续运动时仍有少量不规则裁剪** — `setMask`/`SetWindowRgn` 同时裁剪 OpenGL 渲染输出，低频（5Hz）mask 对于快速 Physics 摆动的跟随存在时间差。
2. Haru 模型切换闪退（未修复，session 15 之前已存在）
3. modelScale=2.0 时纵向拉伸（未解决，session 10 已文档化）

### 下一阶段计划：拆分渲染窗口与 InputProxyWindow

`setMask`/`SetWindowRgn` 的根本问题是它同时影响输入穿透和 OpenGL 渲染裁剪。即使低频更新也会因为 mask 形状与当前帧模型轮廓不匹配而裁剪可见像素。

计划：
- **PetWindow** → 保留为 InputProxyWindow（`FramelessWindowHint` + `Tool`），始终无 mask、全窗口 `HTTRANSPARENT`，只做鼠标事件穿透决策
- **新 PetRenderWidget**（无 `WA_TranslucentBackground`）→ 只负责 Live2D OpenGL 渲染，drawable 裁剪由此处控制
- 穿透逻辑从 `SetWindowRgn` 改为局部坐标空间内的命中检测 → `HTTRANSPARENT` / `HTCLIENT`
- 渲染不再受 `SetWindowRgn` 裁剪

> **注意：** 当前 `setMask` + `HTCLIENT` 方案对静态/低频交互已足够稳定。中等幅度摆动的裁剪是视觉问题（渲染被裁）而非输入穿透问题。如果更在意输入穿透可靠性而非视觉完整，该方案可用。

### 本轮修改文件

| 文件 | 改动 |
|------|------|
| `pet/PetWindow.h` | 新增 `clearMaskAndCache`/`applyMaskRegion`/`dilateRegion`/`onMaskTimer` 声明；新增包络成员 `m_envelopeRegion`/`m_envelopeBounds`/`m_envelopeStableTicks`/`m_maskActive`；新增常量 `kMaskDilationPx=25`/`kMaskPollIntervalMs=200`/`kMaskStableThreshold=10`；新增 `#include <QElapsedTimer>` |
| `pet/PetWindow.cpp` | 构造函数中 timer 从 50ms 改为 200ms 连接 `onMaskTimer`；`modelLoaded`/`stableBoundsReady` 信号中复位 `m_hasCachedMask=false`/`m_maskActive=true`；`updateModelMask()` 重写为包络管理（扩张即时 + 收缩延迟 + 3px 阈值 + 5Hz 限频）；新增 `clearMaskAndCache()`/`applyMaskRegion()`/`dilateRegion()`/`onMaskTimer()`/`markMaskDirty()` 改为同时设 `m_maskActive=true`；`resizeEvent` 中复位缓存 |
| `pet/Live2DView.h` | 新增 `getVisibleDrawableRects()` 声明；新增 `#include <QRect>`/`<QVector>` |
| `pet/Live2DView.cpp` | 新增 `getVisibleDrawableRects()` 实现（逐 Drawable AABB + 投影链映射） |

### 当前 git diff 状态

（项目未使用 git，所有改动手动管理）

### 绝对不能改的部分（新增：InputProxyWindow 后以下依然禁止修改）

| 模块 | 约束 |
|------|------|
| Live2D 渲染 | `Live2DView` 渲染管线、`ModelController`、`Cubism SDK` |
| 模型动作 | 动作加载、Idle 循环、Tap 动作、交叉淡入淡出 |
| HitTest 精细逻辑 | `performHitTest`、`pixelToCanvas`、三层漏斗、Alpha 检测 |
| InteractionManager | 双击/长按/挂机/单击分发 |
| SpeechBubble | 气泡显示/淡出/定位 |
| 浮动文字 | `FloatingTextWidget` 自绘制 |
| 聊天气泡 | `ChatWindow` |
| QA/睡眠提醒 | `setupBedtimeReminder`、深夜提醒 |
| 拖拽逻辑 | `mousePressEvent`/`moveEvent`/`releaseEvent` 中的拖拽 |
| 全屏管理器权限 | 全屏管理器**只能控制**显示状态 + Z-order；**禁止修改** mask、HitTest、input 标志、WS_EX_TRANSPARENT、WindowTransparentForInput、WA_TransparentForMouseEvents |
| **InputProxyWindow（未来）** | 只做鼠标事件穿透/拦截决策，禁止涉及 OpenGL 渲染、Live2D 模块、HitTest 精细逻辑 |
| **PetRenderWidget（未来）** | 只负责 OpenGL 渲染，禁止涉及鼠标穿透决策 |
6. 模型动画中 tight bounds 变化时，mask 是否未及时更新
