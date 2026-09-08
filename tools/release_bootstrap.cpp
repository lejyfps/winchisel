#include <windows.h>
#include "portable_cache.hpp"
#include <commctrl.h>
#include <compressapi.h>
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
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "cabinet.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
#pragma pack(push, 1)
struct Footer { char magic[8]; std::uint64_t table_offset; std::uint64_t table_size; std::uint32_t mode; };
#pragma pack(pop)
struct Entry { std::wstring path; std::uint64_t offset; std::uint64_t size; std::uint64_t compressed_size; };

constexpr wchar_t k_uninstall_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Winchisel";

int task_dialog(wchar_t const* title,wchar_t const* heading,wchar_t const* text,TASKDIALOG_COMMON_BUTTON_FLAGS buttons,PCWSTR icon,TASKDIALOG_BUTTON const* custom=nullptr,UINT custom_count=0,TASKDIALOG_FLAGS flags=TDF_POSITION_RELATIVE_TO_WINDOW){TASKDIALOGCONFIG config{.cbSize=sizeof(config),.dwFlags=flags,.dwCommonButtons=buttons,.pszWindowTitle=title,.pszMainIcon=icon,.pszMainInstruction=heading,.pszContent=text,.cButtons=custom_count,.pButtons=custom,.nDefaultButton=custom_count?custom[0].nButtonID:0};int selected{};return SUCCEEDED(TaskDialogIndirect(&config,&selected,nullptr,nullptr))?selected:IDCANCEL;}
int failure(wchar_t const* message) { task_dialog(L"Winchisel Setup",L"Something went wrong",message,TDCBF_CLOSE_BUTTON,TD_ERROR_ICON);return 1; }
std::wstring utf8(std::string const& value) { const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring result(size,L'\0');if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result; }
using winchisel::release::safe;
std::filesystem::path known(REFKNOWNFOLDERID id) { PWSTR raw{};if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&raw)))return {};std::filesystem::path result=raw;CoTaskMemFree(raw);return result; }
std::filesystem::path module_path() { std::array<wchar_t,32768> raw{};const auto size=GetModuleFileNameW(nullptr,raw.data(),static_cast<DWORD>(raw.size()));return !size||size==raw.size()?std::filesystem::path{}:std::filesystem::path(std::wstring(raw.data(),size)); }

bool unpack(std::filesystem::path const& self,std::filesystem::path const& target,std::vector<std::wstring>* installed_paths=nullptr) {
    constexpr std::uint64_t k_max_file = 256ull * 1024 * 1024;
    constexpr std::uint64_t k_max_total = 1024ull * 1024 * 1024;
    std::ifstream input(self,std::ios::binary|std::ios::ate);const auto end=input.tellg();if(end<static_cast<std::streamoff>(sizeof(Footer)))return false;
    const auto file_end=static_cast<std::uint64_t>(end);
    Footer footer{};input.seekg(end-static_cast<std::streamoff>(sizeof(Footer)));input.read(reinterpret_cast<char*>(&footer),sizeof(footer));
    if(!input||std::string_view(footer.magic,8)!="WCHBNDL2"||footer.table_size>file_end||footer.table_offset>file_end-footer.table_size)return false;
    const auto table_end=footer.table_offset+footer.table_size;
    input.seekg(static_cast<std::streamoff>(footer.table_offset));std::uint32_t count{};input.read(reinterpret_cast<char*>(&count),sizeof(count));if(!input||count>10000||footer.table_size<4)return false;std::vector<Entry> entries;const auto payload_end=file_end-sizeof(Footer);const auto payload_begin=table_end;
    std::uint64_t table_used=4,total_raw{};
    for(std::uint32_t i{};i<count;++i){std::uint32_t length{};input.read(reinterpret_cast<char*>(&length),sizeof(length));if(!input||!length||length>32768)return false;table_used+=4ull+length+24ull;if(table_used>footer.table_size)return false;std::string raw(length,'\0');Entry entry{};input.read(raw.data(),length);input.read(reinterpret_cast<char*>(&entry.offset),sizeof(entry.offset));input.read(reinterpret_cast<char*>(&entry.size),sizeof(entry.size));input.read(reinterpret_cast<char*>(&entry.compressed_size),sizeof(entry.compressed_size));entry.path=utf8(raw);if(!input||!safe(entry.path)||entry.size>k_max_file||entry.compressed_size>k_max_file||total_raw>k_max_total-entry.size||entry.offset<payload_begin||entry.offset>payload_end||entry.compressed_size>payload_end-entry.offset)return false;total_raw+=entry.size;entries.push_back(std::move(entry));}
    if(table_used!=footer.table_size&&table_used>footer.table_size)return false;
    DECOMPRESSOR_HANDLE decompressor{};if(!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&decompressor))return false;std::error_code error;std::filesystem::create_directories(target,error);if(error){CloseDecompressor(decompressor);return false;}
    for(auto const& entry:entries){if(!winchisel::release::safe_destination(target,entry.path)){CloseDecompressor(decompressor);return false;}const auto file=target/entry.path;std::filesystem::create_directories(file.parent_path(),error);if(error){CloseDecompressor(decompressor);return false;}try{std::vector<std::byte> packed(static_cast<std::size_t>(entry.compressed_size)),raw(static_cast<std::size_t>(entry.size));input.clear();input.seekg(static_cast<std::streamoff>(entry.offset));input.read(reinterpret_cast<char*>(packed.data()),static_cast<std::streamsize>(packed.size()));SIZE_T written{};if(!input||!Decompress(decompressor,packed.data(),packed.size(),raw.data(),raw.size(),&written)||written!=raw.size()){CloseDecompressor(decompressor);return false;}std::ofstream output(file,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<char const*>(raw.data()),static_cast<std::streamsize>(raw.size()));if(!output){CloseDecompressor(decompressor);return false;}if(installed_paths)installed_paths->push_back(entry.path);}catch(...){CloseDecompressor(decompressor);return false;}}CloseDecompressor(decompressor);return true;
}

