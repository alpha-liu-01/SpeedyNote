# 极疾记 SpeedyNote

---

**SpeedyNote** 是一款专注于压感墨水输入的跨平台高效笔记App，它具有以下特点：

- C++/Qt6编写，没有Web应用的巨大overhead，可以在Intel Atom N450配1GB内存的设备使用，2011年平板电脑在133Hz采样率CPU占用10%

- 超细腻压感墨水，手感超越OneNote和Xournal++，低端设备上延迟远低于两者

- 全局防误触，即使你使用的是ROG 幻13也可以免受误触困扰

- 创新MagicDial UI，一个旋钮可以掌控一切，不再需要常驻工具栏占用宝贵画布空间

- 独家任天堂Joy-Con支持，极低成本实现数位屏遥控器快捷键，所有操作都是一步，旋钮高效控制MagicDial

- 支持直线工具和套索工具，编辑画布轻松简便

- 支持加载PDF，在其上做注释，1000页PDF高速加载，很难卡顿

- 支持Windows 7, 8, 10, 11，MacOS，大多数Linux发行版。可轻松打包为`.deb`, `.rpm`,`.pkg.tar.zst`,`.apk`，
  ~~同时提供Flatpak~~，支持x86-64和aarch64，几乎所有设备均可运行

- Markdown即时贴，即使用户没有电磁笔，文字笔记也可以非常便捷高效

- 支持中文、英文、法语、西班牙语并可以在未来扩展更多语言支持，用户可覆盖半个地球

# 安装

---

#### Windows

直接安装自解压程序即可。Windows 7/8
用户请使用Legacy版本。

#### Mac OS

下载Mac版本的SpeedyNote运行dmg文件夹中的补全依赖的脚本后把app拖入应用程序文件夹即可完成安装。

#### Linux

安装对应系统的`deb`,`rpm` `.pkg.tar.zst` 和 `.apk` 包

对于Debian/Ubuntu等用户，您也可以使用
```bash
wget -O- https://apt.speedynote.org/speedynote-release-key.gpg | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/speedynote.gpg && echo "deb [arch=amd64,arm64 signed-by=/etc/apt/trusted.gpg.d/speedynote.gpg] https://apt.speedynote.org stable main" | sudo tee /etc/apt/sources.list.d/speedynote.list && sudo apt update && sudo apt install speedynote
```
这一行命令来直接安装SpeedyNote。并在后续直接用`sudo apt update && sudo apt upgrade` 来更新。

# 图片

---

![main](https://i.imgur.com/5PY0LhO.png)

![acer](https://i.imgur.com/f7CrRvN.jpeg)

# 已知特性和缺陷

--- 

> 这些缺陷可能会在不久的将来得到修复。

- PDF为只读格式，暂时无法将笔记和PDF本体融为一体

- ~~在Windows 7上MagicDial的咔声无法正常播放~~ 已修复

- 每个按钮的提示信息在使用手写笔悬浮的时候可能会正常显示（KDE Plasma）也可能不会（Windows）。先前的一个修复方式似乎会导致经常崩溃，所以这个修复暂时被移除了。

- ~~在Phosh桌面上似乎会因为设备映射问题经常崩溃。~~ 可能是因为Phosh的更新自己好了

- ~~Markdown即时贴无法嵌入图片~~ 当前可以直接在画布中插入来自本地或者剪贴板的图片。

- ~~套索选中的区域无法跨页面复制粘贴~~ 在0.8 版本已经得到了修复。

# 翻译

--- 

> 寻求志愿翻译。请使用Qt Linguist将SpeedyNote带到世界上的每一个角落。感谢大家。

- 简体中文 （100%）

- 英语 （100%）

- 法语 （<35%）（机翻）

- 西班牙语 （<35%）（机翻）

# 开源许可证

--- 

SpeedyNote有一个MIT License。也就意味着大家其实可以随意折腾随意玩坏。我不是很在乎这件事。

```
MIT License

Copyright (c) 2025 alpha-liu-01

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

# 社区

---

如果你有对SpeedyNote的意见和建议，请多多在Issues中发布主题。在Issues最好呈现纯英文或者中英双语的话题。如果您不擅长英语请来

> SpeedyNote 问题反馈和开发者交流 QQ 744918470

# 构建

---

#### Windows

请访问[Windows构建文档](../docs/zh_Hans/SpeedyNote_Windows_Build_zh-Hans.md)



#### Darwin

请访问[macOS构建文档](../docs/zh_Hans/SpeedyNote_Darwin_Build_zh-Hans.md)



#### Linux

1. 运行 `./build-package.sh`
2. 安装适用您 Linux 发行版的软件包。
