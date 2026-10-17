# HotImages —— UE5 外部图片热更新纹理插件

让**外部 png/jpg 目录**与 UE 内的 `UTexture2D` 保持双向同步:

- 外部改图 → 编辑器/运行时里的纹理自动更新;
- 在 UE 里正常导入、换源、调整纹理设置 → 目录里自动维护一份对应的 png,供运行时目录继续热更新。

## 目录结构

```
F:\UnrealProject\HotImages\
├─ HotImages\                  # 插件本体(复制到任意工程的 Plugins/ 即可使用)
│  ├─ HotImages.uplugin
│  ├─ Config\DefaultGame.ini   # 默认把 Content/HotImages 登记为打包松散目录
│  └─ Source\
│     ├─ HotImagesRuntime\     # 设置、GameInstance 子系统、Windows 目录监听、解码与原地刷纹理
│     └─ HotImagesEditor\      # 自动导入/再导入、反向导出 png、打包登记、Tools 菜单
├─ HotImageDemo\               # 演示工程(UE 5.4)
│  ├─ Content\HotImages\       # 示例 png/jpg;代理纹理自动生成在同一个目录
│  └─ Config\                  # WatchFolders、Always Cook 等演示配置
├─ SetupDemoPlugin.ps1         # 为演示工程建立插件 junction(见下)
└─ README.md
```

## 运行要求

- UE 5.x;本机已在 **UE 5.4** 与 **UE 5.7** 编译验证,演示工程 `EngineAssociation` 为 5.4。
- 目录监听基于 Windows `ReadDirectoryChangesW`,运行时热更新目标平台为 **Windows**。
- UE 工程路径建议为纯 ASCII(本工程即 `F:\UnrealProject\HotImages`);若放在含中文的路径,UE 5.4 的 UBT 可能报 `cannot open precompiled header file`(构建工具问题,与插件无关)。

## 安装

### 1. 装到任意 UE 工程

把 `HotImages/` 整个目录复制到目标工程的 `Plugins/HotImages`,在 Project Settings > Plugins 中启用。

### 2. 使用本仓库的演示工程

首次使用前运行一次(在仓库根目录):

```powershell
.\SetupDemoPlugin.ps1
```

该脚本在 `HotImageDemo/Plugins/HotImages` 建立一个 junction,指向仓库里的插件目录。之后用编辑器打开 `HotImageDemo/HotImageDemo.uproject` 即可。

## 核心概念与目录映射

每个配置目录称为一个 **WatchFolder**(默认 `Content/HotImages`)。文件、资产、运行时路径一一对应:

| WatchFolder(相对 Content)| 磁盘文件 | UE 资产路径 | 打包后运行时磁盘文件 |
|---|---|---|---|
| `HotImages` | `Content/HotImages/foo.png` | `/Game/HotImages/foo` | `<包体>/HotImageDemo/Content/HotImages/foo.png` |
| `TEST` | `Content/TEST/bar.jpg` | `/Game/TEST/bar` | `<包体>/HotImageDemo/Content/TEST/bar.jpg` |

SlotName = 图片基底名(`foo.png` → `foo`)。

## 双向同步

### A. 外部图片目录 → UE(图驱动纹理)

- 启动编辑器时自动扫描每个 WatchFolder:
  - 发现新 png/jpg/jpeg → 自动导入并创建同基底名代理 `UTexture2D`;
  - 发现文件被外部改写 → 对同一资产静默 reimport,引用不失效;
  - 文件被删除 → **不删除资产**(避免破坏材质/UMG 引用),只记录警告。
- 同一基底名同时存在 png 与 jpg 时,固定以 **png 为准**。
- 临时文件(`*.tmp`、`*.part` 等)与点开头文件会被忽略;写入中的文件失败时保留上一版内容,事件合并去抖后自动重试。

### B. UE → 外部图片目录(纹理驱动 png)

只对**直接位于某个 WatchFolder 下的资产**生效(例如 `/Game/HotImages/foo`),避免污染整个项目:

- 用 Content Browser 正常导入图片到该目录时,自动从纹理 Source 导出一份同名 `.png`(jpg 源也会转成 png 输出);
- 之后对该纹理 **Reimport(换源文件)**,或在纹理编辑器修改 `Padding and Resizing`(PowerOfTwo / Pad / Stretch / 指定分辨率)、填充色、Chroma Key 等设置时,自动重新导出 png;
- 导出内容会按纹理设置应用尺寸变换,尽量与 UE 内最终显示一致;
- 插件自己刚导出的 png 不会再次触发回读导入(有文件时间戳保护),不会弹“override asset”确认框,也不会形成循环。

## 项目设置(Project Settings > Plugins > Hot Images)