bool start(std::filesystem::path const& executable) { std::wstring command=L"\""+executable.wstring()+L"\"";STARTUPINFOW info{.cb=sizeof(info)};PROCESS_INFORMATION process{};if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),&info,&process))return false;CloseHandle(process.hThread);CloseHandle(process.hProcess);return true; }
bool shortcut(std::filesystem::path const& link,std::filesystem::path const& executable) { IShellLinkW* shell{};if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shell))))return false;shell->SetPath(executable.c_str());shell->SetWorkingDirectory(executable.parent_path().c_str());shell->SetDescription(L"Winchisel");IPersistFile* file{};const auto queried=shell->QueryInterface(IID_PPV_ARGS(&file));const auto saved=SUCCEEDED(queried)&&SUCCEEDED(file->Save(link.c_str(),TRUE));if(file)file->Release();shell->Release();return saved; }
void set_string(HKEY key,wchar_t const* name,std::wstring const& value){RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}
void register_install(std::filesystem::path const& target,std::wstring const& version){HKEY key{};if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,k_uninstall_key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;set_string(key,L"DisplayName",L"Winchisel");set_string(key,L"DisplayVersion",version);set_string(key,L"Publisher",L"lejyfps");set_string(key,L"InstallLocation",target.wstring());set_string(key,L"DisplayIcon",(target/L"Winchisel.exe").wstring());set_string(key,L"UninstallString",L"\""+(target/L"Winchisel.Uninstall.exe").wstring()+L"\" --uninstall");DWORD one=1;RegSetValueExW(key,L"NoModify",0,REG_DWORD,reinterpret_cast<BYTE*>(&one),sizeof(one));RegSetValueExW(key,L"NoRepair",0,REG_DWORD,reinterpret_cast<BYTE*>(&one),sizeof(one));RegCloseKey(key);}
std::wstring version_of(std::filesystem::path const& target){std::wifstream input(target/L"version.txt");std::wstring version;std::getline(input,version);return version.empty()?L"unknown":version;}
void remove_shortcuts(){std::error_code error;std::filesystem::remove(known(FOLDERID_CommonPrograms)/L"Winchisel.lnk",error);std::filesystem::remove(known(FOLDERID_PublicDesktop)/L"Winchisel.lnk",error);}

