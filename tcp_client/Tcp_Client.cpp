#include "SocketFunction.h"
#include "TypeDefine.h"
#include "Tcp_Client.h"
#include "Tcp_Server.h"
#include<stdlib.h>

#ifdef _WIN32
#include <afx.h>
#include <WinSock2.h>  
#include "zlib\zlib.h"
#include <direct.h> 
#include <io.h>
#pragma comment(lib,"zlib/zlib.lib")
#pragma comment(lib, "ws2_32.lib")  
#else

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <zlib.h>
#include <unistd.h>
#include <arpa/inet.h>  
#include <arpa/inet.h>  
//#include "/home/xby/StandardOutPut_Program/mysql/include/mysql.h"
//#include "/home/xby/StandardOutPut_Program/hiredis.h"
#include <sys/stat.h>  
#include <dirent.h>

#endif

#include <stdio.h>  
#include <thread>       
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include<stdarg.h>

//用于发送报头、心跳包等 仅包含长度信息
union U4
{
	float v;
	unsigned char c[4];
	unsigned int i;
}uu4;

//本cpp文件全局变量、容器、锁
#ifdef _WIN32

std::vector<CString> vec_cstr_LogSendData;	//发送数据的日志容器


#endif

//外部文件全局变量、容器、锁、函数
extern bool bCurSendStatus;	//发送状态
extern int nRecvCount;	//记录容器vec_rsData_RecvData的当前数目
extern std::string strSendIp;	//总部服务端Ip
extern std::string strPath;//程序路径
extern std::vector<rsData> vec_rsData_RecvData;	//存储接收线程收到的数据

//extern HANDLE hMutex_vec_rsData_RecvData;	//锁：资源为 存储接收线程收到的数据
//extern HANDLE hMutex_vec_cstr_LogSendData;	//锁：资源为 发送数据的日志信息
extern std::mutex mut;
//extern std::unique_lock<std::mutex> lk(mut);



