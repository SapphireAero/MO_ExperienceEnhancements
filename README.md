# SA_EE

《心灵终结》3.3.6 的体验增强插件，从 [MO_ExperienceEnhancements](https://github.com/MagicShiba/MO_ExperienceEnhancements) 提取，适合单机和联机。

## 功能

- 建筑排队、半透明放置预览、地基驱离、快速维修和电力显示。
- 范围下令、自动砍树、攻击过滤、侵略模式、T 键选择、飞机路径点及单机命令扩容。
- 聊天框与输入法定位、选中单位信息和射程圈。聊天需要手动发送。
- 离屏可见信标箭头、联机满槽替换、多观战修复，以及光照、提示框和菜单性能修复。

## 安装

构建后，将 `dist` 中的 `SA_EE.dll` 和 `SA_EE.ini` 放到 `gamemd.exe` 同级目录，通过原客户端开局。升级时先移走旧插件 DLL，避免和原版 MO_EE、SapphireA 或 MOEE Selected 同时加载。

编辑 `SA_EE.ini` 可切换功能，重新开局生效。常用操作为 **Shift+中键**范围下令、**Alt+A** 切换侵略模式，单选后按住 **Ctrl** 查看射程。

## 构建

使用 32 位 MinGW-w64 运行 `python build.py`，DLL 输出到 `dist/`。开发入口见[源码结构](源码结构.md)。
