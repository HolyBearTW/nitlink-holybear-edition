#include "nintendo_auth_callback.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <future>
#include <string_view>

namespace NitLink::Accounts {
namespace {
constexpr wchar_t Protocol[]=L"npf5c38e31cd085304b";
constexpr wchar_t RegistryKey[]=L"Software\\Classes\\npf5c38e31cd085304b";
constexpr wchar_t ReceiverClass[]=L"NitLink.Nintendo.AuthCallback";
constexpr ULONG_PTR MessageId=0x4e4c4e41;
constexpr size_t MaximumUri=8192;
struct Receiver {
    std::wstring state;
    std::optional<NintendoAuthCallback::Result> result;
};
LRESULT CALLBACK Receive(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));return TRUE;}
    if(message!=WM_COPYDATA)return DefWindowProcW(window,message,wParam,lParam);
    const auto receiver=reinterpret_cast<Receiver*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    const auto data=reinterpret_cast<const COPYDATASTRUCT*>(lParam);
    if(!receiver||receiver->result||!data||data->dwData!=MessageId||!data->lpData||data->cbData<2||data->cbData>(MaximumUri+1)*sizeof(wchar_t)||data->cbData%sizeof(wchar_t))return FALSE;
    const auto text=static_cast<const wchar_t*>(data->lpData);const auto count=data->cbData/sizeof(wchar_t);
    if(text[count-1]!=L'\0'||std::find(text,text+count-1,L'\0')!=text+count-1)return FALSE;
    const std::wstring_view uri(text,count-1);
    const auto fragment=uri.find(L'#');
    if(fragment==uri.npos)return FALSE;
    const auto target=uri.substr(0,fragment);
    if(target!=L"npf5c38e31cd085304b://auth"&&target!=L"npf5c38e31cd085304b://auth/")return FALSE;
    // This is the authentication boundary. Only a matching, unconsumed state can complete it.
    try {
        winrt::Windows::Foundation::WwwFormUrlDecoder fields(winrt::hstring(uri.substr(fragment+1)));
        std::wstring state,code,error;unsigned states=0,codes=0,errors=0;
        for(const auto& field:fields){
            if(field.Name()==L"state"){++states;state=field.Value();}
            if(field.Name()==L"session_token_code"){++codes;code=field.Value();}
            if(field.Name()==L"error"){++errors;error=field.Value();}
        }
        if(states!=1||state!=receiver->state)return FALSE;
        if(codes==1&&!errors&&!code.empty()&&code.size()<=4096&&std::none_of(code.begin(),code.end(),[](wchar_t c){return c<0x20||c==0x7f;}))
            receiver->result=NintendoAuthCallback::Result{NintendoAuthCallback::Outcome::Code,std::move(code)};
        else if(errors==1&&!codes)receiver->result=NintendoAuthCallback::Result{error==L"access_denied"?NintendoAuthCallback::Outcome::Denied:NintendoAuthCallback::Outcome::Rejected,{}};
        else return FALSE;
        return TRUE;
    }catch(const winrt::hresult_error&){return FALSE;}
}
}

void NintendoAuthCallback::Stop() {
    if(m_thread.joinable()){m_thread.request_stop();m_thread.join();}
}

