# 校园网连接修复助手（CampusLinkRepair）Linux 服务部署

本文说明如何将 Linux 版 campus-link-repair 配置为 systemd 服务。Linux 版程序从进程工作目录读取 `config.yaml`，因此 `WorkingDirectory` 必须指向同时包含 `campus-link-repair` 和 `config.yaml` 的目录。

## 前置条件

- 已获取 Linux 版 `campus-link-repair` 可执行文件。
- 已编辑 `config.yaml`，并确认 `STUDENT_ID`、`USER_PASSWORD` 正确。
- 系统已安装 libcurl 运行库。如 libcurl 缺失，请按发行版标准手动安装系统包。
- 方式一和方式二需要 sudo 权限；方式三不需要。

## 三种部署方式

| 方式 | 运行身份               | 托管           | sudo   | 无需登陆           |
| ---- | ---------------------- | -------------- | ------ | ------------------------------ |
| 一   | 新建低权限系统用户 | system manager | 需要   | 是                             |
| 二   | 普通用户     | system manager | 需要   | 是                             |
| 三   | 普通用户     | user manager   | 不需要 | 需要额外配置 `loginctl enable-linger` |

## service 文件

仓库里的 `campus-link-repair.service` 是 system 级模板，含两个占位符：

| 占位符            | 说明               | 取值                                                      |
| ----------------- | ------------------ | --------------------------------------------------------- |
| `<USERNAME>`    | 运行服务的用户     | 方式一填`campus-link-repair`；方式二填该用户；方式三删除本行 |
| `<PROGRAM_DIR>` | 程序目录的绝对路径 | 程序目录的绝对路径                             |

## 方式一：专用系统用户

程序目录以 `/opt/campus-link-repair` 为例，可换成任意路径。

```bash
sudo install -d -o root -g root -m 755 /opt/campus-link-repair
sudo cp campus-link-repair config.yaml /opt/campus-link-repair/
sudo useradd --system --home-dir /opt/campus-link-repair --shell /usr/bin/nologin campus-link-repair
sudo chown root:campus-link-repair /opt/campus-link-repair
sudo chmod 750 /opt/campus-link-repair
sudo chown root:root /opt/campus-link-repair/campus-link-repair
sudo chmod 755 /opt/campus-link-repair/campus-link-repair
sudo chown root:campus-link-repair /opt/campus-link-repair/config.yaml
sudo chmod 640 /opt/campus-link-repair/config.yaml
```

发行版没有 `/usr/bin/nologin` 时，用 `command -v nologin` 查实际路径，替换上一条命令中的 shell 路径。

替换占位符后安装 unit：

```bash
sudo cp campus-link-repair.service /etc/systemd/system/campus-link-repair.service
sudo chmod 644 /etc/systemd/system/campus-link-repair.service
sudo systemd-analyze verify /etc/systemd/system/campus-link-repair.service
sudo systemctl daemon-reload
sudo systemctl enable --now campus-link-repair
```

检查占位符是否替换完：

```bash
if grep -q '<.*>' /etc/systemd/system/campus-link-repair.service; then
  echo 'service 文件仍有未替换的占位符'
fi
```

检查该用户能否读取配置并执行程序：

```bash
sudo -u campus-link-repair test -r /opt/campus-link-repair/config.yaml
sudo -u campus-link-repair test -x /opt/campus-link-repair/campus-link-repair
```

## 方式二：已有用户（system 级 unit）

以某个已有的普通用户身份运行，unit 仍装在 `/etc/systemd/system`，所以需要 sudo。程序目录任意，例如 `/home/abc/campus-link-repair-linux` 或 `/srv/campus-link-repair`。

```ini
User=abc
WorkingDirectory=/home/abc/campus-link-repair-linux
ExecStart=/home/abc/campus-link-repair-linux/campus-link-repair
```

该用户需要能遍历目录、读取配置：

```bash
sudo chown abc:abc /home/abc/campus-link-repair-linux/config.yaml
sudo chmod 600 /home/abc/campus-link-repair-linux/config.yaml
sudo chmod 755 /home/abc/campus-link-repair-linux/campus-link-repair
sudo -u abc test -r /home/abc/campus-link-repair-linux/config.yaml
sudo -u abc test -x /home/abc/campus-link-repair-linux/campus-link-repair
```