//函数功能：供线程直接调用的函数，用于发送数据
//输入参数：nSendPort 总部服务端接收的端口，总部服务端的Ip已由全局变量strSendIp传入
void Thread_Send(const int nSendPort, threadsafe_queue<rsData>* MyQueue)
{
	bCurSendStatus = false;
#ifdef _WIN32
	WORD wVerisonRequested = MAKEWORD(1, 1);
	WSADATA wsaData;
	int err;
	for (int i = 0; i < 10; i++)
	{
		err = WSAStartup(wVerisonRequested, &wsaData);
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		if (err == 0)
		{
			break;
		}
	}
	if (err != 0)
	{
		std::string ErrMsg = GetError(WSAGetLastError());
		WriteSendLogs("WSAStartup error : %s", ErrMsg.c_str());
		return;
	}
#endif
	SOCKET sockfd;
	for (int i = 0; i < 10; i++)
	{
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		if (sockfd != -1)
		{
			break;
		}
	}
	if (sockfd == -1)
	{
		std::string ErrMsg = GetError(WSAGetLastError());
		WriteSendLogs("socket error : %s", ErrMsg.c_str());
		return;
	}
#ifdef _WIN32
	struct hostent *host;
	for (int i = 0; i < 10; i++)
	{
		host = gethostbyname(strSendIp.c_str());
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		if (host != NULL)
		{
			break;
		}
	}
	if (host == NULL)
	{
		std::string ErrMsg = GetError(WSAGetLastError());
		WriteSendLogs("host error : %s", ErrMsg.c_str());
		return;
	}
#endif

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(nSendPort);//总部接收端的端口
	serv_addr.sin_addr.s_addr = inet_addr(strSendIp.c_str());//总部接收端的ip
	
	int len = sizeof(serv_addr);
	int timeout = 1000 * 3;		//1000=1秒 超时时间
	int nSendBuf = 32 * 1024;	//接收缓冲区设置 单位B

	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));

	
	while (connect(sockfd, (struct sockaddr *)&serv_addr,
		sizeof(struct sockaddr)) == -1)
	{

		std::string ErrMsg = GetError(WSAGetLastError());
		WriteSendLogs("进入周期前，连接服务端失败，原因%s", ErrMsg.c_str());
		printf("                                ThreadSend:connect failed : %s \r\n", ErrMsg.c_str());
		closesocket(sockfd);
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		{
			//备用
		}
		setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
		setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
		
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	}

	bCurSendStatus = true;

	char HeadBuf[LEN_HEAD] = { 0 };	//存储报头部分	
	int nErrorCount = 0;	//累计接收不成功次数

	int nSend = -1; // 记录本次发送的数据数目 小于0均视为失败
	bool bTcpReconnect = false;	//记录是否发生过 tcp重连成功

	rsData m_Heartpacket;
	m_Heartpacket.len = 0;
	int nHeart = 0;//心跳次数 出现3次无数据发送 则发送心跳包
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		if(MyQueue->empty())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			printf("                                ThreadSend:no data \r\n");
			nHeart++;
			if(nHeart == 3)
			{
				MyQueue->push(m_Heartpacket);
			}
			continue;
		}	
		//获得发送容器中数据数目		
		else
		{
			nHeart = 0;
			rsData m_rsData;			
			bool b = MyQueue->try_front(m_rsData);
			if(!b)
			{
				continue;
			}
			int nLen = m_rsData.len; //报头的内容

			uu4.i = nLen;
			HeadBuf[0 + 3] = uu4.c[3];
			HeadBuf[0 + 2] = uu4.c[2];
			HeadBuf[0 + 1] = uu4.c[1];
			HeadBuf[0] = uu4.c[0];

			//发送第一个报头
			int nNum = 0;	//记录本条数据中已发送的数据字节数

			while (nNum < 4)
			{
				
				nSend = send(sockfd, HeadBuf + nNum, 4 - nNum, 0);
				if (nSend > 0)
				{
					nNum += nSend;
				}
				else
				{
					nErrorCount++;
					std::string ErrMsg = GetError(WSAGetLastError());
					WriteSendLogs("报头发送失败，数据长度%d，原因%s", nLen, ErrMsg.c_str());
					printf("                                ThreadSend:connection lost:%s \r\n", ErrMsg.c_str());
					bCurSendStatus = false;
					
					if(sockfd > 0 )
					{
						int nCloseRes = closesocket(sockfd);
						while(nCloseRes != 0)//关闭失败 则继续等待关闭
						{					
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
							WriteSendLogs("发送报头：关闭套接字...nCloseRes:%d",nCloseRes);
							nCloseRes = closesocket(sockfd);
						}

						WriteSendLogs("发送报头：关闭套接字...nCloseRes:%d",nCloseRes);
						printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
						std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
					}
					
					if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
					{
						//备用
					}
					setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
					setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
					std::this_thread::sleep_for(std::chrono::milliseconds(500));	
					while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
					{
						if(sockfd > 0 )
						{
							int nCloseRes = closesocket(sockfd);
							while(nCloseRes != 0)//关闭失败 则继续等待关闭
							{					
								std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
								WriteSendLogs("发送报头：重连关闭套接字...nCloseRes:%d",nCloseRes);
								nCloseRes = closesocket(sockfd);
							}

							WriteSendLogs("发送报头：重连关闭套接字...nCloseRes:%d",nCloseRes);
							printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
						}
						if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
						{
							//备用
						}
						setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
						setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
						WriteSendLogs("报头发送失败后，重连总部服务端...");
						printf("                                ThreadSend:reconnection... \r\n");
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
					}
					WriteSendLogs("报头发送失败后，重连总部服务端成功...");
					printf("                                ThreadSend:reconnection success \r\n");
					bCurSendStatus = true;
					nErrorCount = 0;
					bTcpReconnect = true;
					break;	
				}
				

			} // end of while 发送报头结束
			if (bTcpReconnect)
			{
				bTcpReconnect = false;
				continue;
			}
			WriteSendLogs("报头发送完成，数据长度%d", nLen );
			
			if(nLen == 0)//发送的心跳包
			{
				MyQueue->try_pop(m_rsData);
				continue;
			}
			//发送数据
			nNum = 0;
			while (nLen > nNum)
			{
				nSend = send(sockfd, m_rsData.c + nNum, nLen - nNum, 0);
				if (nSend > 0)
				{
					nNum += nSend;//发送成功
				}
				else
				{
					std::string ErrMsg = GetError(WSAGetLastError());
					printf("                                ThreadSend:connection lost：%s \r\n", ErrMsg.c_str());
					WriteSendLogs("数据发送失败后，准备重连总部服务端...");
					bCurSendStatus = false;
					if(sockfd > 0 )
					{
						int nCloseRes = closesocket(sockfd);
						while(nCloseRes != 0)//关闭失败 则继续等待关闭
						{					
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
							WriteSendLogs("发送数据：重连关闭套接字...nCloseRes:%d",nCloseRes);
							nCloseRes = closesocket(sockfd);
						}
		
						WriteSendLogs("发送数据：重连关闭套接字...nCloseRes:%d",nCloseRes);
						printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
						std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
					}
					if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
					{
						//备用
					}
					setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
					setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
					std::this_thread::sleep_for(std::chrono::milliseconds(500));	
					while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
					{
						if(sockfd > 0 )
						{
							int nCloseRes = closesocket(sockfd);
							while(nCloseRes != 0)//关闭失败 则继续等待关闭
							{					
								std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
								WriteSendLogs("发送数据：重连关闭套接字...nCloseRes:%d",nCloseRes);
								nCloseRes = closesocket(sockfd);
							}

							WriteSendLogs("发送数据：重连关闭套接字...nCloseRes:%d",nCloseRes);
							printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
						}
						
						if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
						{
							//备用
						}
						setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
						setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
						WriteSendLogs("数据发送失败后，重连总部服务端...");
						printf("                                ThreadSend:reconnection... \r\n");
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
					}
					WriteSendLogs("数据发送失败后，重连总部服务端成功...");
					printf("                                ThreadSend:reconnection success \r\n");
					bCurSendStatus = true;
					nErrorCount = 0;
					bTcpReconnect = true;
					break;					
				}//end of else
				

			} // end of while 发送数据结束

			if (bTcpReconnect)
			{
				bTcpReconnect = false;
				continue;
			}

			int tmpLen = m_rsData.len;
			uLongf comprLen = tmpLen * 50;
			char recvAfterUn[LEN_RECV];
			int err2 = uncompress((Bytef*)recvAfterUn, &comprLen, (const Bytef*)m_rsData.c, tmpLen);
			if (err2 != 0)
			{
				WriteSendLogs("解压错误");
				printf("                                ThreadSend:uncompress error \r\n");
			}

			// //解析报文中的时间
			char cTime[24] = { 0 };
			int nTime[6] = { 0 };//年月日十分秒
			int* pTime = nTime;
			U4 uu4;
			for (int i = 0; i < LEN_TIME; i += 4)
			{
				int nLocation = i;
				uu4.c[0] = recvAfterUn[nLocation];
				uu4.c[1] = recvAfterUn[nLocation + 1];
				uu4.c[2] = recvAfterUn[nLocation + 2];
				uu4.c[3] = recvAfterUn[nLocation + 3];
				*pTime++ = (int)uu4.i;
			}
			pTime = nTime;

			memset(cTime, 0, 24);
			const char* pcFormat_Time_Kafka = "%04d-%02d-%02d %02d:%02d:%02d";
			sprintf(cTime, pcFormat_Time_Kafka, pTime[5], pTime[4], pTime[3], pTime[2], pTime[1], pTime[0]);
			printf("                                send : [%s]\r\n",cTime);
			WriteSendLogs("数据发送完成，原长%d，解压后长度 %d \r 时间是【%s】", tmpLen, comprLen, cTime);

			MyQueue->try_pop(m_rsData);

		}//end of else 

	}//end of while 结束大循环
}



