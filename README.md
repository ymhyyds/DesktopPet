# DesktopPet

<p align="center">
  基于 Qt 6 与 Live2D Cubism SDK 开发的 Windows 桌面宠物
</p>

<p align="center">
  <a href="https://github.com/ymhyyds/DesktopPet/releases/latest">
    <img src="https://img.shields.io/github/v/release/ymhyyds/DesktopPet?label=Release" alt="Release">
  </a>
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011-blue" alt="Platform">
  <img src="https://img.shields.io/badge/Architecture-x64-blue" alt="Architecture">
  <img src="https://img.shields.io/badge/Qt-6.11.1-41CD52" alt="Qt">
  <img src="https://img.shields.io/badge/Live2D-Cubism%20R6-orange" alt="Live2D">
</p>

DesktopPet 是一款支持 Live2D 模型、AI 对话、语音朗读、天气感知、桌面互动和个性化设置的 Windows 桌面宠物应用。

程序采用便携式发布，无需安装 Qt 或 Python。下载 Release 压缩包并完整解压后，即可直接运行。

<!--
将项目截图保存为 docs/images/preview.png 后，取消下一行注释：

![DesktopPet 运行预览](docs/images/preview.png)
-->

## 功能特性

### Live2D 桌面宠物

- Live2D Cubism 实时渲染
- 透明无边框窗口
- 鼠标拖动与贴边吸附
- 模型尺寸自由调整
- 头部追踪和眼球追踪
- Physics 与 Pose 联动
- 平滑待机动画循环
- 多模型切换
- 自动扫描新导入的模型
- 支持用户调整模型参数

### 桌面互动

- 双击模型区域打开聊天窗口
- 点击头部触发摸头互动
- 点击身体播放随机动作
- 右键打开托盘与互动菜单
- 好感度和喂食状态
- 点击穿透模式
- 全屏应用上方显示
- 漫画式说话气泡
- 可自定义气泡颜色

### AI 对话

- 集成 DeepSeek API
- 支持流式回复
- 支持对话上下文与摘要记忆
- 支持自定义 AI 风格
- 支持实时时间与天气信息
- AI 回复可联动：
  - Live2D 情绪表情
  - 说话气泡
  - Edge TTS 语音
  - 本地聊天记录

### 对话管理

- SQLite 本地保存聊天记录
- 关键词模糊搜索
- 逐条浏览搜索结果
- 清除聊天记录
- 导出为：
  - TXT
  - JSON
  - HTML

### 语音合成

- 内置自包含 Edge TTS
- 无需安装 Python
- 支持调节语音、语速、音量和音高
- 支持中文及特殊字符
- 新请求自动停止旧语音
- 自动清理临时音频文件
- 支持可选 VoiceVox 后端

> VoiceVox 服务不包含在发布包中，使用 VoiceVox 时需要自行安装并启动相应服务。

### 时间与天气

- 自动获取当前时间
- 获取当前天气、温度、体感温度和湿度
- AI 可结合时间和天气生成回复
- 异步网络请求，不阻塞桌宠界面
- 请求失败与超时处理

### 截图

- 截取透明背景 Live2D 模型
- 高分辨率渲染改善边缘质量
- 自动保存 PNG
- 自动复制图片和文件路径到剪贴板
- 支持自定义截图保存位置

### 其他功能

- Windows Startup 快捷方式开机自启
- 单实例保护，防止重复启动
- 深夜就寝提醒
- 自动发言
- 自定义聊天背景
- 自定义气泡颜色
- 自定义窗口吸附距离
- 自定义挂机超时时间

## 下载

前往：

[GitHub Releases](https://github.com/ymhyyds/DesktopPet/releases/latest)

下载：

```text
DesktopPet-Windows-x64.zip
