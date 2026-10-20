#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <cstring>
#include <iostream>
#include <cstdlib>
struct MockOption { DWORD id; union { DWORD flags; LPWSTR text; FILETIME time; } value; };
struct MockList { DWORD size; LPWSTR connection; DWORD count, error; MockOption *options; };
DWORD mockFlags = 0;
std::wstring mockServer, mockPac;
int writes = 0, notifications = 0, reads = 0;
int changeOnRead = 0;
bool failRead = false, failWrite = false, failNotify = false, changeDuringWrite = false, failUi = false;
void Assert(bool ok, const char *name) { if (!ok) { std::cerr << "FAIL: " << name << std::endl; std::exit(1); } }
LPWSTR Copy(const std::wstring &s) {
    auto p = static_cast<LPWSTR>(GlobalAlloc(GMEM_FIXED, (s.size()+1)*sizeof(wchar_t)));
    Assert(p != NULL, "allocate mock string");
    std::memcpy(p, s.c_str(), (s.size()+1)*sizeof(wchar_t));
    return p;
}
BOOL WINAPI MockQuery(void *, DWORD option, void *data, DWORD *) {
    ++reads;
    if (failRead) return FALSE;
    Assert(option == 75, "query option");
    auto list = static_cast<MockList *>(data);
    Assert(list->connection == NULL && list->count == 3, "current-user LAN query");
    Assert((list->options[0].id == 10 || list->options[0].id == 1) && list->options[1].id == 2 && list->options[2].id == 4, "query keys");
    if (failUi && list->options[0].id == 10) return FALSE;
    if (reads == changeOnRead) mockServer = L"remote.example.com:8080";
    list->options[0].value.flags = mockFlags;
    list->options[1].value.text = Copy(mockServer);
    list->options[2].value.text = Copy(mockPac);
    return TRUE;
}
BOOL WINAPI MockSet(void *, DWORD option, void *data, DWORD) {
    if (option == 75) {
        auto list = static_cast<MockList *>(data);
        Assert(list->connection == NULL && list->count == 1 && list->options[0].id == 1, "only LAN enable flags written");
        if (failWrite) return FALSE;
        ++writes;
        mockFlags = list->options[0].value.flags;
        if (changeDuringWrite) mockServer = L"remote.example.com:8080";
        return TRUE;
    }
    Assert(option == 39 || option == 37, "notification option");
    ++notifications;
    return !failNotify;
}
HMODULE WINAPI MockLoad(LPCWSTR) { return reinterpret_cast<HMODULE>(1); }
BOOL WINAPI MockFree(HMODULE) { return TRUE; }
FARPROC WINAPI MockAddress(HMODULE, LPCSTR name) {
    FARPROC out = NULL;
    if (std::strcmp(name, "InternetQueryOptionW") == 0) {
        auto p = &MockQuery; static_assert(sizeof(p)==sizeof(out), "pointer size"); std::memcpy(&out, &p, sizeof(out));
    } else if (std::strcmp(name, "InternetSetOptionW") == 0) {
        auto p = &MockSet; static_assert(sizeof(p)==sizeof(out), "pointer size"); std::memcpy(&out, &p, sizeof(out));
    }
    return out;
}
#define LoadLibraryW MockLoad
#define FreeLibrary MockFree
#define GetProcAddress MockAddress
#define main ApplicationMain
#include "../../src/windows/campus-link-repair.cpp"
#undef main
#undef GetProcAddress
#undef FreeLibrary
#undef LoadLibraryW

