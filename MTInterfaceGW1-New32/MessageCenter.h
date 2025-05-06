#pragma once
#include "MT4Manager.h"

#define OUT_MSG_LEN     128*1024
#define IN_BUFF_LEN     64*1024
#define IN_MSG_LEN      1024

class MessageCenter
{
public:
	MessageCenter(void);
	~MessageCenter(void);
	bool Init();
	static UINT AcceptHandler(VOID* param);
	static VOID NTAPI MessageHandler(PTP_CALLBACK_INSTANCE Instance,PVOID Context);
	static UINT PingHandler(VOID* param);
	const static UINT MAX_THREAD_NUM=20;
	const static UINT SOCKET_TIMEOUT=2000;//2�볬ʱ

	SOCKET   m_serverSkt;
};

extern MessageCenter mCenter;
int GetIntParam(LPCSTR string,LPCSTR param,int *data);
int GetFltParam(LPCSTR string,LPCSTR param,double *data);
int GetStrParam(LPCSTR string,LPCSTR param,char *buf,const int maxlen);
int CheckPassword(LPCSTR password,int passwordNum=5);

