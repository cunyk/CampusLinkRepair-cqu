#ifndef CAMPUS_LINK_REPAIR_PROXY_RECOVERY_H
#define CAMPUS_LINK_REPAIR_PROXY_RECOVERY_H

// Windows 当前用户的 LAN 代理恢复；不依赖客户端名称，不修改 DNS、路由或网卡。
// WinHTTP 与 WinINet 头文件有重名类型，以下使用公开的 WinINet ABI 动态调用，
// 保持原有单文件编译命令。结构布局见 Microsoft INTERNET_PER_CONN_OPTIONW 文档。
namespace proxy_recovery
{
const DWORD DIRECT = 1, MANUAL = 2, PAC = 4;
const ULONGLONG GRACE_MS = 30000;

struct Endpoint
{
    std::string host;
    unsigned short port = 0;
};

std::string Lower(std::string value)
{
    for (char &c : value)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return value;
}

// 仅接受 loopback 字面量和 localhost，绝不解析任意代理域名。
bool ParseEndpoint(std::string value, bool pacUrl, Endpoint &out)
{
    value = Trim(value);
    size_t schemeEnd = value.find("://");
    unsigned short defaultPort = 0;
    if (schemeEnd != std::string::npos)
    {
        std::string scheme = Lower(value.substr(0, schemeEnd));
        if (scheme != "http" && scheme != "https")
            return false;
        defaultPort = scheme == "https" ? 443 : 80;
        value.erase(0, schemeEnd + 3);
    }
    else if (pacUrl)
        return false;

    size_t path = value.find_first_of("/?#");
    if (path != std::string::npos)
    {
        if (!pacUrl)
            return false;
        value.erase(path);
    }
    if (value.empty() || value.find('@') != std::string::npos)
        return false;

    std::string host, port;
    if (value[0] == '[')
    {
        size_t end = value.find(']');
        if (end == std::string::npos)
            return false;
        host = value.substr(1, end - 1);
        if (end + 1 < value.size())
        {
            if (value[end + 1] != ':')
                return false;
            port = value.substr(end + 2);
            if (port.empty())
                return false;
        }
    }
    else
    {
        size_t colon = value.find(':');
        host = value.substr(0, colon);
        if (colon != std::string::npos)
        {
            port = value.substr(colon + 1);
            if (port.empty())
                return false;
        }
    }
    host = Lower(host);
    IN_ADDR ipv4 = {};
    IN6_ADDR ipv6 = {};
    bool local = host == "localhost" ||
                 (InetPtonA(AF_INET, host.c_str(), &ipv4) == 1 &&
                  (ntohl(ipv4.s_addr) >> 24) == 127) ||
                 (InetPtonA(AF_INET6, host.c_str(), &ipv6) == 1 && IN6_IS_ADDR_LOOPBACK(&ipv6));
    if (!local)
        return false;
    unsigned int number = defaultPort;
    if (!port.empty())
    {
        number = 0;
        for (char c : port)
        {
            if (c < '0' || c > '9' || port.size() > 5)
                return false;
            number = number * 10 + static_cast<unsigned int>(c - '0');
        }
    }
    if (number == 0 || number > 65535)
        return false;
    out.host = host;
    out.port = static_cast<unsigned short>(number);
    return true;
}

enum class PortState { Listening, Refused, Unknown };

PortState ProbeAddress(const std::string &host, unsigned short port)
{
    sockaddr_storage address = {};
    IN_ADDR ipv4 = {};
    int family = InetPtonA(AF_INET, host.c_str(), &ipv4) == 1 ? AF_INET : AF_INET6;
    int length;
    if (family == AF_INET)
    {
        auto *addr = reinterpret_cast<sockaddr_in *>(&address);
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        addr->sin_addr = ipv4;
        length = sizeof(*addr);
    }
    else
    {
        auto *addr = reinterpret_cast<sockaddr_in6 *>(&address);
        addr->sin6_family = AF_INET6;
        addr->sin6_port = htons(port);
        if (InetPtonA(AF_INET6, host.c_str(), &addr->sin6_addr) != 1)
            return PortState::Unknown;
        length = sizeof(*addr);
    }
    SOCKET socketHandle = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET)
        return PortState::Unknown;
    u_long nonblocking = 1;
    PortState result = PortState::Unknown;
    if (ioctlsocket(socketHandle, FIONBIO, &nonblocking) == 0)
    {
        int rc = connect(socketHandle, reinterpret_cast<sockaddr *>(&address), length);
        int error = rc == 0 ? 0 : WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
        {
            fd_set writeSet, errorSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(socketHandle, &writeSet);
            FD_SET(socketHandle, &errorSet);
            // Windows loopback 的拒绝连接可能延后约 2 秒返回，不能按短超时判定。
            timeval timeout = {2, 500000};
            if (select(0, NULL, &writeSet, &errorSet, &timeout) > 0)
            {
                int size = sizeof(error);
                if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) != 0)
                    error = WSAGetLastError();
            }
        }
        if (error == 0)
            result = PortState::Listening;
        else if (error == WSAECONNREFUSED)
            result = PortState::Refused;
    }
    closesocket(socketHandle);
    return result;
}

