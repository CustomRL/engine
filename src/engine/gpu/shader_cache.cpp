#include "engine/gpu/shader_cache.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace engine::gpu {

namespace {

// FNV-1a; each string with its terminator, so fields can't run into each other
uint64_t mix(uint64_t h, const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

uint64_t mix(uint64_t h, const char* s) {
    return s ? mix(h, s, std::strlen(s) + 1) : mix(h, "\xff", 1);
}

// The loaded compiler's link time and image size, so another d3dcompiler_47.dll compiles afresh
uint64_t compilerBuild() {
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(L"d3dcompiler_47.dll"));
    if (!base)
        return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return (uint64_t(nt->FileHeader.TimeDateStamp) << 32) | nt->OptionalHeader.SizeOfImage;
}

const std::wstring& cacheDir() {
    static const std::wstring dir = [] {
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring d(path, n && n < MAX_PATH ? n : 0);
        d = d.substr(0, d.find_last_of(L'\\') + 1) + L"shader_cache\\";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

bool read(const std::wstring& path, ID3DBlob** code) {
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    DWORD got = 0;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 26) && SUCCEEDED(D3DCreateBlob(SIZE_T(size.QuadPart), code));
    ok = ok && ReadFile(f, (*code)->GetBufferPointer(), DWORD(size.QuadPart), &got, nullptr) && got == DWORD(size.QuadPart);
    CloseHandle(f);
    if (!ok && *code)
        (*code)->Release(), *code = nullptr;
    return ok;
}

// Through a file of this process's own, renamed into place, so a reader sees a whole file or none
void write(const std::wstring& path, ID3DBlob* code) {
    const std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    const HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    const bool ok = WriteFile(f, code->GetBufferPointer(), DWORD(code->GetBufferSize()), &put, nullptr) && put == code->GetBufferSize();
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(temp.c_str());
}

}

HRESULT compileCached(const void* source, size_t size, const char* name, const D3D_SHADER_MACRO* defines, ID3DInclude* include,
                      const char* entry, const char* target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors) {
    if (include)
        return D3DCompile(source, size, name, defines, include, entry, target, flags1, flags2, code, errors);
    uint64_t h = mix(0xcbf29ce484222325ull, &size, sizeof size);
    h = mix(h, source, size);
    uint32_t count = 0;
    for (const D3D_SHADER_MACRO* d = defines; d && d->Name; d++, count++)
        h = mix(mix(h, d->Name), d->Definition);
    h = mix(h, &count, sizeof count);
    h = mix(mix(mix(h, name), entry), target);
    const uint64_t build = compilerBuild();
    const UINT flags[2] = {flags1, flags2};
    h = mix(mix(h, flags, sizeof flags), &build, sizeof build);
    wchar_t file[17];
    swprintf_s(file, L"%016llx", static_cast<unsigned long long>(h));
    const std::wstring path = cacheDir() + file + L".cso";
    if (errors)
        *errors = nullptr;
    if (read(path, code))
        return S_OK;
    const HRESULT hr = D3DCompile(source, size, name, defines, nullptr, entry, target, flags1, flags2, code, errors);
    if (SUCCEEDED(hr))
        write(path, *code);
    return hr;
}

}