int uninstall(std::filesystem::path const& self){if(task_dialog(L"Uninstall Winchisel",L"Remove Winchisel?",L"This removes the application and its shortcuts. Your personal Windows settings are not changed.",TDCBF_YES_BUTTON|TDCBF_NO_BUTTON,TD_WARNING_ICON)!=IDYES)return 0;remove_shortcuts();RegDeleteTreeW(HKEY_LOCAL_MACHINE,k_uninstall_key);const auto target=self.parent_path();const auto manifest=target/L"install-manifest.txt";std::wifstream input(manifest);std::wstring relative;std::error_code error;while(std::getline(input,relative)){if(!safe(relative))continue;const auto candidate=(target/relative).lexically_normal();if(candidate.parent_path()!=target&&candidate.native().rfind(target.native()+L"\\",0)!=0)continue;std::filesystem::remove(candidate,error);error.clear();}std::filesystem::remove(manifest,error);std::vector<std::filesystem::path> directories;for(std::filesystem::recursive_directory_iterator it(target,std::filesystem::directory_options::skip_permission_denied,error),end;!error&&it!=end;it.increment(error))if(it->is_directory(error))directories.push_back(it->path());std::sort(directories.rbegin(),directories.rend());for(auto const& directory:directories){std::filesystem::remove(directory,error);error.clear();}MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);std::filesystem::remove(target,error);task_dialog(L"Winchisel",L"Uninstall complete",L"Winchisel product files were removed. Any unknown files in the install folder were preserved.",TDCBF_CLOSE_BUTTON,TD_INFORMATION_ICON);return 0;}

int install(std::filesystem::path const& self){constexpr TASKDIALOG_BUTTON choices[]={{100,L"Install\nAdd Winchisel to the Start menu"},{101,L"Install with desktop shortcut\nAlso place a shortcut on the desktop"}};const auto answer=task_dialog(L"Winchisel Setup",L"Ready to install Winchisel",L"Winchisel will be installed for all users in Program Files.",TDCBF_CANCEL_BUTTON,TD_SHIELD_ICON,choices,static_cast<UINT>(std::size(choices)),TDF_USE_COMMAND_LINKS|TDF_POSITION_RELATIVE_TO_WINDOW);if(answer!=100&&answer!=101)return 0;const auto program_files=known(FOLDERID_ProgramFiles);if(program_files.empty())return failure(L"Program Files is unavailable.");const auto target=program_files/L"Winchisel";std::vector<std::wstring> installed_paths;if(!unpack(self,target,&installed_paths))return failure(L"Installation failed. Close Winchisel and check available disk space.");const auto executable=target/L"Winchisel.exe";if(!CopyFileW(self.c_str(),(target/L"Winchisel.Uninstall.exe").c_str(),FALSE))return failure(L"The uninstaller could not be created.");std::wofstream manifest(target/L"install-manifest.txt",std::ios::trunc);for(auto const& path:installed_paths)manifest<<path<<L'\n';manifest<<L"Winchisel.Uninstall.exe\ninstall-manifest.txt\n";if(!manifest)return failure(L"The install manifest could not be created.");manifest.close();if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return failure(L"Windows shell initialization failed.");const auto menu=known(FOLDERID_CommonPrograms)/L"Winchisel.lnk";const bool menu_ok=shortcut(menu,executable);const bool desktop_ok=answer!=101||shortcut(known(FOLDERID_PublicDesktop)/L"Winchisel.lnk",executable);CoUninitialize();if(!menu_ok||!desktop_ok)return failure(L"Winchisel was installed, but a shortcut could not be created.");register_install(target,version_of(target));constexpr TASKDIALOG_BUTTON done[]={{100,L"Launch Winchisel"}};const auto next=task_dialog(L"Winchisel Setup",L"Installation complete",L"Winchisel is ready to use.",TDCBF_CLOSE_BUTTON,TD_INFORMATION_ICON,done,static_cast<UINT>(std::size(done)));if(next==100&&!start(executable))return failure(L"Winchisel was installed, but could not be started.");return 0;}

int portable(std::filesystem::path const& self){
    const auto local=known(FOLDERID_LocalAppData);
    if(local.empty())return failure(L"LocalAppData is unavailable.");
    const auto hash=winchisel::release::package_hash(self);
    const auto target=winchisel::release::prepare_portable(local/L"Winchisel"/L"portable",hash,
        [&](auto const& staging){return unpack(self,staging);});
    if(!target)return failure(L"The portable application could not be prepared.");
    SetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST",self.c_str());
    const bool started=start(*target/L"Winchisel.exe");
    SetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST",nullptr);
    return started?0:failure(L"The portable application could not be started.");
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){const auto self=module_path();if(self.empty())return failure(L"The package path could not be determined.");if(std::wstring_view(GetCommandLineW()).find(L"--uninstall")!=std::wstring_view::npos)return uninstall(self);std::ifstream input(self,std::ios::binary|std::ios::ate);Footer footer{};input.seekg(-static_cast<std::streamoff>(sizeof(footer)),std::ios::end);input.read(reinterpret_cast<char*>(&footer),sizeof(footer));if(!input||std::string_view(footer.magic,8)!="WCHBNDL2")return failure(L"The embedded package is invalid.");return footer.mode==2?install(self):portable(self);}
