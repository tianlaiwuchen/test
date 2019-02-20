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

//���ڷ��ͱ�ͷ���������� ������������Ϣ
union U4
{
	float v;
	unsigned char c[4];
	unsigned int i;
}uu4;

//��cpp�ļ�ȫ�ֱ�������������
#ifdef _WIN32

std::vector<CString> vec_cstr_LogSendData;	//�������ݵ���־����


#endif

//�ⲿ�ļ�ȫ�ֱ�������������������
extern bool bCurSendStatus;	//����״̬
extern int nRecvCount;	//��¼����vec_rsData_RecvData�ĵ�ǰ��Ŀ
extern std::string strSendIp;	//�ܲ������Ip
extern std::string strPath;//����·��
extern std::vector<rsData> vec_rsData_RecvData;	//�洢�����߳��յ�������

//extern HANDLE hMutex_vec_rsData_RecvData;	//������ԴΪ �洢�����߳��յ�������
//extern HANDLE hMutex_vec_cstr_LogSendData;	//������ԴΪ �������ݵ���־��Ϣ
extern std::mutex mut;
//extern std::unique_lock<std::mutex> lk(mut);



//�������ܣ����߳�ֱ�ӵ��õĺ��������ڷ�������
//���������nSendPort �ܲ�����˽��յĶ˿ڣ��ܲ�����˵�Ip����ȫ�ֱ���strSendIp����
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
	serv_addr.sin_port = htons(nSendPort);//�ܲ����ն˵Ķ˿�
	serv_addr.sin_addr.s_addr = inet_addr(strSendIp.c_str());//�ܲ����ն˵�ip
	
	int len = sizeof(serv_addr);
	int timeout = 1000 * 3;		//1000=1�� ��ʱʱ��
	int nSendBuf = 32 * 1024;	//���ջ��������� ��λB

	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));

	
	while (connect(sockfd, (struct sockaddr *)&serv_addr,
		sizeof(struct sockaddr)) == -1)
	{

		std::string ErrMsg = GetError(WSAGetLastError());
		WriteSendLogs("��������ǰ�����ӷ����ʧ�ܣ�ԭ��%s", ErrMsg.c_str());
		printf("                                ThreadSend:connect failed : %s \r\n", ErrMsg.c_str());
		closesocket(sockfd);
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		{
			//����
		}
		setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
		setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
		
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	}

	bCurSendStatus = true;

	char HeadBuf[LEN_HEAD] = { 0 };	//�洢��ͷ����	
	int nErrorCount = 0;	//�ۼƽ��ղ��ɹ�����

	int nSend = -1; // ��¼���η��͵�������Ŀ С��0����Ϊʧ��
	bool bTcpReconnect = false;	//��¼�Ƿ����� tcp�����ɹ�

	rsData m_Heartpacket;
	m_Heartpacket.len = 0;
	int nHeart = 0;//�������� ����3�������ݷ��� ����������
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
		//��÷���������������Ŀ		
		else
		{
			nHeart = 0;
			rsData m_rsData;			
			bool b = MyQueue->try_front(m_rsData);
			if(!b)
			{
				continue;
			}
			int nLen = m_rsData.len; //��ͷ������

			uu4.i = nLen;
			HeadBuf[0 + 3] = uu4.c[3];
			HeadBuf[0 + 2] = uu4.c[2];
			HeadBuf[0 + 1] = uu4.c[1];
			HeadBuf[0] = uu4.c[0];

			//���͵�һ����ͷ
			int nNum = 0;	//��¼�����������ѷ��͵������ֽ���

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
					WriteSendLogs("��ͷ����ʧ�ܣ����ݳ���%d��ԭ��%s", nLen, ErrMsg.c_str());
					printf("                                ThreadSend:connection lost:%s \r\n", ErrMsg.c_str());
					bCurSendStatus = false;
					
					if(sockfd > 0 )
					{
						int nCloseRes = closesocket(sockfd);
						while(nCloseRes != 0)//�ر�ʧ�� ������ȴ��ر�
						{					
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
							WriteSendLogs("���ͱ�ͷ���ر��׽���...nCloseRes:%d",nCloseRes);
							nCloseRes = closesocket(sockfd);
						}

						WriteSendLogs("���ͱ�ͷ���ر��׽���...nCloseRes:%d",nCloseRes);
						printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
						std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
					}
					
					if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
					{
						//����
					}
					setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
					setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
					std::this_thread::sleep_for(std::chrono::milliseconds(500));	
					while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
					{
						if(sockfd > 0 )
						{
							int nCloseRes = closesocket(sockfd);
							while(nCloseRes != 0)//�ر�ʧ�� ������ȴ��ر�
							{					
								std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
								WriteSendLogs("���ͱ�ͷ�������ر��׽���...nCloseRes:%d",nCloseRes);
								nCloseRes = closesocket(sockfd);
							}

							WriteSendLogs("���ͱ�ͷ�������ر��׽���...nCloseRes:%d",nCloseRes);
							printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
						}
						if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
						{
							//����
						}
						setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
						setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
						WriteSendLogs("��ͷ����ʧ�ܺ������ܲ������...");
						printf("                                ThreadSend:reconnection... \r\n");
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
					}
					WriteSendLogs("��ͷ����ʧ�ܺ������ܲ�����˳ɹ�...");
					printf("                                ThreadSend:reconnection success \r\n");
					bCurSendStatus = true;
					nErrorCount = 0;
					bTcpReconnect = true;
					break;	
				}
				

			} // end of while ���ͱ�ͷ����
			if (bTcpReconnect)
			{
				bTcpReconnect = false;
				continue;
			}
			WriteSendLogs("��ͷ������ɣ����ݳ���%d", nLen );
			
			if(nLen == 0)//���͵�������
			{
				MyQueue->try_pop(m_rsData);
				continue;
			}
			//��������
			nNum = 0;
			while (nLen > nNum)
			{
				nSend = send(sockfd, m_rsData.c + nNum, nLen - nNum, 0);
				if (nSend > 0)
				{
					nNum += nSend;//���ͳɹ�
				}
				else
				{
					std::string ErrMsg = GetError(WSAGetLastError());
					printf("                                ThreadSend:connection lost��%s \r\n", ErrMsg.c_str());
					WriteSendLogs("���ݷ���ʧ�ܺ�׼�������ܲ������...");
					bCurSendStatus = false;
					if(sockfd > 0 )
					{
						int nCloseRes = closesocket(sockfd);
						while(nCloseRes != 0)//�ر�ʧ�� ������ȴ��ر�
						{					
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
							WriteSendLogs("�������ݣ������ر��׽���...nCloseRes:%d",nCloseRes);
							nCloseRes = closesocket(sockfd);
						}
		
						WriteSendLogs("�������ݣ������ر��׽���...nCloseRes:%d",nCloseRes);
						printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
						std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
					}
					if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
					{
						//����
					}
					setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
					setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
					std::this_thread::sleep_for(std::chrono::milliseconds(500));	
					while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
					{
						if(sockfd > 0 )
						{
							int nCloseRes = closesocket(sockfd);
							while(nCloseRes != 0)//�ر�ʧ�� ������ȴ��ر�
							{					
								std::this_thread::sleep_for(std::chrono::milliseconds(3000));					
								WriteSendLogs("�������ݣ������ر��׽���...nCloseRes:%d",nCloseRes);
								nCloseRes = closesocket(sockfd);
							}

							WriteSendLogs("�������ݣ������ر��׽���...nCloseRes:%d",nCloseRes);
							printf("                                thread reconnect...nCloseRes:%d\r\n",nCloseRes);
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));	
						}
						
						if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
						{
							//����
						}
						setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
						setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
						WriteSendLogs("���ݷ���ʧ�ܺ������ܲ������...");
						printf("                                ThreadSend:reconnection... \r\n");
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
					}
					WriteSendLogs("���ݷ���ʧ�ܺ������ܲ�����˳ɹ�...");
					printf("                                ThreadSend:reconnection success \r\n");
					bCurSendStatus = true;
					nErrorCount = 0;
					bTcpReconnect = true;
					break;					
				}//end of else
				

			} // end of while �������ݽ���

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
				WriteSendLogs("��ѹ����");
				printf("                                ThreadSend:uncompress error \r\n");
			}

			// //���������е�ʱ��
			char cTime[24] = { 0 };
			int nTime[6] = { 0 };//������ʮ����
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
			WriteSendLogs("���ݷ�����ɣ�ԭ��%d����ѹ�󳤶� %d \r ʱ���ǡ�%s��", tmpLen, comprLen, cTime);

			MyQueue->try_pop(m_rsData);

		}//end of else 

	}//end of while ������ѭ��
}