//函数功能：供线程直接调用的函数，用于删除过期文件或者日志
void Thread_DelOldFile()
{
	const int nDay_HistoryData = 14;	//历史数据存放天数
	const int nDay_Logs = 30;	//日志存放天数

	const int nCycle = 60;	//大循环周期 单位秒
	time_t CurTime = 0, PreTime = 0;
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		while (CurTime - PreTime < nCycle)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			time(&CurTime);
		}
		PreTime = CurTime;

		int nDelTime_HistoryData = GetDelTime(nDay_HistoryData);
		int nDelTime_Logs = GetDelTime(nDay_Logs);
#ifdef _WIN32
		DelHistoryLogs("RecvLogs", nDelTime_Logs);
		DelHistoryLogs("SendLogs", nDelTime_Logs);
#else
		std::string strTmpPath = "SendLogs/";
		DelHistoryLogs(strTmpPath, nDelTime_Logs);
		
		strTmpPath = "RecvLogs/";
		DelHistoryLogs(strTmpPath, nDelTime_Logs);
		
		if (remove("printf.file") == 0)
		{

		}
		
#endif
	}//end of while
}


//函数功能：获得删除时间，获得nDays天前的时间（年月日时），以整型数据表示
//输入参数：nDays（天数）
//返 回 值：整型数据表示的时间 例如：2018030204代表2018年3月2日4点
int  GetDelTime(const int nDays)
{
	time_t t = time(0);
	tm curtime;
	int size1 = sizeof(curtime);
	int size2 = sizeof(tm);
	char tmp[64];
	curtime = *localtime(&t);
	//将格式后的时间存入tmp
	strftime(tmp, sizeof(tmp), "%Y-%m-%d %X", &curtime);

	std::string strtxt(tmp);
	std::string strtxt1 = strtxt.substr(0, 4) + strtxt.substr(5, 2) +
		strtxt.substr(8, 2) + strtxt.substr(11, 2);
	int nTime = atoi(strtxt1.c_str());

	int tmpTime = nTime;
	int hour = tmpTime % 100;

	tmpTime = nTime / 100;
	int day = tmpTime % 100;

	tmpTime = nTime / 10000;
	int month = tmpTime % 100;

	int year = nTime / 1000000;

	tm info = { 0 };
	info.tm_year = year - 1900;
	info.tm_mon = month - 1;
	info.tm_mday = day;
	info.tm_hour = hour;
	time_t t_ = mktime(&info) - nDays * 3600 * 24;
	tm tm_ = *localtime(&t_);
	int rs;
	rs = (tm_.tm_year + 1900) * 1000000 + (tm_.tm_mon + 1) * 10000 + (tm_.tm_mday) * 100 + (tm_.tm_hour);

	return rs;
}

