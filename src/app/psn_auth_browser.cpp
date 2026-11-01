#include "psn_auth_browser.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace NitLink::Accounts {
namespace {
using Handle=std::unique_ptr<void,decltype(&CloseHandle)>;
using Internet=std::unique_ptr<void,decltype(&WinHttpCloseHandle)>;
struct BrowserSession {
    std::filesystem::path root,folder;
    Handle job{nullptr,CloseHandle},process{nullptr,CloseHandle};
    Internet session{nullptr,WinHttpCloseHandle},connection{nullptr,WinHttpCloseHandle},socket{nullptr,WinHttpCloseHandle};
    Handle ready{CreateEventW(nullptr,FALSE,FALSE,nullptr),CloseHandle},drained{CreateEventW(nullptr,TRUE,FALSE,nullptr),CloseHandle};
    bool waitForDrain=false;
    DWORD error=0,bytes=0;
    std::string outgoing;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
    std::array<char,16384> buffer{}; // Remains alive until all cancelled receive callbacks have drained.
    ~BrowserSession(){
        if(socket){
            static constexpr char close[]=R"({"id":2147483647,"method":"Browser.close"})";
            ResetEvent(ready.get());
            WinHttpWebSocketSend(socket.get(),WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,const_cast<char*>(close),sizeof(close)-1);
            WaitForSingleObject(ready.get(),1000);
        }
        socket.reset();connection.reset();session.reset();
        if(waitForDrain)WaitForSingleObject(drained.get(),INFINITE);
        if(process)WaitForSingleObject(process.get(),1500);
        job.reset(); // This job contains only the browser created with our unique, isolated profile.
        if(process)WaitForSingleObject(process.get(),1500);
        process.reset();
        std::error_code error;
        if(!folder.empty()&&std::filesystem::weakly_canonical(folder,error).parent_path()==root&&!error)std::filesystem::remove_all(folder,error);
    }
};
void CALLBACK HttpComplete(HINTERNET,DWORD_PTR context,DWORD status,LPVOID information,DWORD){
    if(!context)return;
    auto& browser=*reinterpret_cast<BrowserSession*>(context);
    if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)browser.error=static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
    else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE){
        const auto result=static_cast<WINHTTP_WEB_SOCKET_STATUS*>(information);browser.bytes=result->dwBytesTransferred;browser.type=result->eBufferType;
    }else if(status!=WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE&&status!=WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE&&status!=WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)return;
    SetEvent(browser.ready.get());
}
}

void PsnAuthBrowser::Stop(){
    if(m_thread.joinable()){m_thread.request_stop();m_thread.join();}
}

