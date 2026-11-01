#include "app/xbox_auth_callback.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using NitLink::Accounts::XboxAuthCallback;
using namespace std::chrono_literals;
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
unsigned short Port(const std::wstring& uri){return static_cast<unsigned short>(std::stoi(uri.substr(uri.find_last_of(L':')+1)));}
std::string Get(unsigned short port,const std::string& target){
    const auto client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    Check(client!=INVALID_SOCKET,"client socket");
    DWORD timeout=2000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
    if(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))){closesocket(client);return {};}
    const auto request="GET "+target+" HTTP/1.1\r\nHost: localhost:"+std::to_string(port)+"\r\nConnection: close\r\n\r\n";
    send(client,request.c_str(),static_cast<int>(request.size()),0);
    std::string response;char buffer[2048];int bytes;
    while((bytes=recv(client,buffer,sizeof(buffer),0))>0)response.append(buffer,bytes);
    closesocket(client);return response;
}
}
int main(){
    WSADATA winsock{};Check(WSAStartup(MAKEWORD(2,2),&winsock)==0,"Winsock initialization");
    int exit=0;
    try{
        XboxAuthCallback receiver;std::promise<XboxAuthCallback::Result> completed;auto future=completed.get_future();std::atomic<unsigned> count=0;
        const auto uri=receiver.Start(L"test-state",[&](auto result){++count;completed.set_value(std::move(result));},3s);
        Check(uri&&uri->starts_with(L"http://localhost:")&&uri->ends_with(L"/"),"ephemeral localhost redirect");
        const auto port=Port(*uri);Check(port!=0,"Windows assigned a port");
        Check(Get(port,"/favicon.ico").starts_with("HTTP/1.1 404"),"favicon does not complete sign-in");
        Check(Get(port,"/?state=wrong&code=not-a-real-code").starts_with("HTTP/1.1 403"),"wrong state rejected");
        Check(Get(port,"/?state=test-state&state=test-state&code=not-a-real-code").starts_with("HTTP/1.1 403"),"duplicate state rejected");
        Check(future.wait_for(0ms)==std::future_status::timeout,"invalid requests do not consume valid login");
        const auto response=Get(port,"/?state=test-state&code=test%2Bcode");
        Check(response.starts_with("HTTP/1.1 200")&&response.find("test+code")==response.npos&&response.find("test%2Bcode")==response.npos&&response.find("Cache-Control: no-store")!=response.npos,"response never exposes authorization code");
        Check(future.wait_for(2s)==std::future_status::ready,"valid callback completes");
        const auto result=future.get();Check(result.outcome==XboxAuthCallback::Outcome::Code&&result.code==L"test+code","authorization code decoded once");
        receiver.Stop();Get(port,"/?state=test-state&code=duplicate");Check(count==1,"duplicate callback cannot complete twice");
        std::promise<XboxAuthCallback::Result> denied;auto denial=denied.get_future();
        const auto deniedUri=receiver.Start(L"test-state",[&](auto value){denied.set_value(std::move(value));},3s);
        Check(deniedUri.has_value(),"listener restarts");Get(Port(*deniedUri),"/?state=test-state&error=access_denied");
        Check(denial.wait_for(2s)==std::future_status::ready&&denial.get().outcome==XboxAuthCallback::Outcome::Denied,"consent cancellation reported");receiver.Stop();
        std::promise<XboxAuthCallback::Result> expired;auto expiration=expired.get_future();
        Check(receiver.Start(L"test-state",[&](auto value){expired.set_value(std::move(value));},150ms).has_value(),"timeout listener starts");
        Check(expiration.wait_for(2s)==std::future_status::ready&&expiration.get().outcome==XboxAuthCallback::Outcome::Timeout,"login timeout closes receiver");receiver.Stop();
        std::atomic<unsigned> cancelledCallbacks=0;
        const auto cancelUri=receiver.Start(L"test-state",[&](auto){++cancelledCallbacks;},3s);Check(cancelUri.has_value(),"cancellable receiver starts");
        const auto begin=std::chrono::steady_clock::now();receiver.Stop();
        Check(std::chrono::steady_clock::now()-begin<2s&&cancelledCallbacks==0,"explicit stop promptly joins without submitting a callback");
        XboxAuthCallback occupied;const auto occupiedUri=occupied.Start(L"other-state",[](auto){},3s);Check(occupiedUri.has_value(),"another local listener owns a port");
        const auto second=receiver.Start(L"test-state",[](auto){},3s);Check(second&&Port(*second)!=Port(*occupiedUri),"existing occupied port does not block dynamic allocation");
        receiver.Stop();occupied.Stop();
        std::cout<<"PASS Xbox loopback state, cancellation, timeout, replay, restart and dynamic-port cases\n";
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';exit=1;}
    WSACleanup();return exit;
}