//函数功能：获得字符串中前n个数字
//输入参数：strData（字符串），n（控制获得多少个数字） 
//返 回 值：返回获得的数字（整型） 
int GetNumFromString(std::string strData, const int n)
{
	char c;
	std::string strNum = "";
	int i = 0;
	for (std::string::iterator s_iter = strData.begin(); s_iter != strData.end(); ++s_iter)
	{
		c = *s_iter;
		if (c >= '0' && c <= '9')
		{
			i++;
			if (i <= n)
			{
				strNum = strNum + c;
			}
		}
	}

	int num = atoi(strNum.c_str());
	return num;
}

//函数功能：函数ThreadDelOldFile()的调用函数
//输入参数：filePath（文件路径），DelTime用于判断是否删除历史日志文件 
void DelHistoryLogs(const std::string filePath, const int DelTime)
{
	std::vector<std::string> vec_str_TxtFiles;//存放txt文件名称
	vec_str_TxtFiles.clear();

	//获得filePath下 一级子文件中的 文件夹 或 txt文件
	GetLogsTxtFiles(filePath, vec_str_TxtFiles);

	//注：日志文件的filePath下是且仅是txt，名称为 年月日 构成，如20180302
	//删除过期 日志文件
	int nSize = vec_str_TxtFiles.size();
	for (int i = 0; i < nSize; i++)
	{
		std::string strCurFileName = vec_str_TxtFiles[i];

		int nTimeInFileName = GetNumFromString(strCurFileName, 10);

		int nNewDeltime = DelTime;
		if (nTimeInFileName < nNewDeltime)//从文件中取得的时间早于删除时间
		{
#ifdef _WIN32
			std::string strDelFileName = filePath + "/" + strCurFileName;

#else
			std::string strDelFileName = strCurFileName;

#endif
			if (remove(strDelFileName.c_str()) == 0)
			{

			}
			else
			{
				perror("Removed error.");
				printf("%s \n\r", strDelFileName.c_str());
			}
		}
	}
}

