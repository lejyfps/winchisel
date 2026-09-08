#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
#pragma pack(push, 1)
struct Footer { char magic[8]; std::uint64_t table_offset; std::uint64_t table_size; std::uint32_t mode; };
#pragma pack(pop)
struct Entry { std::wstring path; std::uint64_t offset; std::uint64_t size; };

constexpr wchar_t k_uninstall_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Winchisel";

int failure(wchar_t const* message) { MessageBoxW(nullptr, message, L"Winchisel Setup", MB_OK | MB_ICONERROR); return 1; }
std::wstring utf8(std::string const& value) { const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring result(size,L'\0');if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result; }
bool safe(std::wstring const& value) { const std::filesystem::path path(value);return !path.empty()&&!path.is_absolute()&&!value.contains(L"..")&&value.find(L':')==std::wstring::npos; }
std::filesystem::path known(REFKNOWNFOLDERID id) { PWSTR raw{};if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&raw)))return {};std::filesystem::path result=raw;CoTaskMemFree(raw);return result; }
std::filesystem::path module_path() { std::array<wchar_t,32768> raw{};const auto size=GetModuleFileNameW(nullptr,raw.data(),static_cast<DWORD>(raw.size()));return !size||size==raw.size()?std::filesystem::path{}:std::filesystem::path(std::wstring(raw.data(),size)); }

bool unpack(std::filesystem::path const& self,std::filesystem::path const& target) {
    std::ifstream input(self,std::ios::binary|std::ios::ate);const auto end=input.tellg();if(end<static_cast<std::streamoff>(sizeof(Footer)))return false;
    Footer footer{};input.seekg(end-static_cast<std::streamoff>(sizeof(Footer)));input.read(reinterpret_cast<char*>(&footer),sizeof(footer));if(!input||std::string_view(footer.magic,8)!="WCHBNDL1"||footer.table_offset+footer.table_size>static_cast<std::uint64_t>(end))return false;
    input.seekg(static_cast<std::streamoff>(footer.table_offset));std::uint32_t count{};input.read(reinterpret_cast<char*>(&count),sizeof(count));if(!input||count>10000)return false;std::vector<Entry> entries;const auto payload_end=static_cast<std::uint64_t>(end)-sizeof(Footer);const auto payload_begin=footer.table_offset+footer.table_size;
    for(std::uint32_t i{};i<count;++i){std::uint32_t length{};input.read(reinterpret_cast<char*>(&length),sizeof(length));if(!input||!length||length>32768)return false;std::string raw(length,'\0');Entry entry{};input.read(raw.data(),length);input.read(reinterpret_cast<char*>(&entry.offset),sizeof(entry.offset));input.read(reinterpret_cast<char*>(&entry.size),sizeof(entry.size));entry.path=utf8(raw);if(!input||!safe(entry.path)||entry.offset<payload_begin||entry.offset>payload_end||entry.size>payload_end-entry.offset)return false;entries.push_back(std::move(entry));}
    std::error_code error;std::filesystem::create_directories(target,error);if(error)return false;std::array<char,65536> buffer{};
    for(auto const& entry:entries){const auto file=target/entry.path;std::filesystem::create_directories(file.parent_path(),error);if(error)return false;std::ofstream output(file,std::ios::binary|std::ios::trunc);input.clear();input.seekg(static_cast<std::streamoff>(entry.offset));std::uint64_t remaining=entry.size;while(remaining){const auto chunk=static_cast<std::streamsize>(std::min<std::uint64_t>(remaining,buffer.size()));input.read(buffer.data(),chunk);if(input.gcount()!=chunk)return false;output.write(buffer.data(),chunk);remaining-=chunk;}if(!output)return false;}return true;
}