//�������ܣ����߳�ֱ�ӵ��õĺ���������ɾ�������ļ�������־
void Thread_DelOldFile()
{
	const int nDay_HistoryData = 14;	//��ʷ���ݴ������
	const int nDay_Logs = 30;	//��־�������

	const int nCycle = 60;	//��ѭ������ ��λ��
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


//�������ܣ����ɾ��ʱ�䣬���nDays��ǰ��ʱ�䣨������ʱ�������������ݱ�ʾ
//���������nDays��������
//�� �� ֵ���������ݱ�ʾ��ʱ�� ���磺2018030204����2018��3��2��4��
int  GetDelTime(const int nDays)
{
	time_t t = time(0);
	tm curtime;
	int size1 = sizeof(curtime);
	int size2 = sizeof(tm);
	char tmp[64];
	curtime = *localtime(&t);
	//����ʽ���ʱ�����tmp
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

//�������ܣ�����ַ�����ǰn������
//���������strData���ַ�������n�����ƻ�ö��ٸ����֣� 
//�� �� ֵ�����ػ�õ����֣����ͣ� 
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

//�������ܣ�����ThreadDelOldFile()�ĵ��ú���
//���������filePath���ļ�·������DelTime�����ж��Ƿ�ɾ����ʷ��־�ļ� 
void DelHistoryLogs(const std::string filePath, const int DelTime)
{
	std::vector<std::string> vec_str_TxtFiles;//���txt�ļ�����
	vec_str_TxtFiles.clear();

	//���filePath�� һ�����ļ��е� �ļ��� �� txt�ļ�
	GetLogsTxtFiles(filePath, vec_str_TxtFiles);

	//ע����־�ļ���filePath�����ҽ���txt������Ϊ ������ ���ɣ���20180302
	//ɾ������ ��־�ļ�
	int nSize = vec_str_TxtFiles.size();
	for (int i = 0; i < nSize; i++)
	{
		std::string strCurFileName = vec_str_TxtFiles[i];

		int nTimeInFileName = GetNumFromString(strCurFileName, 10);

		int nNewDeltime = DelTime;
		if (nTimeInFileName < nNewDeltime)//���ļ���ȡ�õ�ʱ������ɾ��ʱ��
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

//�������ܣ�����ThreadDelOldFile()�ĵ��ú���
//���������filePath���ļ�·������DelTime�����ж��Ƿ�ɾ����ʷ�����ļ�
void DelHistoryDataFiles(const std::string filePath, const int DelTime)
{
	std::vector<std::string> vec_str_Folder;//����ļ�������
	vec_str_Folder.clear();

	//���filePath�� һ�����ļ��е� �ļ��� �� txt�ļ�
	GetHistoryDataFolders(filePath, vec_str_Folder);

	//ע�������ļ���filePath��һ����Ŀ¼ȫ���ļ��У�����Ϊ ������ʱ ���ɣ���2018030205
	//ɾ������ �����ļ�
	int nSize = vec_str_Folder.size();
	for (int i = 0; i < nSize; i++)
	{
		std::string strCurFileName = vec_str_Folder[i];
		
		int nTimeInFileName = GetNumFromString(strCurFileName, 10);
		if (nTimeInFileName < DelTime)//���ļ���ȡ�õ�ʱ������ɾ��ʱ��
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

//�������ܣ�д��־���������������д����־
void WriteSendLogs(const char*  Format, ...)
{
	//���ʱ��
	time_t t = time(0);
	tm curtime;
	int size1 = sizeof(curtime);
	int size2 = sizeof(tm);
	char cDataTime[64];//�������ʱ��
	localtime_s(&curtime, &t);

	strftime(cDataTime, sizeof(cDataTime), "%Y-%m-%d %X", &curtime);
	std::string strDateTime(cDataTime);//�������ʱ��

	//��� ������ ��ɵ����������ַ���
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

	//�����ļ� �򿪸��ļ����������
	createDirectory(strNewPath);
	errno_t bt = fopen_s(&pFile, cNewPath, "at+");;
	while (bt != 0)
	{
		bt = fopen_s(&pFile, cNewPath, "at+");
	}

	char text[1024] = "";//�洢��Ҫд���ļ�������
	sprintf_s(text, "[%s]%s \r\n", cDataTime, m_strLogs);

	fprintf(pFile, text);
	fclose(pFile);

	//������־����
	//WaitForSingleObject(hMutex_vec_cstr_LogSendData, INFINITE);//����

	//int cSize = vec_cstr_LogSendData.size();
	//if (cSize > 1000)//������ʾ����־������С��Ĭ��1000��
	//{
	//	vec_cstr_LogSendData.erase(vec_cstr_LogSendData.begin());
	//}
	//CString cstmp = text;
	//vec_cstr_LogSendData.push_back(cstmp);

	//ReleaseMutex(hMutex_vec_cstr_LogSendData);//����
}

//�������ܣ�����ָ��·���ҵ�·����һ�����ļ��� 
//���������path��ָ����·����
//���������files�����·����һ�����ļ������ƣ�
//ע������Ϊpath�����·������
void GetHistoryDataFolders(const std::string path, std::vector<std::string>& files)
{
	//�ļ����  
	long   hFile = 0;
	//�ļ���Ϣ  
	struct _finddata_t fileinfo;
	std::string p;
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//�����Ŀ¼,�����б�  
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

//�������ܣ�����ָ��·���ҵ�·����һ����txt�ļ�
//���������path��ָ����·����
//���������txtFiles�����·����һ����txt�ļ����ƣ�
//ע������Ϊpath�����·������
void GetLogsTxtFiles(const std::string path, std::vector<std::string>& txtFiles)
{
	//�ļ����  
	long   hFile = 0;
	//�ļ���Ϣ  
	struct _finddata_t fileinfo;
	std::string p;
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//�����Ŀ¼,�����б�  
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

//�������ܣ�����ָ��·��������·��ɾ��������·������·���������ļ����ļ���
//���������filePath��ָ����·����
void DoRemoveFile(const std::string filePath)
{
	long   hFile = 0;//�ļ����

	struct _finddata_t fileinfo;//�ļ���Ϣ 

	std::string p;
	if ((hFile = _findfirst(p.assign(filePath).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//�����Ŀ¼
			if ((fileinfo.attrib &  _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
				{
					//�����Ŀ¼������Ŀ¼����ݹ���ñ�����
					DoRemoveFile(p.assign(filePath).append("\\").append(fileinfo.name));
				}

				//���ٺ�����Ŀ¼ʱ��ɾ������Ŀ¼
				std::string delFileName = p.assign(filePath).append("\\").append(fileinfo.name);
				WCHAR wszClassName[256];
				memset(wszClassName, 0, sizeof(wszClassName));
				MultiByteToWideChar(CP_ACP, 0, delFileName.c_str(), strlen(delFileName.c_str()) + 1, wszClassName,
					sizeof(wszClassName) / sizeof(wszClassName[0]));
				bool bFlag = RemoveDirectory(wszClassName);
			}
			else
			{
				//ɾ���������ļ�
				std::string delFileName = p.assign(filePath).append("\\").append(fileinfo.name);
				int iFlag = remove(delFileName.c_str());
			}
		} while (_findnext(hFile, &fileinfo) == 0);

		_findclose(hFile);
	}
	//����ɾ����Ŀ¼
	WCHAR wszClassName[256];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, filePath.c_str(), strlen(filePath.c_str()) + 1, wszClassName,
		sizeof(wszClassName) / sizeof(wszClassName[0]));
	bool bFlag = RemoveDirectory(wszClassName);
}

#define ACCESS(fileName,accessMode) _access(fileName,accessMode)
#define MKDIR(path) _mkdir(path)
//�������ܣ�����·��������ӦĿ¼���ļ�
//���������directoryPath������������·����
//�� �� ֵ��0�������ɹ���-1����·�����ƹ�����������ֵ������Ŀ¼�����Ĵ�����
int32_t createDirectory(const std::string &directoryPath)
{
	uint32_t dirPathLen = directoryPath.length();
	if (dirPathLen > 256)
	{
		return -1;//�����򷵻�
	}

	char tmpDirPath[256] = { 0 };
	for (uint32_t i = 0; i < dirPathLen; ++i)
	{
		tmpDirPath[i] = directoryPath[i];
		if (tmpDirPath[i] == '\\' || tmpDirPath[i] == '/')//Ŀ¼��һ��һ������
		{
			if (ACCESS(tmpDirPath, 0) != 0)//�����ڸ�Ŀ¼�򴴽�
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

//�������ܣ���wstringת��Ϊstring
//���������ws��ԭwstring��
//�� �� ֵ��wsת�����string
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

////�������ܣ���ʹ��tcpЭ��ʱ�õ������ʹ�����ֵת��Ϊ�ַ�������
////���������nError��ʹ��tcpЭ��ʱ�õ������ʹ�����ֵ��
////�� �� ֵ������nError��Ӧ���ַ�������
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
//�������ܣ�����ThreadDelOldFile()�ĵ��ú���
//���������filePath���ļ�·������DelTime�����ж��Ƿ�ɾ����ʷ��־�ļ� 
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
		// �ж��ļ���С
		if (ftell(logfile) < 5 * 1024 * 1024)   // 5M
		{
			//����ʱ��
			const char* pTemp = longtime;
			fwrite(pTemp, 1, strlen(pTemp), logfile);
			fwrite(" ", 1, 1, logfile);
			//����
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
	//�ļ����  
	long   hFile = 0;
	//�ļ���Ϣ  
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