//函数功能：函数ThreadDelOldFile()的调用函数
//输入参数：filePath（文件路径），DelTime用于判断是否删除历史数据文件
void DelHistoryDataFiles(const std::string filePath, const int DelTime)
{
	std::vector<std::string> vec_str_Folder;//存放文件夹名称
	vec_str_Folder.clear();

	//获得filePath下 一级子文件中的 文件夹 或 txt文件
	GetHistoryDataFolders(filePath, vec_str_Folder);

	//注：数据文件的filePath的一级子目录全是文件夹，名称为 年月日时 构成，如2018030205
	//删除过期 数据文件
	int nSize = vec_str_Folder.size();
	for (int i = 0; i < nSize; i++)
	{
		std::string strCurFileName = vec_str_Folder[i];
		
		int nTimeInFileName = GetNumFromString(strCurFileName, 10);
		if (nTimeInFileName < DelTime)//从文件中取得的时间早于删除时间
		{
#ifdef _WIN32
			std::string strDelFileName = filePath + "/" + strCurFileName;

#else
			std::string strDelFileName = strCurFileName;

#endif
			DoRemoveFile(strDelFileName);
		}
	}
}

#ifdef _WIN32

//函数功能：写日志，将发送数据情况写入日志
void WriteSendLogs(const char*  Format, ...)
{
	//获得时间
	time_t t = time(0);
	tm curtime;
	int size1 = sizeof(curtime);
	int size2 = sizeof(tm);
	char cDataTime[64];//存放日期时间
	localtime_s(&curtime, &t);

	strftime(cDataTime, sizeof(cDataTime), "%Y-%m-%d %X", &curtime);
	std::string strDateTime(cDataTime);//存放日期时间

	//获得 年月日 组成的日期数字字符串
	std::string strDate = strDateTime.substr(0, 4) + strDateTime.substr(5, 2) +
		strDateTime.substr(8, 2);

	char m_strLogs[1024];
	memset(m_strLogs, 0, 1024);

	if (Format)
	{
		va_list args;
		va_start(args, Format);
		vsprintf_s(m_strLogs, Format, args);
		va_end(args);
	}

	FILE* pFile;

	std::string strNewPath = "./SendLogs/SendLogs_" + strDate + ".txt";
	const char* cNewPath = strNewPath.c_str();

	//创建文件 打开该文件并且无误打开
	createDirectory(strNewPath);
	errno_t bt = fopen_s(&pFile, cNewPath, "at+");;
	while (bt != 0)
	{
		bt = fopen_s(&pFile, cNewPath, "at+");
	}

	char text[1024] = "";//存储将要写入文件的内容
	sprintf_s(text, "[%s]%s \r\n", cDataTime, m_strLogs);

	fprintf(pFile, text);
	fclose(pFile);

	//处理日志容器
	//WaitForSingleObject(hMutex_vec_cstr_LogSendData, INFINITE);//上锁

	//int cSize = vec_cstr_LogSendData.size();
	//if (cSize > 1000)//界面显示的日志容器大小，默认1000行
	//{
	//	vec_cstr_LogSendData.erase(vec_cstr_LogSendData.begin());
	//}
	//CString cstmp = text;
	//vec_cstr_LogSendData.push_back(cstmp);

	//ReleaseMutex(hMutex_vec_cstr_LogSendData);//解锁
}

//函数功能：根据指定路径找到路径下一级的文件夹 
//输入参数：path（指定的路径）
//输出参数：files（存放路径下一级的文件夹名称）
//注：名称为path的相对路径名称
void GetHistoryDataFolders(const std::string path, std::vector<std::string>& files)
{
	//文件句柄  
	long   hFile = 0;
	//文件信息  
	struct _finddata_t fileinfo;
	std::string p;
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//如果是目录,加入列表  
			if ((fileinfo.attrib &  _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
				{
					std::string tmp = p.assign(fileinfo.name);
					if (tmp == "." || tmp == "..")
					{
						continue;
					}
					files.push_back(p.assign(fileinfo.name));
				}
			}
			else
			{
			}
		} while (_findnext(hFile, &fileinfo) == 0);
		_findclose(hFile);

	}//end of if
}

//函数功能：根据指定路径找到路径下一级的txt文件
//输入参数：path（指定的路径）
//输出参数：txtFiles（存放路径下一级的txt文件名称）
//注：名称为path的相对路径名称
void GetLogsTxtFiles(const std::string path, std::vector<std::string>& txtFiles)
{
	//文件句柄  
	long   hFile = 0;
	//文件信息  
	struct _finddata_t fileinfo;
	std::string p;
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//如果是目录,加入列表  
			if ((fileinfo.attrib &  _A_SUBDIR))
			{

			}
			else
			{
				std::string strFileName = fileinfo.name;
				int nIndex = strFileName.find_last_of(".");
				std::string strFileType = strFileName.substr(nIndex);
				if (strFileType == ".txt")
				{
					txtFiles.push_back(p.assign(fileinfo.name));
				}
			}
		} while (_findnext(hFile, &fileinfo) == 0);
		_findclose(hFile);
	}
}