安装与启停命令同方式一：unit 放 `/etc/systemd/system`，用 `sudo systemctl`。

不要把程序放在加密 home、网络挂载 home，或必须登录后才可访问的目录：服务在开机时以该用户身份启动，这些目录此时可能不可用。

## 方式三：当前用户（User 级 unit）

unit 放在 `~/.config/systemd/user/campus-link-repair.service`，由你自己的 user manager 托管，全程不需要 sudo，可选的 `loginctl enable-linger` 除外。

模板要改两处：把 `WantedBy=multi-user.target` 改成 `default.target`，删掉 `User=<USERNAME>` 行。

```bash
mkdir -p ~/.config/systemd/user
cp campus-link-repair.service ~/.config/systemd/user/campus-link-repair.service
systemctl --user daemon-reload
systemctl --user enable --now campus-link-repair
```

与 system 级的两点差异：

- `After=`/`Wants=network-online.target` 在 user manager 中没有对应 unit（`not-found`），依赖被静默忽略，user 实例也排不到系统网络就绪之后。所以服务启动后首轮检查可能失败一次，日志为 `error: failed to get local IPv4 address`，下一轮自愈。
- 未启用 linger 时服务随会话存在，注销或退出图形会话即停止。要在开机（未登录）时就运行、并在注销后保持，执行 `sudo loginctl enable-linger <USERNAME>`。

## 日常管理

方式一、方式二：

```bash
sudo systemctl status campus-link-repair --no-pager
sudo systemctl restart campus-link-repair
sudo journalctl -u campus-link-repair -n 100 --no-pager
sudo journalctl -u campus-link-repair -f
sudo systemctl stop campus-link-repair
sudo systemctl disable campus-link-repair
```

方式三加 `--user`，不加 sudo：

```bash
systemctl --user status campus-link-repair --no-pager
systemctl --user restart campus-link-repair
journalctl --user -u campus-link-repair -n 100 --no-pager
systemctl --user stop campus-link-repair
systemctl --user disable campus-link-repair
```

改 `config.yaml` 后重启服务即可。改 `.service` 后先 `daemon-reload`（system 级加 sudo，User 级加 `--user`），再重启。

日志进入 systemd journal，可能包含本机 IP 和门户返回片段，不要直接贴到公共渠道。`config.yaml` 含账号和密码，保持最小可读权限。

## 配置项说明

`config.yaml` 必须与 `campus-link-repair` 位于同一工作目录。

- `STUDENT_ID`：学号。
- `USER_PASSWORD`：校园网密码。
- `SERVER_IP`：认证服务器 IP。填写后跳过 `login.cqu.edu.cn` 的 DNS 解析，但请求仍发送 `Host: login.cqu.edu.cn`。IPv6 地址直接填写，不带方括号。
- `CA_BUNDLE`：CA 证书文件路径（可选）。门户证书不在系统 CA 中时使用；文件不可读时以退出码 `78` 失败。
- `LOGIN_IP`：提交给认证服务器的客户端 IPv4。路由器/NAT 代登录场景可能需要；普通主机通常留空。
- `CHECK_INTERVAL`：检查间隔（秒），有效范围 5-3600，默认 20；非法或超范围以退出码 `78` 失败。
- `TIMEOUT`：libcurl 请求超时（秒），有效范围 1-300，默认 5；非法或超范围以退出码 `78` 失败。

配置校验：`config.yaml` 由程序内建的简单校验读取。以下任一情况都会打印错误（含行号与原因）并以退出码 `78` 失败：文件缺失、非法行、空键、未知键、重复键、账号或密码为空、未替换的模板占位符（`xxxxxxxx`/`xxxxxx`）、`LOGIN_IP` 非合法 IPv4、`SERVER_IP` 非合法 IP、`CA_BUNDLE` 指向的文件不可读、数值超范围。

## 故障排查

### 服务无法启动

方式一、方式二：

```bash
sudo systemctl status campus-link-repair --no-pager
sudo journalctl -u campus-link-repair -n 100 --no-pager
sudo systemd-analyze verify /etc/systemd/system/campus-link-repair.service
```

