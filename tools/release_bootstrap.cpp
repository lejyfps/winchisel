#include <windows.h>
#include <shlobj.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef WINCHISEL_INSTALLER
#pragma comment(linker, "/manifestuac:\"level='requireAdministrator' uiAccess='false'\"")
#endif
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
#pragma pack(push, 1)
struct Footer { char magic[8]; std::uint64_t table_offset; std::uint64_t table_size; std::uint32_t mode; };
#pragma pack(pop)
struct Entry { std::wstring path; std::uint64_t offset; std::uint64_t size; };

std::wstring utf8(std::string const& value) { const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0); std::wstring result(size,L'\0'); if(size) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size); return result; }
bool safe(std::wstring const& value) { const std::filesystem::path path(value); return !path.empty() && !path.is_absolute() && !value.contains(L"..") && value.find(L':')==std::wstring::npos; }

bool unpack(std::filesystem::path const& self, std::filesystem::path const& target) {
    std::ifstream input(self, std::ios::binary | std::ios::ate); const auto end=input.tellg(); if(end<static_cast<std::streamoff>(sizeof(Footer))) return false;
    Footer footer{}; input.seekg(end-static_cast<std::streamoff>(sizeof(Footer))); input.read(reinterpret_cast<char*>(&footer),sizeof(footer)); if(std::string_view(footer.magic,8)!="WCHBNDL1" || footer.table_offset+footer.table_size>static_cast<std::uint64_t>(end)) return false;
    input.seekg(static_cast<std::streamoff>(footer.table_offset)); std::uint32_t count{}; input.read(reinterpret_cast<char*>(&count),sizeof(count)); if(!input||count>10000)return false; std::vector<Entry> entries;
    const auto payload_end = static_cast<std::uint64_t>(end) - sizeof(Footer);
    const auto payload_begin = footer.table_offset + footer.table_size;
    for(std::uint32_t i{};i<count;++i){std::uint32_t length{};input.read(reinterpret_cast<char*>(&length),sizeof(length));if(!input||length==0||length>32768)return false;std::string raw(length,'\0');Entry entry{};input.read(raw.data(),length);input.read(reinterpret_cast<char*>(&entry.offset),sizeof(entry.offset));input.read(reinterpret_cast<char*>(&entry.size),sizeof(entry.size));entry.path=utf8(raw);if(!input||!safe(entry.path)||entry.offset<payload_begin||entry.offset>payload_end||entry.size>payload_end-entry.offset)return false;entries.push_back(std::move(entry));}
    std::error_code error; std::filesystem::create_directories(target,error);if(error)return false; std::array<char,65536> buffer{};
    for(auto const& entry:entries){const auto file=target/entry.path;std::filesystem::create_directories(file.parent_path(),error);if(error)return false;std::ofstream output(file,std::ios::binary|std::ios::trunc);input.clear();input.seekg(static_cast<std::streamoff>(entry.offset));std::uint64_t remaining=entry.size;while(remaining){const auto chunk=static_cast<std::streamsize>(std::min<std::uint64_t>(remaining,buffer.size()));input.read(buffer.data(),chunk);if(input.gcount()!=chunk)return false;output.write(buffer.data(),chunk);remaining-=chunk;}if(!output)return false;}
    return true;
}

bool start(std::filesystem::path const& executable) { std::wstring command=L"\""+executable.wstring()+L"\"";STARTUPINFOW info{.cb=sizeof(info)};PROCESS_INFORMATION process{};if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),&info,&process))return false;CloseHandle(process.hThread);CloseHandle(process.hProcess);return true; }
int failure(wchar_t const* message) { MessageBoxW(nullptr, message, L"Winchisel", MB_OK | MB_ICONERROR); return 1; }
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){std::array<wchar_t,32768> raw{};const auto length=GetModuleFileNameW(nullptr,raw.data(),static_cast<DWORD>(raw.size()));if(!length||length==raw.size())return failure(L"The package path could not be determined.");const std::filesystem::path self(std::wstring(raw.data(),length));std::ifstream input(self,std::ios::binary|std::ios::ate);Footer footer{};input.seekg(-static_cast<std::streamoff>(sizeof(footer)),std::ios::end);input.read(reinterpret_cast<char*>(&footer),sizeof(footer));if(!input||std::string_view(footer.magic,8)!="WCHBNDL1")return failure(L"The embedded package is invalid.");PWSTR base{};const auto folder=footer.mode==2?FOLDERID_ProgramFiles:FOLDERID_LocalAppData;if(FAILED(SHGetKnownFolderPath(folder,0,nullptr,&base)))return failure(L"The destination folder is unavailable.");std::filesystem::path target=base;CoTaskMemFree(base);target/=L"Winchisel";target/=footer.mode==2?L"":L"portable";target/=footer.mode==2?L"":std::to_wstring(footer.table_offset);if(!unpack(self,target))return failure(L"Winchisel could not be extracted. Check folder permissions and available disk space.");return start(target/L"Winchisel.exe")?0:failure(L"Winchisel was installed, but could not be started.");}