//函数功能：根据指定路径，将该路径删除包括本路径及本路径下所有文件或文件夹
//输入参数：filePath（指定的路径）
void DoRemoveFile(const std::string filePath)
{
	long   hFile = 0;//文件句柄

	struct _finddata_t fileinfo;//文件信息 

	std::string p;
	if ((hFile = _findfirst(p.assign(filePath).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//获得子目录
			if ((fileinfo.attrib &  _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
				{
					//如果子目录含有子目录，则递归调用本函数
					DoRemoveFile(p.assign(filePath).append("\\").append(fileinfo.name));
				}

				//不再含有子目录时，删除该子目录
				std::string delFileName = p.assign(filePath).append("\\").append(fileinfo.name);
				WCHAR wszClassName[256];
				memset(wszClassName, 0, sizeof(wszClassName));
				MultiByteToWideChar(CP_ACP, 0, delFileName.c_str(), strlen(delFileName.c_str()) + 1, wszClassName,
					sizeof(wszClassName) / sizeof(wszClassName[0]));
				bool bFlag = RemoveDirectory(wszClassName);
			}
			else
			{
				//删除最里层的文件
				std::string delFileName = p.assign(filePath).append("\\").append(fileinfo.name);
				int iFlag = remove(delFileName.c_str());
			}
		} while (_findnext(hFile, &fileinfo) == 0);

		_findclose(hFile);
	}
	//最终删除本目录
	WCHAR wszClassName[256];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, filePath.c_str(), strlen(filePath.c_str()) + 1, wszClassName,
		sizeof(wszClassName) / sizeof(wszClassName[0]));
	bool bFlag = RemoveDirectory(wszClassName);
}

#define ACCESS(fileName,accessMode) _access(fileName,accessMode)
#define MKDIR(path) _mkdir(path)
//函数功能：根据路径创建对应目录或文件
//输入参数：directoryPath（将被创建的路径）
//返 回 值：0代表创建成功，-1代表路径名称过长，其它数值代表创建目录遇到的错误码
int32_t createDirectory(const std::string &directoryPath)
{
	uint32_t dirPathLen = directoryPath.length();
	if (dirPathLen > 256)
	{
		return -1;//过长则返回
	}

	char tmpDirPath[256] = { 0 };
	for (uint32_t i = 0; i < dirPathLen; ++i)
	{
		tmpDirPath[i] = directoryPath[i];
		if (tmpDirPath[i] == '\\' || tmpDirPath[i] == '/')//目录需一级一级创建
		{
			if (ACCESS(tmpDirPath, 0) != 0)//不存在该目录则创建
			{
				int32_t ret = _mkdir(tmpDirPath);
				if (ret != 0)
				{
					return ret;
				}
			}
		}
	}//end of for

	return 0;
}

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

////函数功能：将使用tcp协议时得到的整型错误数值转换为字符串描述
////输入参数：nError（使用tcp协议时得到的整型错误数值）
////返 回 值：参数nError对应的字符串描述
std::string GetError(const int nError)
{
	HLOCAL LocalAddress = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, nError, 0, (PTSTR)&LocalAddress, 0, NULL);
	char cTemp[1000] = { 0 };
	strcpy(cTemp, (char*)LocalAddress);
	std::wstring strError((LPCTSTR)(PTSTR)LocalAddress);
	std::string strRs = ws2s(strError);
	return strRs;
}