bool start(std::filesystem::path const& executable) { std::wstring command=L"\""+executable.wstring()+L"\"";STARTUPINFOW info{.cb=sizeof(info)};PROCESS_INFORMATION process{};if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),&info,&process))return false;CloseHandle(process.hThread);CloseHandle(process.hProcess);return true; }
bool shortcut(std::filesystem::path const& link,std::filesystem::path const& executable) { IShellLinkW* shell{};if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shell))))return false;shell->SetPath(executable.c_str());shell->SetWorkingDirectory(executable.parent_path().c_str());shell->SetDescription(L"Winchisel");IPersistFile* file{};const auto queried=shell->QueryInterface(IID_PPV_ARGS(&file));const auto saved=SUCCEEDED(queried)&&SUCCEEDED(file->Save(link.c_str(),TRUE));if(file)file->Release();shell->Release();return saved; }
void set_string(HKEY key,wchar_t const* name,std::wstring const& value){RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}
void register_install(std::filesystem::path const& target,std::wstring const& version){HKEY key{};if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,k_uninstall_key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;set_string(key,L"DisplayName",L"Winchisel");set_string(key,L"DisplayVersion",version);set_string(key,L"Publisher",L"lejyfps");set_string(key,L"InstallLocation",target.wstring());set_string(key,L"DisplayIcon",(target/L"Winchisel.exe").wstring());set_string(key,L"UninstallString",L"\""+(target/L"Winchisel.Uninstall.exe").wstring()+L"\" --uninstall");DWORD one=1;RegSetValueExW(key,L"NoModify",0,REG_DWORD,reinterpret_cast<BYTE*>(&one),sizeof(one));RegSetValueExW(key,L"NoRepair",0,REG_DWORD,reinterpret_cast<BYTE*>(&one),sizeof(one));RegCloseKey(key);}
std::wstring version_of(std::filesystem::path const& target){std::wifstream input(target/L"version.txt");std::wstring version;std::getline(input,version);return version.empty()?L"unknown":version;}
void remove_shortcuts(){std::error_code error;std::filesystem::remove(known(FOLDERID_CommonPrograms)/L"Winchisel.lnk",error);std::filesystem::remove(known(FOLDERID_PublicDesktop)/L"Winchisel.lnk",error);}

int uninstall(std::filesystem::path const& self){if(MessageBoxW(nullptr,L"Remove Winchisel and all installed application files?",L"Uninstall Winchisel",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;remove_shortcuts();RegDeleteTreeW(HKEY_LOCAL_MACHINE,k_uninstall_key);const auto target=self.parent_path();std::error_code error;for(std::filesystem::directory_iterator it(target,error),end;!error&&it!=end;it.increment(error)){if(_wcsicmp(it->path().c_str(),self.c_str())!=0)std::filesystem::remove_all(it->path(),error);}MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);MoveFileExW(target.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);MessageBoxW(nullptr,L"Winchisel was uninstalled.",L"Winchisel",MB_OK|MB_ICONINFORMATION);return 0;}

int install(std::filesystem::path const& self){const auto answer=MessageBoxW(nullptr,L"Install Winchisel for all users?\n\nYes: also create a desktop shortcut\nNo: install without a desktop shortcut",L"Winchisel Setup",MB_YESNOCANCEL|MB_ICONQUESTION);if(answer==IDCANCEL)return 0;const auto program_files=known(FOLDERID_ProgramFiles);if(program_files.empty())return failure(L"Program Files is unavailable.");const auto target=program_files/L"Winchisel";if(!unpack(self,target))return failure(L"Installation failed. Close Winchisel and check available disk space.");const auto executable=target/L"Winchisel.exe";if(!CopyFileW(self.c_str(),(target/L"Winchisel.Uninstall.exe").c_str(),FALSE))return failure(L"The uninstaller could not be created.");if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return failure(L"Windows shell initialization failed.");const auto menu=known(FOLDERID_CommonPrograms)/L"Winchisel.lnk";const bool menu_ok=shortcut(menu,executable);const bool desktop_ok=answer!=IDYES||shortcut(known(FOLDERID_PublicDesktop)/L"Winchisel.lnk",executable);CoUninitialize();if(!menu_ok||!desktop_ok)return failure(L"Winchisel was installed, but a shortcut could not be created.");register_install(target,version_of(target));if(MessageBoxW(nullptr,L"Winchisel was installed successfully. Start it now?",L"Winchisel Setup",MB_YESNO|MB_ICONINFORMATION)==IDYES&&!start(executable))return failure(L"Winchisel was installed, but could not be started.");return 0;}

int portable(std::filesystem::path const& self,Footer const& footer){const auto target=known(FOLDERID_LocalAppData)/L"Winchisel"/L"portable"/std::to_wstring(footer.table_offset);if(!unpack(self,target))return failure(L"The portable application could not be extracted.");return start(target/L"Winchisel.exe")?0:failure(L"The portable application could not be started.");}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){const auto self=module_path();if(self.empty())return failure(L"The package path could not be determined.");if(std::wstring_view(GetCommandLineW()).find(L"--uninstall")!=std::wstring_view::npos)return uninstall(self);std::ifstream input(self,std::ios::binary|std::ios::ate);Footer footer{};input.seekg(-static_cast<std::streamoff>(sizeof(footer)),std::ios::end);input.read(reinterpret_cast<char*>(&footer),sizeof(footer));if(!input||std::string_view(footer.magic,8)!="WCHBNDL1")return failure(L"The embedded package is invalid.");return footer.mode==2?install(self):portable(self,footer);}