| 设置 | 默认 | 说明 |
|---|---|---|
| `WatchFolders` | `HotImages`(一项) | 可增删多个 Content 相对目录,每个目录独立监听、独立映射资产路径 |
| `bAutoSyncInEditor` | true | 编辑器内自动导入/再导入 |
| `UpdateDebounceSeconds` | 0.5 | 文件事件去抖秒数 |
| `bApplyChangesAtRuntime` | true | 打包运行时的目录监听与原地刷新 |

**打包登记自动同步**:在项目设置里往 `WatchFolders` 添加目录时,插件会自动把该目录追加到 Project Settings > Packaging > Additional Non-Asset Directories To Copy(`DirectoriesToAlwaysStageAsNonUFS`)并写入 `DefaultGame.ini`,打包时图片会以松散文件落到包体对应 Content 路径。移除 WatchFolder 时不会自动移除该登记项(避免误删手动用途)。

## Runtime 使用

编辑器/蓝图/UMG 中把代理纹理当作普通纹理引用即可。需要按名称动态获取时,使用 GameInstance 子系统:

### `UHotImageSubsystem`

| 成员 | 类型 | 说明 |
|---|---|---|
| `GetTexture(SlotName)` | 函数 | 返回该槽位的 `UTexture2D`;运行时若磁盘文件较新会自动补刷新 |
| `RefreshSlot(SlotName)` | 函数 | 强制按磁盘文件刷新单个槽位 |
| `RefreshAll()` | 函数 | 扫描所有配置目录并刷新已加载纹理 |
| `IsRuntimeWatching()` | 函数 | 运行时监听是否启用 |
| `OnHotImageUpdated(SlotName, Texture)` | 动态多播事件 | 纹理成功更新后触发 |

打包运行时只刷新**已加载**的代理资产(引用它的材质/UMG/关卡已经加载);尚未加载的槽位通过 `GetTexture` 读取时会自动补一次刷新。若想在打包后动态加载从未被引用的代理,需要在打包设置里把对应资产目录加入 Always Cook(演示工程已把 `/Game/HotImages` 加入)。

### 编辑器内模拟运行时路径

```powershell
UnrealEditor.exe HotImageDemo\HotImageDemo.uproject -game -HotImagesRuntimeSimulation
```

该开关会关闭编辑器“资产再导入”路径、启用与打包一致的内存刷新路径,用于不打包调试。

## 打包与部署

1. 图片文件:WatchFolder 已在打包设置里登记为 NonUFS 松散目录 → 打到包体 `Content/<目录>`,不进入 pak,运行期可被外部程序直接覆盖。
2. 代理纹理:被材质/关卡引用 → 正常 cook 进 pak;未被引用但要在运行时按路径加载 → 把资产目录加入 Always Cook。
3. 运行期替换:直接覆盖安装目录下 `Content/<目录>/foo.png`,已显示的纹理自动原地刷新。

## 已知限制与待处理项

- **打包目录里的 `.uasset`**:UE 对 NonUFS 目录是“整目录复制”,不过滤扩展名;当前代理 `.uasset` 与 png 同目录,因此打包后的 `Content/HotImages` 中会出现 `.uasset`。若要求“打包后的目录只有 png”,需要把代理资产目录与图片目录拆开(例如代理统一放另一个 Content 目录),插件后续需同步调整导入落点、`GetTexture` 映射与反向导出判断。
- 目录不递归子目录;只支持 png/jpg/jpeg。
- 非 Windows 平台不启动目录监听,可用 `RefreshSlot`/`RefreshAll` 手动刷新。
- 编辑器进程内(含 PIE)走资产再导入路径,保证内容持久化;不另起内存替换,避免同一资产双写。
- 插件写入 png 使用“临时文件 + 改名覆盖”,避免运行时读到半个文件。

## 已做验证

- UE 5.4(源码版 5.4.4 与启动器版 5.4)编译通过,UE 5.7 编译通过;
- 编辑器运行验证:首次扫描自动生成代理、新增 png 自动导入、外部改写触发静默 reimport、jpg 导入后自动导出 png;
- 多 WatchFolder 验证:两个目录同时监听并各自建代理;
- `-game -HotImagesRuntimeSimulation` 验证:运行时原地替换纹理数据(含尺寸变化),进程稳定;
- 修复记录:`Stop()` 线程析构递归导致的栈溢出、退出编辑器时 `GetMutableDefault` 崩溃、自导出 png 引发的二次导入与 override 弹窗。

## 其它文档

- 完整中文使用手册（含打包目录与运行效果截图）：[`Docs/HotImages使用手册.md`](Docs/HotImages使用手册.md)。
- 插件子目录内有精简说明 `HotImages/README.md`。
- 演示工程的 HUD 代码演示了 `GetTexture` 的使用方式。