PortState Probe(const Endpoint &endpoint)
{
    if (endpoint.host != "localhost")
        return ProbeAddress(endpoint.host, endpoint.port);
    PortState ipv4 = ProbeAddress("127.0.0.1", endpoint.port);
    if (ipv4 == PortState::Listening)
        return ipv4;
    PortState ipv6 = ProbeAddress("::1", endpoint.port);
    if (ipv4 == PortState::Listening || ipv6 == PortState::Listening)
        return PortState::Listening;
    return ipv4 == PortState::Refused && ipv6 == PortState::Refused ? PortState::Refused : PortState::Unknown;
}

struct Snapshot
{
    DWORD flags = 0;
    std::string server, pac;
    bool operator==(const Snapshot &other) const
    {
        return flags == other.flags && server == other.server && pac == other.pac;
    }
};

// 混合远程代理、仍在监听的本地代理、超时、未知格式均保留。
template<class ProbeFn>
DWORD DeadSettings(const Snapshot &settings, ProbeFn probe)
{
    // http/https 常复用同一端口，只探测一次；异常复杂的配置限制总探测时间。
    std::map<std::pair<std::string, unsigned short>, PortState> cache;
    ULONGLONG started = GetTickCount64();
    auto cachedProbe = [&](const Endpoint &endpoint)
    {
        auto key = std::make_pair(endpoint.host, endpoint.port);
        auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
        PortState result = GetTickCount64() - started >= 10000 ? PortState::Unknown : probe(endpoint);
        cache[key] = result;
        return result;
    };
    DWORD mask = 0;
    if ((settings.flags & MANUAL) && !settings.server.empty())
    {
        std::istringstream entries(settings.server);
        std::string entry;
        bool allDead = true;
        unsigned int count = 0;
        while (std::getline(entries, entry, ';'))
        {
            if (++count > 8 || Trim(entry).empty())
            {
                allDead = false;
                break;
            }
            size_t equals = entry.find('=');
            if (equals != std::string::npos)
            {
                std::string protocol = Lower(Trim(entry.substr(0, equals)));
                if (protocol != "http" && protocol != "https" && protocol != "ftp" && protocol != "socks")
                {
                    allDead = false;
                    break;
                }
                entry.erase(0, equals + 1);
            }
            Endpoint endpoint;
            if (!ParseEndpoint(entry, false, endpoint) || cachedProbe(endpoint) != PortState::Refused)
            {
                allDead = false;
                break;
            }
        }
        if (allDead && count > 0)
            mask |= MANUAL;
    }
    Endpoint pac;
    if ((settings.flags & PAC) && ParseEndpoint(settings.pac, true, pac) && cachedProbe(pac) == PortState::Refused)
        mask |= PAC;
    return mask;
}

