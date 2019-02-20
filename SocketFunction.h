#ifndef SOCKET_FUNCTION_H
#define SOCKET_FUNCTION_H


#ifdef _WIN32

#endif


#include <mutex>
#include <string>

#ifndef _WIN32
#ifndef _WINSOCKAPI_
typedef int        SOCKET;
int closesocket(SOCKET s);
 int GetLastError();
 unsigned long GetTickCount();
 int WSAGetLastError();
#endif
#endif


std::string GetError(int nError);


#endif