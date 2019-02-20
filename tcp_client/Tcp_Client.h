#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <atomic>
#include<string>
#include<vector>
#include "Queue_ThreadSafe.h"
#include "Tcp_Server.h"
void Thread_Send(const int nSendPort, threadsafe_queue<rsData>* MyQueue);

void WriteSendLogs(const char*  Format, ...);
int32_t createDirectory(const std::string &directoryPath);

void Thread_DelOldFile();
int  GetDelTime(const int nDays);
void GetHistoryDataFolders(const std::string path, std::vector<std::string>& files);
void GetLogsTxtFiles(const std::string path, std::vector<std::string>& txtFiles);
int GetNumFromString(std::string strData, const int n);
void DelHistoryDataFiles(const std::string filePath, const int DelTime);
void DelHistoryLogs(const std::string filePath, const int DelTime);
void DoRemoveFile(const std::string filePath);

std::string ws2s(const std::wstring& ws);
std::string GetError(const int nError);

#endif