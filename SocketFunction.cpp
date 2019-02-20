#include "SocketFunction.h"



#include <string>
#include <iostream>
#include <thread>

std::mutex mut;
//std::unique_lock<std::mutex> lk(mut);


#ifdef _WIN32
#include <WinSock2.h>
//函数功能：将wstring转换为string
//输入参数：ws（原wstring）
//返 回 值：ws转换后的string
std::string ws2s(const std::wstring& ws)
{
	size_t convertedChars = 0;
	std::string curLocale = setlocale(LC_ALL, NULL); //curLocale="C"
	setlocale(LC_ALL, "chs");
	const wchar_t* wcs = ws.c_str();
	size_t dByteNum = sizeof(wchar_t)*ws.size() + 1;


	char* dest = new char[dByteNum];
	wcstombs_s(&convertedChars, dest, dByteNum, wcs, _TRUNCATE);
	std::string result = dest;
	delete[] dest;
	setlocale(LC_ALL, curLocale.c_str());
	return result;
}

std::string GetError(int nError)
{
	HLOCAL LocalAddress = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, nError, 0, (PTSTR)&LocalAddress, 0, NULL);

#ifdef _UNICODE
	std::wstring strError((LPCTSTR)(PTSTR)LocalAddress);
	std::string strRs = ws2s(strError);
#else
	std::string strRs = (PTSTR)LocalAddress;
	
#endif
	return strRs;
}


#else
#ifndef INVALID_SOCKET
#define INVALID_SOCKET  (SOCKET)(~0)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR            (-1)
#endif

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <zlib.h>
#include <unistd.h>
#include <arpa/inet.h>  
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <functional> 
int closesocket (SOCKET s)
{
	return close(s);
}
int GetLastError()
{
	return errno;
}
int WSAGetLastError()
{
	return errno;
}
std::string GetError(int nError)
{
	std::string str1(strerror(nError));
	return str1;
}
unsigned long GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