struct Observation
{
    Snapshot previous;
    DWORD mask = 0;
    ULONGLONG since = 0;
    bool reportedFailure = false;
    bool Ready(const Snapshot &current, DWORD dead, bool authenticated, ULONGLONG now)
    {
        if (!(current == previous) || dead != mask)
        {
            previous = current;
            mask = dead;
            since = now;
        }
        return dead != 0 && authenticated && now - since >= GRACE_MS;
    }
};

class InternetOptions
{
    struct Option
    {
        DWORD id;
        union { DWORD flags; LPWSTR text; FILETIME time; } value;
    };
    struct OptionList
    {
        DWORD size;
        LPWSTR connection;
        DWORD count, error;
        Option *options;
    };
    typedef BOOL (WINAPI *QueryFn)(void *, DWORD, void *, DWORD *);
    typedef BOOL (WINAPI *SetFn)(void *, DWORD, void *, DWORD);
    HMODULE module = NULL;
    QueryFn query = NULL;
    SetFn set = NULL;
public:
    InternetOptions()
    {
        wchar_t directory[MAX_PATH];
        UINT size = GetSystemDirectoryW(directory, MAX_PATH);
        if (size == 0 || size >= MAX_PATH)
            return;
        module = LoadLibraryW((std::wstring(directory) + L"\\wininet.dll").c_str());
        if (module)
        {
            FARPROC queryAddress = GetProcAddress(module, "InternetQueryOptionW");
            FARPROC setAddress = GetProcAddress(module, "InternetSetOptionW");
            static_assert(sizeof(query) == sizeof(queryAddress), "Windows function pointer size");
            static_assert(sizeof(set) == sizeof(setAddress), "Windows function pointer size");
            std::memcpy(&query, &queryAddress, sizeof(query));
            std::memcpy(&set, &setAddress, sizeof(set));
        }
    }
    ~InternetOptions() { if (module) FreeLibrary(module); }
    InternetOptions(const InternetOptions &) = delete;
    InternetOptions &operator=(const InternetOptions &) = delete;
    bool Read(Snapshot &snapshot)
    {
        if (!query)
            return false;
        Option options[3] = {};
        options[0].id = 10; // INTERNET_PER_CONN_FLAGS_UI (Windows 7+)
        options[1].id = 2; // INTERNET_PER_CONN_PROXY_SERVER
        options[2].id = 4; // INTERNET_PER_CONN_AUTOCONFIG_URL
        OptionList list = {sizeof(OptionList), NULL, 3, 0, options};
        DWORD size = sizeof(list);
        bool ok = query(NULL, 75 /* INTERNET_OPTION_PER_CONNECTION_OPTION */, &list, &size) != FALSE;
        if (!ok)
        {
            if (options[1].value.text) GlobalFree(options[1].value.text);
            if (options[2].value.text) GlobalFree(options[2].value.text);
            options[1].value.text = options[2].value.text = NULL;
            options[0].id = 1; // 旧版 WinINet 回退 INTERNET_PER_CONN_FLAGS。
            list.error = 0;
            size = sizeof(list);
            ok = query(NULL, 75, &list, &size) != FALSE;
        }
        if (ok)
        {
            snapshot.flags = options[0].value.flags;
            snapshot.server = options[1].value.text ? ToString(options[1].value.text) : "";
            snapshot.pac = options[2].value.text ? ToString(options[2].value.text) : "";
        }
        if (options[1].value.text) GlobalFree(options[1].value.text);
        if (options[2].value.text) GlobalFree(options[2].value.text);
        return ok;
    }
    bool Disable(const Snapshot &expected, DWORD dead)
    {
        // 防止等待期间客户端已重新启动或用户已改设置。
        Snapshot current;
        if (dead == 0 || (dead & ~(MANUAL | PAC)) || !set || !Read(current) ||
            !(current == expected) || DeadSettings(current, Probe) != dead)
            return false;
        // TCP 探测可能耗时数秒，探测结束后再次比对，缩小与客户端更新的竞态窗口。
        Snapshot latest;
        if (!Read(latest) || !(latest == current) ||
            (g_hExitEvent && WaitForSingleObject(g_hExitEvent, 0) != WAIT_TIMEOUT))
            return false;
        Option option = {};
        option.id = 1;
        option.value.flags = (current.flags & ~dead) | DIRECT;
        OptionList list = {sizeof(OptionList), NULL, 1, 0, &option};
        if (!set(NULL, 75, &list, sizeof(list)))
            return false;
        // 只修改启用标志，保留服务器地址、PAC URL、绕过列表和自动发现设置。
        bool notified = set(NULL, 39 /* SETTINGS_CHANGED */, NULL, 0) != FALSE;
        notified = (set(NULL, 37 /* REFRESH */, NULL, 0) != FALSE) && notified;
        Snapshot updated;
        bool verified = Read(updated) && updated.flags == option.value.flags &&
                        updated.server == current.server && updated.pac == current.pac;
        if (!notified)
            std::cerr << "[代理恢复] 设置变更通知失败，浏览器可能需要重新启动。" << std::endl;
        return verified;
    }
};