NintendoAuthCallback::StartResult NintendoAuthCallback::Start(std::wstring state,Completion complete) {
    Stop();
    std::promise<StartResult> ready;auto started=ready.get_future();
    m_thread=std::jthread([state=std::move(state),complete=std::move(complete),ready=std::move(ready)](std::stop_token stop) mutable {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto instance=GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};windowClass.lpfnWndProc=Receive;windowClass.hInstance=instance;windowClass.lpszClassName=ReceiverClass;
        const auto registered=RegisterClassW(&windowClass);
        if(!registered&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){ready.set_value(StartResult::Failed);winrt::uninit_apartment();return;}
        Receiver receiver{std::move(state),{}};
        const auto window=CreateWindowExW(0,ReceiverClass,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,&receiver);
        if(!window){ready.set_value(StartResult::Failed);winrt::uninit_apartment();return;}
        // Browser activation can come from a non-elevated process when NitLink is elevated.
        // Only this private receiver accepts COPYDATA; authentication still requires its random state and PKCE.
        if(!ChangeWindowMessageFilterEx(window,WM_COPYDATA,MSGFLT_ALLOW,nullptr)){DestroyWindow(window);ready.set_value(StartResult::Failed);winrt::uninit_apartment();return;}
        std::array<wchar_t,32768> module{},priorOwner{},association{};
        const auto length=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
        DWORD ownerBytes=static_cast<DWORD>(priorOwner.size()*sizeof(wchar_t));
        const bool owned=RegGetValueW(HKEY_CURRENT_USER,RegistryKey,L"NitLinkOwner",RRF_RT_REG_SZ,nullptr,priorOwner.data(),&ownerBytes)==ERROR_SUCCESS;
        DWORD associationLength=static_cast<DWORD>(association.size());
        const auto associated=AssocQueryStringW(ASSOCF_IS_PROTOCOL,ASSOCSTR_EXECUTABLE,Protocol,nullptr,association.data(),&associationLength);
        const bool otherApp=SUCCEEDED(associated)&&_wcsicmp(association.data(),module.data())&&(!owned||_wcsicmp(association.data(),priorOwner.data()));
        ULONGLONG previousWindow=0;DWORD windowBytes=sizeof(previousWindow);
        RegGetValueW(HKEY_CURRENT_USER,RegistryKey,L"NitLinkWindow",RRF_RT_REG_QWORD,nullptr,&previousWindow,&windowBytes);
        HKEY key=nullptr;DWORD disposition=0;
        auto status=StartResult::Failed;
        std::wstring command;
        if(otherApp||(owned&&IsWindow(reinterpret_cast<HWND>(static_cast<UINT_PTR>(previousWindow)))))status=StartResult::InUse;
        else if(length&&length<module.size()&&RegCreateKeyExW(HKEY_CURRENT_USER,RegistryKey,0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&key,&disposition)==ERROR_SUCCESS){
            if(disposition==REG_OPENED_EXISTING_KEY&&!owned)status=StartResult::InUse;
            else {
                const auto handle=static_cast<ULONGLONG>(reinterpret_cast<UINT_PTR>(window));
                command=L"\""+std::wstring(module.data(),length)+L"\" --nintendo-auth-return "+std::to_wstring(handle)+L" \"%1\"";
                constexpr wchar_t description[]=L"URL:NitLink Nintendo sign-in";
                HKEY openCommand=nullptr;
                if(RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(description),sizeof(description))==ERROR_SUCCESS&&
                    RegSetValueExW(key,L"URL Protocol",0,REG_SZ,reinterpret_cast<const BYTE*>(L""),sizeof(wchar_t))==ERROR_SUCCESS&&
                    RegSetValueExW(key,L"NitLinkOwner",0,REG_SZ,reinterpret_cast<const BYTE*>(module.data()),(length+1)*sizeof(wchar_t))==ERROR_SUCCESS&&
                    RegSetValueExW(key,L"NitLinkWindow",0,REG_QWORD,reinterpret_cast<const BYTE*>(&handle),sizeof(handle))==ERROR_SUCCESS&&
                    RegCreateKeyExW(key,L"shell\\open\\command",0,nullptr,0,KEY_WRITE,nullptr,&openCommand,nullptr)==ERROR_SUCCESS){
                    if(RegSetValueExW(openCommand,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS)status=StartResult::Ready;
                    RegCloseKey(openCommand);
                }
            }
            RegCloseKey(key);
            if(status==StartResult::Failed)RegDeleteTreeW(HKEY_CURRENT_USER,RegistryKey); // Only our newly created or stale owned registration.
        }
        if(status==StartResult::Ready)SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
        ready.set_value(status);
        if(status==StartResult::Ready){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
            while(!stop.stop_requested()&&!receiver.result&&std::chrono::steady_clock::now()<deadline){
                MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            }
            std::array<wchar_t,32768> current{};DWORD bytes=static_cast<DWORD>(current.size()*sizeof(wchar_t));
            if(RegGetValueW(HKEY_CURRENT_USER,(std::wstring(RegistryKey)+L"\\shell\\open\\command").c_str(),nullptr,RRF_RT_REG_SZ,nullptr,current.data(),&bytes)==ERROR_SUCCESS&&command==current.data()){
                RegDeleteTreeW(HKEY_CURRENT_USER,RegistryKey);
                SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
            }
        }
        DestroyWindow(window);
        if(status==StartResult::Ready&&!stop.stop_requested())complete(receiver.result.value_or(Result{Outcome::Timeout,{}}));
        winrt::clear_factory_cache();winrt::uninit_apartment();
    });
    return started.get();
}

std::optional<int> ForwardNintendoAuthCallback() {
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)return 2;
    if(count<2||std::wstring_view(args[1])!=L"--nintendo-auth-return"){LocalFree(args);return std::nullopt;}
    int result=2;
    if(count==4){
        wchar_t* end=nullptr;const auto number=std::wcstoull(args[2],&end,10);
        const auto window=reinterpret_cast<HWND>(static_cast<UINT_PTR>(number));
        std::array<wchar_t,128> name{};GetClassNameW(window,name.data(),static_cast<int>(name.size()));
        const std::wstring_view uri(args[3]);
        if(number&&end!=args[2]&&!*end&&std::wstring_view(name.data())==ReceiverClass&&uri.size()<=MaximumUri){
            COPYDATASTRUCT data{MessageId,static_cast<DWORD>((uri.size()+1)*sizeof(wchar_t)),args[3]};DWORD_PTR accepted=0;
            if(SendMessageTimeoutW(window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&accepted)&&accepted)result=0;
        }
    }
    LocalFree(args);return result;
}
}
