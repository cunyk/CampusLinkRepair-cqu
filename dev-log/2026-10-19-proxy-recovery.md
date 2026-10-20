# 2026-10-19：Windows 本机代理残留恢复

## 行为

- 不绑定 VPN/代理客户端名称，检查当前运行用户的 LAN 手动代理和本机 HTTP(S) PAC 服务。
- `PROXY_RECOVERY` 支持 `auto`（默认）、`detect`、`off`；新增配置项有严格校验，错误退出 78。
- 自动恢复要求同一配置在至少 30 秒间隔的多次检查中端口均拒绝连接，且当前一轮直连门户认证成功。端口超时、未知格式、远程或混合远程代理、正常监听的端口均保留。
- 写入前重新读取配置并重探端口，防止客户端在观察期间重启；只关闭确认失效项的启用标志，保留地址、PAC URL、绕过列表和自动发现设置。写入后广播刷新并验证结果。
- `--diagnose-network` 不需要账号配置，只读当前用户代理及探测 loopback TCP，不修改设置、不访问门户。检查成功退出 0 表示诊断完成，不代表校园网已可用。
- TUN、DNS、路由、客户端断网保护不自动重置；门户直连认证失败时给出提示。此功能不能保证解决所有 VPN 残留故障。
- Windows 响应分类增加数值结束边界检查，防止小数/指数前缀误判成功后触发恢复。Linux 文件未改。

## 实现与依赖

- 新模块：`src/windows/ProxyRecovery.h`；由 Windows 主源文件包含，原有单文件编译命令继续有效。
- 通过系统目录中的 WinINet DLL 调用公开接口，不增加第三方运行依赖或链接参数。
- Windows 7+ 查询 `INTERNET_PER_CONN_FLAGS_UI`，失败回退 `INTERNET_PER_CONN_FLAGS`；写入使用 `INTERNET_PER_CONN_FLAGS`。
- 本机拒绝连接在 Windows 上实测约 2 秒才返回，因此单地址探测等待最多 2.5 秒；同一端点缓存结果，并限制复杂配置的探测工作量。
- 只能操作实际运行账户的 LAN 配置；服务账户、浏览器独立代理、拨号/VPN 连接专属代理配置不在范围内。

接口依据：[连接选项结构](https://learn.microsoft.com/en-us/windows/win32/api/wininet/ns-wininet-internet_per_conn_optionw)、[设置与读取 Internet 选项](https://learn.microsoft.com/en-us/windows/win32/wininet/setting-and-retrieving-internet-options)。

## 验证

- 使用临时目录中的官方 w64devkit 2.10.1 编译；下载包 SHA-256 与 GitHub 发布摘要一致。
- Windows `--self-test`：47 项通过（21 项响应/地址检查，26 项端点解析及恢复策略检查）；不访问真实门户、不修改代理设置。
- `tests/windows/proxy-recovery-test.cpp`：真实 loopback 监听/拒绝连接，配合模拟 WinINet API，覆盖手动代理/PAC 恢复、远程配置保留、FLAGS_UI 回退、设置变化、读取/写入失败、通知失败、写后验证，以及 auto/detect/off 和认证失败门控。测试未修改开发者的实际代理配置。
- 配置读取检查：默认值、auto、detect、off 通过；非法值、空值、缺文件均返回 78；检查过程不进行认证或设置修改。
- 实机运行只读 `--diagnose-network`，正常退出 0。
- 与原始源码包对比确认 Linux 主源文件、配置及 systemd unit 未改；修改文件为 LF、无行尾空格。
- 新增恢复模块无 `-Wall -Wextra` 警告；原 Windows 文件仍有既有的结构初始化、未使用线程参数及 `GetLastError` 未调用等警告，本次未扩大范围修复。
- 尚未验证真实校园网认证和实际 Windows 用户配置写入后的浏览器恢复；写入测试均使用模拟 API。

从项目根目录编译集成测试（将 `<TEMP>` 换成临时输出目录）：

```text
g++ tests/windows/proxy-recovery-test.cpp -o <TEMP>/proxy-recovery-test.exe -lwinhttp -liphlpapi -lws2_32 -lshell32 -luser32 -static -O2
```

运行测试应以 `PASS: real loopback probes and mocked WinINet recovery, no live proxy settings changed` 结束；模拟通知失败和认证失败的警告属于预期分支。