void Check(const std::string &mode, bool authenticated, Observation &state)
{
    if (mode == "off" || WaitForSingleObject(g_hExitEvent, 0) != WAIT_TIMEOUT)
        return;
    static InternetOptions options;
    Snapshot snapshot;
    if (!options.Read(snapshot))
    {
        if (!state.reportedFailure)
            std::cerr << "[代理恢复] 无法读取当前用户代理设置，保持原设置。" << std::endl;
        state.reportedFailure = true;
        state.mask = 0; // 未能持续确认失效，下一次成功读取后重新计时。
        return;
    }
    DWORD dead = DeadSettings(snapshot, Probe);
    bool changed = !(snapshot == state.previous) || dead != state.mask;
    bool ready = state.Ready(snapshot, dead, authenticated, GetTickCount64());
    if (changed && dead)
        std::cout << "[代理恢复] 检测到已启用的本机代理/PAC 端口拒绝连接；"
                  << (mode == "auto" ? "持续观察至少 30 秒，门户直连认证成功后自动停用失效项。" : "当前仅诊断；PROXY_RECOVERY=auto 可启用恢复。") << std::endl;
    if (!authenticated && !state.reportedFailure)
        std::cerr << "[网络诊断] 门户直连认证未成功，暂不修改代理。若重开客户端再退出才能恢复，请检查其 TUN、DNS、路由或断网保护；程序不自动重置这些设置。" << std::endl;
    state.reportedFailure = !authenticated;
    if (mode == "auto" && ready && WaitForSingleObject(g_hExitEvent, 0) == WAIT_TIMEOUT)
    {
        if (options.Disable(snapshot, dead))
        {
            std::cout << "[代理恢复] 已停用失效的本机代理/PAC；地址保留，可在 Windows 代理设置中重新启用。" << std::endl;
            state.mask = 0;
        }
        else
        {
            std::cerr << "[代理恢复] 设置已变化或恢复未确认成功，稍后重新检查。" << std::endl;
            state.since = GetTickCount64();
        }
    }
}

bool Diagnose()
{
    InternetOptions options;
    Snapshot snapshot;
    if (!options.Read(snapshot))
    {
        std::cerr << "[网络诊断] 无法读取当前用户的 LAN 代理设置。" << std::endl;
        return false;
    }
    DWORD dead = DeadSettings(snapshot, Probe);
    std::cout << "[网络诊断] 手动代理=" << ((snapshot.flags & MANUAL) ? "启用" : "关闭")
              << "，PAC=" << ((snapshot.flags & PAC) ? "启用" : "关闭") << std::endl;
    if (dead)
        std::cout << "[网络诊断] 检测到本机代理/PAC 端口拒绝连接，可能是客户端退出后的残留。" << std::endl;
    else
        std::cout << "[网络诊断] 未确认失效的本机代理；远程代理、超时及未知格式均保留。" << std::endl;
    std::cout << "[网络诊断] 本次未修改设置；此检查不验证校园网连通性，也不覆盖 TUN、DNS、路由或客户端断网保护。" << std::endl;
    return true;
}

