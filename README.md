# 校园网连接修复助手 · CampusLinkRepair

**校园网认证保活 · 本机代理残留恢复**

校园网络连接守护工具，定期向认证门户续接会话，并在 Windows 上识别和恢复失效的本机代理配置。提供 Windows 与 Linux 两套 C++ 实现，当前默认适配重庆大学认证门户。

GitHub 仓库：`cunyk/CampusLinkRepair-cqu`；Windows 程序为 `campus-link-repair.exe`，Linux 程序及 systemd 服务为 `campus-link-repair`、`campus-link-repair.service`。

## **项目背景与设计理念**

项目目标是提供在 Windows 与 Linux 上运行的完善的校园网认证工具，支持 IPv6、通过 `SERVER_IP` 绕过 DNS 解析、不经过系统代理直连门户等功能。配置文件与可执行文件分离。

**核心特性：**

- **配置分离**：账号信息和常规设置与核心代码严格分离，配置文件独立于可运行文件
- **多平台支持**：提供 Windows (.exe) 和 Linux 二进制可执行文件
- **C++ 实现**：Windows 版基于 WinHTTP 系统 API，Linux 版依赖 libcurl，使用系统原生 API 实现极低的资源开销
- **运行模式**：Windows 版可把控制台隐藏到托盘（后台）或还原（前台）（仅原版 cmd/powershell，安装 WindowsTerminal 后不生效）；Linux 版面向 systemd 后台服务设计，输出进入 systemd journal
- **Windows 本机代理恢复**：不绑定 VPN/代理客户端。客户端未启动而系统仍指向其本机代理或 PAC 服务时，确认端口持续拒绝连接至少 30 秒且门户直连认证成功后，自动停用失效项；保留地址，不修改远程代理、DNS、路由或 TUN 网卡。

---

## **快速开始**

### 使用 C++

预编译版本无需大型运行时环境；Linux 版本需要系统提供 libcurl 运行库。

**Windows 用户:**

1. 下载 `campus-link-repair-windows-x64.zip`
2. 解压，**确保 `config.yaml` 与 `campus-link-repair.exe` 在同一目录**
3. 使用记事本编辑 ``config.yaml`` ，按照注释的提示**补全上网账号信息**
4. 双击运行 `campus-link-repair.exe`；如需登录后无窗口后台自动运行，按 `windows-headless-setup.md` 配置任务计划程序

**Linux 用户:**

1. 下载 `campus-link-repair-linux.tar.gz`
2. 解压并赋予执行权限，**确保 `config.yaml` 与 `campus-link-repair` 在同一目录**
3. 编辑 ``config.yaml`` ，按照注释的提示**补全上网账号信息**
4. 确认系统已安装 libcurl 运行库（由 `curl` 包提供）
5. 进入解压目录后运行 `./campus-link-repair`（不推荐长期手动运行，建议配置 systemd 启动项，请参考 `linux_systemd-setup.md`）

离线自检：Linux 运行 `./campus-link-repair --self-test`，Windows 运行 `campus-link-repair.exe --self-test`。自检不读取配置文件、不访问网络，校验响应分类与本机地址选择逻辑，全部通过时退出码为 `0`。可用于部署前确认二进制在目标系统可正常运行。

### Windows：忘关 VPN/代理后无法上网

Windows 配置项 `PROXY_RECOVERY` 支持 `auto`（默认自动恢复）、`detect`（仅诊断）、`off`（关闭）。正常使用中的本机代理、远程代理、连接超时或不明确的配置均保留；启动观察期可避免代理客户端尚未启动完就被关闭。程序仅检查当前运行用户的 LAN 代理设置，需以遇到故障的同一 Windows 账户运行。

无需账号配置的只读检查：

```powershell
.\campus-link-repair.exe --diagnose-network
```

这项功能针对系统代理残留；TUN、DNS、路由或客户端断网保护造成的故障，需要在对应客户端恢复网络或正常退出后重试，程序会给出排查提示。自动恢复仅切换失效项的启用标志，原代理地址和 PAC URL 保留，可在 Windows“设置 → 网络和 Internet → 代理”中重新启用。

---

## 源码构建

Windows（MinGW，在项目根目录运行）：

```powershell
g++ src/windows/campus-link-repair.cpp -o campus-link-repair.exe -lwinhttp -liphlpapi -lws2_32 -lshell32 -luser32 -static -O2
```

Linux（安装 libcurl 开发包后，在项目根目录运行）：

```bash
g++ src/linux/campus-link-repair.cpp -o campus-link-repair -lcurl -O2
```

将对应平台的 `config.yaml` 复制到可执行文件所在目录。Windows 相对程序目录读取配置，Linux 需在该目录中启动。

## **许可证**

本项目在 Optance（Si/nagelanping）的 MIT 许可代码基础上修改维护，原始版权声明完整保留于 [LICENSE](LICENSE)。分发需保留该声明，来源说明见 [NOTICE](NOTICE)。