int main() {
    WSADATA data = {};
    Assert(WSAStartup(MAKEWORD(2,2), &data)==0, "winsock startup");
    SOCKET local = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Assert(local != INVALID_SOCKET, "local socket");
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Assert(bind(local, reinterpret_cast<sockaddr *>(&address), sizeof(address))==0, "bind loopback");
    int size = sizeof(address);
    Assert(getsockname(local, reinterpret_cast<sockaddr *>(&address), &size)==0, "port allocation");
    unsigned short port = ntohs(address.sin_port);
    using namespace proxy_recovery;
    Assert(ProbeAddress("127.0.0.1", port)==PortState::Refused, "real TCP refusal");
    std::wstring configuredServer = L"http=127.0.0.1:"+std::to_wstring(port)+L";https=127.0.0.1:"+std::to_wstring(port);
    auto reset = [&]() {
        mockFlags = DIRECT|MANUAL|PAC|8;
        mockServer = configuredServer;
        mockPac = L"https://remote.example.com/proxy.pac";
        writes = notifications = reads = 0;
        changeOnRead = 0;
        failRead = failWrite = failNotify = changeDuringWrite = failUi = false;
    };
    InternetOptions api;
    Snapshot expected;
    reset(); failUi = true;
    Assert(api.Read(expected) && reads == 2, "legacy flags query fallback");
    reset();
    Assert(api.Read(expected), "mock snapshot");
    Assert(api.Disable(expected, MANUAL), "disable dead manual setting");
    Assert(writes==1 && notifications==2 && mockFlags==(DIRECT|PAC|8), "flags and notifications");
    Assert(mockServer==configuredServer && mockPac==L"https://remote.example.com/proxy.pac", "endpoints preserved");
    reset(); mockServer = L"remote.example.com:8080";
    mockPac = L"http://127.0.0.1:"+std::to_wstring(port)+L"/proxy.pac";
    api.Read(expected);
    Assert(api.Disable(expected, PAC) && mockFlags==(DIRECT|MANUAL|8), "dead PAC disabled while remote manual proxy preserved");
    reset(); api.Read(expected); mockServer = L"127.0.0.1:1";
    Assert(!api.Disable(expected, MANUAL) && writes==0, "changed settings preserved");
    reset(); api.Read(expected); failWrite = true;
    Assert(!api.Disable(expected, MANUAL) && writes==0 && notifications==0, "write failure preserved");
    reset(); api.Read(expected); failRead = true;
    Assert(!api.Disable(expected, MANUAL) && writes==0, "read failure preserved");
    reset(); api.Read(expected); changeDuringWrite = true;
    Assert(!api.Disable(expected, MANUAL), "concurrent change detected after write");
    reset(); api.Read(expected); changeOnRead = 3;
    Assert(!api.Disable(expected, MANUAL) && writes==0, "settings changed during TCP probe preserved");
    reset(); api.Read(expected); failNotify = true;
    Assert(api.Disable(expected, MANUAL) && writes==1, "notification failure distinguished from settings verification");
    reset(); api.Read(expected);
    Assert(listen(local, 4)==0, "listen loopback");
    Endpoint localhost; localhost.host="localhost"; localhost.port=port;
    Assert(Probe(localhost)==PortState::Listening, "live localhost IPv4 survives IPv6 refusal");
    Assert(!api.Disable(expected, MANUAL) && writes==0, "live proxy never disabled");
    closesocket(local);
    reset(); api.Read(expected);
    g_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    Assert(g_hExitEvent != NULL, "exit event");
    Observation state; state.previous = expected; state.mask = MANUAL; state.since = GetTickCount64()-GRACE_MS-1;
    Check("detect", true, state);
    Assert(writes==0, "detect mode read-only");
    Check("auto", false, state);
    Assert(writes==0, "failed authentication blocks repair");
    int readCount = reads;
    Check("off", true, state);
    Assert(reads==readCount && writes==0, "off mode skips diagnostics");
    Check("auto", true, state);
    Assert(writes==1 && mockFlags==(DIRECT|PAC|8), "authenticated auto repair");
    CloseHandle(g_hExitEvent); g_hExitEvent = NULL;
    WSACleanup();
    std::cout << "PASS: real loopback probes and mocked WinINet recovery, no live proxy settings changed" << std::endl;
}