#else
//函数功能：函数ThreadDelOldFile()的调用函数
//输入参数：filePath（文件路径），DelTime用于判断是否删除历史日志文件 
void WriteSendLogs(const char* Format, ...)
{
	std::string strLogPath = strPath + "SendLogs/";
	static int n = 0;
	int buflen = 5120;
	char buf[buflen];
	int i = 0;
	memset(buf, 0, buflen);
	va_list args;
	va_start(args, Format);
	vsnprintf(buf, buflen, Format, args);
	va_end(args);

	//printf("%s\n", buf);
	FILE* logfile = NULL;
	char logpath[128] = { 0 };
	snprintf(logpath, sizeof(logpath), strLogPath.c_str());
	if (access(logpath, 0) != 0)
	{
		char cmdstr[256] = { 0 };
		sprintf(cmdstr, "mkdir -p %s", logpath);
		system(cmdstr);
	}

	char fname[512];
	char longtime[200];
	time_t t;
	memset(fname, 0, sizeof(fname));
	time(&t);
	struct tm local = { 0 };
	localtime_r(&t, &local);

	sprintf(longtime, "%04d-%02d-%02d %02d", local.tm_year + 1900,
		local.tm_mon + 1, local.tm_mday, local.tm_hour);
	sprintf(fname, "%slog_%s_%d.txt", logpath, longtime, n);

	sprintf(longtime, "[%04d-%02d-%02d %02d:%02d:%02d]", local.tm_year + 1900,
		local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);

	for (i = 0; i < 3; i++)
	{
		logfile = fopen(fname, "ab");
		if (logfile)
			break;

	}
	if (logfile)
	{
		// 判断文件大小
		if (ftell(logfile) < 5 * 1024 * 1024)   // 5M
		{
			//日期时间
			const char* pTemp = longtime;
			fwrite(pTemp, 1, strlen(pTemp), logfile);
			fwrite(" ", 1, 1, logfile);
			//内容
			fwrite(buf, 1, strlen(buf), logfile);
			fwrite(" \r\n", 1, 3, logfile);
			fclose(logfile);
		}
		else
		{
			fclose(logfile);
			n++;
			//remove(fname);
		}
	}
}

void DoRemoveFile(std::string filePath)
{
	//文件句柄  
	long   hFile = 0;
	//文件信息  
	std::string tmpPath = filePath + "/";
	struct dirent *direntp;
	DIR *dirp = opendir(tmpPath.c_str());

	if (dirp != NULL)
	{
		while ((direntp = readdir(dirp)) != NULL)
		{

			std::string tmp(direntp->d_name);
			if (tmp == "." || tmp == "..")
			{
				continue;
			}
			std::string newPath = "";
			newPath = tmpPath + tmp;
			DoRemoveFile(newPath);

		}
		if (remove(filePath.c_str()) == 0)
		{

		}
		else
		{
			perror("Removed error.");
			printf("%s \n\r", filePath.c_str());
		}

	}
	else
	{
		if (remove(filePath.c_str()) == 0)
		{

		}
		else
		{
			perror("Removed error.");
			printf("%s \n\r", filePath.c_str());
		}
	}
	closedir(dirp);


}


int mkpath(std::string s, mode_t mode)
{
	size_t pre = 0, pos;
	std::string dir;
	int mdret;

	if (s[s.size() - 1] != '/')
	{
		// force trailing / so we can handle everything in loop  
		s += '/';
	}

	while ((pos = s.find_first_of('/', pre)) != std::string::npos)
	{
		dir = s.substr(0, pos++);
		pre = pos;
		if (dir.size() == 0) continue; // if leading / first time is 0 length  
		if ((mdret = ::mkdir(dir.c_str(), mode)) && errno != EEXIST)
		{
			return mdret;
		}
	}
	return mdret;
}

void GetLogsTxtFiles(std::string path, std::vector<std::string>& txtFiles)
{
	std::string tmpPath = path + "/";
	struct dirent *direntp;
	DIR *dirp = opendir(tmpPath.c_str());

	if (dirp != NULL)
	{
		while ((direntp = readdir(dirp)) != NULL)
		{
			std::string tmp(direntp->d_name);
			if (tmp == "." || tmp == "..")
			{
				continue;
			}
			if (tmp.find("txt") != -1)
			{
				txtFiles.push_back(tmpPath + tmp);
			}
		}
	}
	closedir(dirp);
}

void GetHistoryDataFolders(std::string path, std::vector<std::string>& files)
{
	std::string tmpPath = path + "/";
	struct dirent *direntp;
	DIR *dirp = opendir(tmpPath.c_str());

	if (dirp != NULL)
	{
		while ((direntp = readdir(dirp)) != NULL)
		{
			std::string tmp(direntp->d_name);
			if (tmp == "." || tmp == "..")
			{
				continue;
			}
			if (tmp.find("txt") != -1)
			{
				continue;
			}
			files.push_back(tmpPath + tmp);
		}
	}

	closedir(dirp);
}

#endif