方式三：

```bash
systemctl --user status campus-link-repair --no-pager
journalctl --user -u campus-link-repair -n 100 --no-pager
```

重点检查：

- unit 文件中是否还有 `<USERNAME>` 或 `<PROGRAM_DIR>`：方式一、方式二看 `/etc/systemd/system/campus-link-repair.service`，方式三看 `~/.config/systemd/user/campus-link-repair.service`。
- `WorkingDirectory` 是否为绝对路径，并指向 `config.yaml` 所在目录。
- `campus-link-repair` 是否存在且可执行。
- 运行服务的用户是否能读取 `config.yaml`；方式三就是你自己。

### 配置错误后不自动重启

配置校验失败时程序返回退出码 `78`（检查项见「配置项说明」），service 因 `RestartPreventExitStatus=78` 不再反复重启。错误原因含行号，用 `journalctl -u campus-link-repair` 查看，方式三加 `--user`。修好配置后执行：

```bash
sudo systemctl restart campus-link-repair
```

方式三：

```bash
systemctl --user restart campus-link-repair
```

### 权限不足

方式一：

```bash
sudo namei -l /opt/campus-link-repair/config.yaml
sudo -u campus-link-repair test -r /opt/campus-link-repair/config.yaml
sudo -u campus-link-repair test -x /opt/campus-link-repair/campus-link-repair
```

方式二、方式三把命令里的用户和路径换成实际值。方式三以你本人身份运行，直接检查：

```bash
namei -l <程序目录>/config.yaml
test -r <程序目录>/config.yaml
test -x <程序目录>/campus-link-repair
```

### 缺少 libcurl

如果日志或 `systemctl status` 显示动态库加载失败，检查二进制依赖（路径换成实际程序路径）：

```bash
ldd /opt/campus-link-repair/campus-link-repair
```

缺少系统库时，请按发行版标准安装 libcurl 运行库。Arch Linux 上由 `curl` 包提供。

### 登录失败

```bash
sudo journalctl -u campus-link-repair -n 100 --no-pager
getent hosts login.cqu.edu.cn
cd /opt/campus-link-repair
sudo -u campus-link-repair ./campus-link-repair
```

方式三把 `cd` 的目录和 `sudo -u` 去掉即可。
日志中的 `ip=...` 是程序选择并提交给门户的本机 IPv4，括号内标签表示该地址的来源：`manual` 为 `LOGIN_IP` 显式指定；`route` 为按到认证服务器的路由选定源地址；`route-v4-fallback` 为路由探测仅得到 IPv6 且同接口无可用 IPv4，上报 IPv4 退回启发式选定；`heuristic` 为按接口/网段启发式选定。多网卡、VPN、容器或路由器/NAT 环境下，如果该地址不是预期地址，可考虑在 `config.yaml` 中设置 `LOGIN_IP`。

如果日志显示证书校验失败（如 curl error 60 `certificate verify failed`），说明门户证书不在系统 CA 中，设置 `CA_BUNDLE` 指向对应的 CA 证书文件后重启服务。

### 启动初期日志

即使启动早于校园网门户可用，程序也会按 `CHECK_INTERVAL` 周期重试。启动初期偶发 `warning: DNS resolution of login.cqu.edu.cn failed; falling back to heuristic address`，或 libcurl 的域名解析失败消息（如 `Could not resolve host`），不一定代表配置错误。设置 `SERVER_IP` 后程序绕过 DNS 解析，不会出现上述消息。

## 卸载

方式一、方式二：

```bash
sudo systemctl disable --now campus-link-repair
sudo rm -f /etc/systemd/system/campus-link-repair.service
sudo systemctl daemon-reload
sudo systemctl reset-failed campus-link-repair
```

方式一删掉专用用户和程序目录前，确认不再需要这份配置：

```bash
sudo rm -rf /opt/campus-link-repair
sudo userdel campus-link-repair
```

方式三：

```bash
systemctl --user disable --now campus-link-repair
rm -f ~/.config/systemd/user/campus-link-repair.service
systemctl --user daemon-reload
systemctl --user reset-failed campus-link-repair
```