bool SelfTest()
{
    bool ok = true;
    auto check = [&ok](bool condition, const char *name)
    {
        if (!condition)
        {
            std::cerr << "self-test: FAIL proxy_" << name << std::endl;
            ok = false;
        }
    };
    Endpoint endpoint;
    check(ParseEndpoint("127.0.0.1:7890", false, endpoint) && endpoint.port == 7890, "ipv4");
    check(ParseEndpoint("127.2.3.4:1080", false, endpoint), "loopback_subnet");
    check(ParseEndpoint("[::1]:7890", false, endpoint), "ipv6");
    check(ParseEndpoint("LOCALHOST:8080", false, endpoint), "localhost");
    check(ParseEndpoint("http://localhost/proxy.pac", true, endpoint) && endpoint.port == 80, "pac_default_port");
    check(!ParseEndpoint("http://127.0.0.1.example.com:8080/a", true, endpoint), "fake_loopback");
    check(!ParseEndpoint("192.168.1.1:8080", false, endpoint), "remote");
    check(!ParseEndpoint("127.0.0.1:65536", false, endpoint), "port_overflow");
    check(!ParseEndpoint("127.0.0.1:0", false, endpoint), "port_zero");
    check(!ParseEndpoint("127.0.0.1:80x", false, endpoint), "port_suffix");
    check(!ParseEndpoint("http://localhost:/a", true, endpoint), "empty_port");
    check(!ParseEndpoint("http://user@localhost:80/a", true, endpoint), "userinfo");
    auto refused = [](const Endpoint &) { return PortState::Refused; };
    auto live = [](const Endpoint &) { return PortState::Listening; };
    auto unknown = [](const Endpoint &) { return PortState::Unknown; };
    Snapshot settings;
    settings.flags = MANUAL | PAC | DIRECT;
    settings.server = "http=127.0.0.1:7890;https=[::1]:7890";
    settings.pac = "http://localhost:7891/proxy.pac";
    check(DeadSettings(settings, refused) == (MANUAL | PAC), "all_dead");
    check(DeadSettings(settings, live) == 0, "live_preserved");
    check(DeadSettings(settings, unknown) == 0, "timeout_preserved");
    settings.server += ";socks=proxy.example.com:1080";
    check(DeadSettings(settings, refused) == PAC, "mixed_remote_preserved");
    settings.server = "unknown=127.0.0.1:8080";
    check(DeadSettings(settings, refused) == PAC, "unknown_protocol_preserved");
    settings.server = "http=127.0.0.1:8080;https=localhost:8081";
    auto partlyLive = [](const Endpoint &e) { return e.port == 8081 ? PortState::Listening : PortState::Refused; };
    check(DeadSettings(settings, partlyLive) == PAC, "partly_live_preserved");
    settings.pac = "https://proxy.example.com/proxy.pac";
    check(DeadSettings(settings, refused) == MANUAL, "remote_pac_preserved");
    settings.flags = DIRECT;
    check(DeadSettings(settings, refused) == 0, "disabled_preserved");
    Observation observation;
    check(!observation.Ready(settings, MANUAL, true, 100), "startup_grace");
    check(!observation.Ready(settings, MANUAL, false, 30100), "auth_failure_preserved");
    check(observation.Ready(settings, MANUAL, true, 30100), "ready");
    settings.server = "127.0.0.1:9999";
    check(!observation.Ready(settings, MANUAL, true, 30101), "config_change_restarts_grace");
    check(!observation.Ready(settings, 0, true, 70000), "recovered_preserved");
    check(!observation.Ready(settings, MANUAL, true, 70001), "reappearance_restarts_grace");
    return ok;
}
}
#endif