void PsnAuthBrowser::Start(Completion complete){
    Stop();
    m_thread=std::jthread([complete=std::move(complete)](std::stop_token stop){
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Result result{Outcome::Browser,{}};
        {
            BrowserSession browser;
            try {
                // Launch a real browser with a new profile. Never attach to a user's running browser.
                std::filesystem::path executable;
                std::array<wchar_t,32768> preferred{};DWORD preferredLength=static_cast<DWORD>(preferred.size());
                if(SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL,ASSOCSTR_EXECUTABLE,L"https",L"open",preferred.data(),&preferredLength))){
                    const std::filesystem::path path(preferred.data());const auto name=path.filename().wstring();
                    if(!_wcsicmp(name.c_str(),L"msedge.exe")||!_wcsicmp(name.c_str(),L"chrome.exe"))executable=path;
                }
                for(const auto& known:{FOLDERID_ProgramFilesX86,FOLDERID_ProgramFiles,FOLDERID_LocalAppData}){
                    if(!executable.empty())break;
                    PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(known,0,nullptr,&value)))continue;
                    const std::filesystem::path root(value);CoTaskMemFree(value);
                    for(const auto* relative:{L"Microsoft/Edge/Application/msedge.exe",L"Google/Chrome/Application/chrome.exe"}){
                        std::error_code error;const auto path=root/relative;
                        if(std::filesystem::is_regular_file(path,error)){executable=path;break;}
                    }
                    if(!executable.empty())break;
                }
                if(executable.empty())throw Outcome::Browser;
                PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw Outcome::Browser;
                browser.root=std::filesystem::path(local)/L"NitLink"/L"AccountLogin"/L"PlayStation";CoTaskMemFree(local);
                GUID id{};std::array<wchar_t,40> identifier{};
                if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,identifier.data(),static_cast<int>(identifier.size())))throw Outcome::Browser;
                std::error_code error;
                std::filesystem::create_directories(browser.root,error);if(error)throw Outcome::Browser;
                browser.root=std::filesystem::weakly_canonical(browser.root,error);if(error)throw Outcome::Browser;
                const auto folder=browser.root/identifier.data();
                if(!std::filesystem::create_directory(folder,error)||error)throw Outcome::Browser;
                browser.folder=folder;
                browser.job.reset(CreateJobObjectW(nullptr,nullptr));
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if(!browser.job||!SetInformationJobObject(browser.job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw Outcome::Browser;
                auto command=L"\""+executable.wstring()+L"\" --remote-debugging-port=0 --remote-debugging-address=127.0.0.1 --user-data-dir=\""+
                    browser.folder.wstring()+L"\" --no-first-run --no-default-browser-check --disable-background-mode --new-window https://www.playstation.com/acct/management/";
                STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
                if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&process))throw Outcome::Browser;
                browser.process.reset(process.hProcess);Handle thread(process.hThread,CloseHandle);
                if(!AssignProcessToJobObject(browser.job.get(),browser.process.get())){TerminateProcess(browser.process.get(),1);throw Outcome::Browser;}
                if(ResumeThread(thread.get())==static_cast<DWORD>(-1))throw Outcome::Browser;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
                const auto startupDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                unsigned port=0;std::string endpoint;
                while(!stop.stop_requested()&&std::chrono::steady_clock::now()<startupDeadline){
                    std::ifstream active(browser.folder/L"DevToolsActivePort");std::string number;
                    if(std::getline(active,number)&&std::getline(active,endpoint)){
                        if(!number.empty()&&number.back()=='\r')number.pop_back();if(!endpoint.empty()&&endpoint.back()=='\r')endpoint.pop_back();
                        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),port);
                        if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||!port||port>65535||
                            !endpoint.starts_with("/devtools/browser/")||endpoint.size()>256||endpoint.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=endpoint.npos)throw Outcome::Browser;
                        break;
                    }
                    if(WaitForSingleObject(browser.process.get(),100)==WAIT_OBJECT_0)throw Outcome::Cancelled;
                }
                if(stop.stop_requested())throw Outcome::Cancelled;
                if(!port)throw Outcome::Browser;
                browser.session.reset(WinHttpOpen(L"NitLink-PSN-Login/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
                if(!browser.session||!browser.ready||!browser.drained)throw Outcome::Browser;
                HANDLE drained=browser.drained.get();
                if(!WinHttpSetOption(browser.session.get(),WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT,&drained,sizeof(drained)))throw Outcome::Browser;
                browser.waitForDrain=true;
                if(WinHttpSetStatusCallback(browser.session.get(),HttpComplete,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS,0)==WINHTTP_INVALID_STATUS_CALLBACK)throw Outcome::Browser;
                WinHttpSetTimeouts(browser.session.get(),1000,1000,2000,2000);
                const auto wait=[&]{
                    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                    while(std::chrono::steady_clock::now()<until){
                        if(stop.stop_requested())throw Outcome::Cancelled;
                        if(WaitForSingleObject(browser.ready.get(),100)==WAIT_OBJECT_0){if(browser.error)throw Outcome::Browser;return;}
                    }
                    throw Outcome::Browser;
                };
                browser.connection.reset(WinHttpConnect(browser.session.get(),L"127.0.0.1",static_cast<INTERNET_PORT>(port),0));
                const auto path=winrt::to_hstring(endpoint);
                Internet request(browser.connection?WinHttpOpenRequest(browser.connection.get(),L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0):nullptr,WinHttpCloseHandle);
                DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;
                if(!request||!WinHttpSetOption(request.get(),WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||!WinHttpSetOption(request.get(),WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0)||
                    !WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(&browser)))throw Outcome::Browser;
                wait();
                if(!WinHttpReceiveResponse(request.get(),nullptr))throw Outcome::Browser;
                wait();
                DWORD status=0,size=sizeof(status);
                if(!WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)||status!=101)throw Outcome::Browser;
                browser.socket.reset(WinHttpWebSocketCompleteUpgrade(request.get(),reinterpret_cast<DWORD_PTR>(&browser)));request.reset();
                if(!browser.socket)throw Outcome::Browser;
                // Only this isolated profile's Sony NPSSO is used. No scripts, passwords or daily browser profile are accessed.
                unsigned sequence=0;
                while(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline){
                    browser.outgoing=std::string("{\"id\":")+std::to_string(++sequence)+",\"method\":\"Storage.getCookies\"}";
                    const auto sent=WinHttpWebSocketSend(browser.socket.get(),WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,browser.outgoing.data(),static_cast<DWORD>(browser.outgoing.size()));
                    if(sent!=NO_ERROR&&sent!=ERROR_IO_PENDING)throw Outcome::Browser;
                    wait();
                    for(;;){
                        std::string message;WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                        do {
                            const auto received=WinHttpWebSocketReceive(browser.socket.get(),browser.buffer.data(),static_cast<DWORD>(browser.buffer.size()),nullptr,nullptr);
                            if(received!=NO_ERROR&&received!=ERROR_IO_PENDING)throw Outcome::Browser;
                            wait();type=browser.type;
                            if(stop.stop_requested())throw Outcome::Cancelled;
                            if(type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)throw Outcome::Cancelled;
                            if(type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE&&type!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)throw Outcome::Browser;
                            message.append(browser.buffer.data(),browser.bytes);if(message.size()>2*1024*1024)throw Outcome::Browser;
                        }while(type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);
                        const auto response=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(message));
                        const auto responseId=response.TryLookup(L"id");
                        if(!responseId||responseId.ValueType()!=winrt::Windows::Data::Json::JsonValueType::Number||responseId.GetNumber()!=sequence)continue;
                        if(response.HasKey(L"error"))throw Outcome::Browser;
                        for(const auto& entry:response.GetNamedObject(L"result").GetNamedArray(L"cookies")){
                            const auto cookie=entry.GetObject();const auto domain=cookie.GetNamedString(L"domain");
                            if(cookie.GetNamedString(L"name")==L"npsso"&&(domain==L"sony.com"||std::wstring_view(domain).ends_with(L".sony.com"))){
                                const auto value=cookie.GetNamedString(L"value");
                                if(!value.empty()){result={Outcome::Cookie,std::wstring(value)};break;}
                            }
                        }
                        break;
                    }
                    if(result.outcome==Outcome::Cookie)break;
                    for(unsigned wait=0;wait<10&&!stop.stop_requested();++wait)
                        if(WaitForSingleObject(browser.process.get(),100)==WAIT_OBJECT_0)throw Outcome::Cancelled;
                }
                if(result.outcome!=Outcome::Cookie)throw Outcome::Timeout;
            }catch(Outcome outcome){result={outcome,{}};}
            catch(const winrt::hresult_error&){result={Outcome::Browser,{}};}
        }
        if(!stop.stop_requested())complete(std::move(result));
        winrt::clear_factory_cache();winrt::uninit_apartment();
    });
}
}
