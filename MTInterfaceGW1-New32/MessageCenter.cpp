#include "StdAfx.h"
#include "MessageCenter.h"
#include "MT4Manager.h"

int GetIntParam(LPCSTR string,LPCSTR param,int *data)
  {
//---- checks
   if(string==NULL || param==NULL || data==NULL) return(FALSE);
//---- find
   if((string=strstr(string,param))==NULL)       return(FALSE);
//---- all right
   while(string[strlen(param)] < '0' || string[strlen(param)] > '9')
   {
	   if((string=strstr(string+strlen(param),param))==NULL)       return(FALSE);
   }

   *data=atoi(&string[strlen(param)]);
   return(TRUE);
  }
//+------------------------------------------------------------------+
//| Reading floating parameter                                       |
//+------------------------------------------------------------------+
int GetFltParam(LPCSTR string,LPCSTR param,double *data)
  {
//---- checks
   if(string==NULL || param==NULL || data==NULL) return(FALSE);
//---- find
   if((string=strstr(string,param))==NULL)       return(FALSE);
//---- all right
   *data=atof(&string[strlen(param)]);
   return(TRUE);
  }
//+------------------------------------------------------------------+
//| Reading string parameter                                         |
//+------------------------------------------------------------------+
int GetStrParam(LPCSTR string,LPCSTR param,char *buf,const int maxlen)
  {
   int i=0;
//---- checks
   if(string==NULL || param==NULL || buf==NULL)  return(FALSE);
//---- find
   if((string=strstr(string,param))==NULL)       return(FALSE);
//---- receive result
   string+=strlen(param);
   while(*string!=0 && *string!='|' && i<maxlen) { *buf++=*string++; i++; }
   *buf=0;
//----
   return(TRUE);
  }

int CheckPassword(LPCSTR password,int passwordNum)
  {
   char   tmp[256];
   int    len,num=0,upper=0,lower=0;
   USHORT type[256];
//----
   if(password==NULL) return(FALSE);
//---- check len
   if((len=strlen(password))<5) return(FALSE);
//---- must Upper case,lower case and digits
   strcpy(tmp,password);
   if(GetStringTypeA(LOCALE_SYSTEM_DEFAULT,CT_CTYPE1,tmp,len,(USHORT*)type))
     {
      for(int i=0;i<len;i++)
        {
         if(type[i]&C1_DIGIT)  { num=1;   continue; }
         if(type[i]&C1_UPPER)  { upper=1; continue; }
         if(type[i]&C1_LOWER)  { lower=1; continue; }
         if(!(type[i] & (C1_ALPHA | C1_DIGIT) )) { num=2; break; }
        }
     }
//---- compute complexity
   return((num+upper+lower)>=2);
  }

MessageCenter::MessageCenter(void)
{
}


MessageCenter::~MessageCenter(void)
{
}

bool MessageCenter::Init()
{
	m_serverSkt = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if(INVALID_SOCKET==m_serverSkt)
		return false;

	SOCKADDR_IN saServer;
	saServer.sin_family = AF_INET;
	saServer.sin_addr.s_addr = htonl(INADDR_ANY);
	saServer.sin_port = htons(g_pManagerIF->GetPort());

	if(SOCKET_ERROR==bind(m_serverSkt,(SOCKADDR*)&saServer,sizeof(saServer)))
		return false;

	if(SOCKET_ERROR==listen(m_serverSkt,3))
		return false;

	AfxBeginThread(AcceptHandler,NULL);

	return true;
}

UINT MessageCenter::AcceptHandler(VOID* param)
{
	PTP_POOL ptpPool;
	TP_CALLBACK_ENVIRON tce;
	ptpPool = CreateThreadpool(NULL);
	SetThreadpoolThreadMaximum(ptpPool,MAX_THREAD_NUM);

	InitializeThreadpoolEnvironment(&tce);
	SetThreadpoolCallbackPool(&tce,ptpPool);

	while(true)
	{
		SOCKET clientSkt=INVALID_SOCKET;
		sockaddr_in client_addr;
	
		int length = sizeof(client_addr);
		clientSkt = accept(mCenter.m_serverSkt, (sockaddr*)&client_addr ,&length);
		if(clientSkt == INVALID_SOCKET) continue;

		int TimeOut=SOCKET_TIMEOUT;
		setsockopt(clientSkt,SOL_SOCKET,SO_SNDTIMEO,(char *)&TimeOut,sizeof(TimeOut));
		//setsockopt(clientSkt,SOL_SOCKET,SO_RCVTIMEO,(char *)&TimeOut,sizeof(TimeOut));
		BOOL bKeepAlive = TRUE;  
		setsockopt(clientSkt, SOL_SOCKET, SO_KEEPALIVE,(char*)&bKeepAlive, sizeof(bKeepAlive)); 
		
		
		cout<<"Socket#"<<clientSkt<<" is connected from "<<inet_ntoa(client_addr.sin_addr)
				<<":"<<client_addr.sin_port<<endl;
		
		TrySubmitThreadpoolCallback(MessageHandler,(PVOID)clientSkt,&tce);
	}

	DestroyThreadpoolEnvironment(&tce);
	CloseThreadpool(ptpPool);
	
	return TRUE;
}

UINT MessageCenter::PingHandler(VOID* param)
{
	g_pManagerIF->Ping();
	return TRUE;
}

VOID NTAPI MessageCenter::MessageHandler(PTP_CALLBACK_INSTANCE Instance,PVOID Context)
{
	SOCKET clientSkt=(SOCKET)Context;
	bool bRun = true;
	int ret;
	unsigned int outmsgLen = 0;//已经拷贝到发送消息缓冲区的字节数
	unsigned int inBytes = 0;//已经从读缓冲区读取的字节数
	unsigned int inmsgLen = 0;//已经拷贝到接收消息缓冲区的字节数
	char * outMsg = new char[OUT_MSG_LEN];
	char * buffer = new char[IN_BUFF_LEN];
	char * inMsg =  new char[IN_MSG_LEN];
	int packageLen = 0; //消息包长度

	while(bRun)
	{
		memset(buffer,0,IN_BUFF_LEN);
		inBytes = 0;
		ret = recv(clientSkt, buffer, IN_BUFF_LEN, 0);

		if(ret <= 0)
		{
			cout<<"Socket#"<<clientSkt<<" is closed!"<<endl;
			closesocket(clientSkt);
			break;
		}
			
		while(inBytes<(UINT)ret)
		{
			packageLen = 0;
			memset(inMsg+inmsgLen,0,IN_MSG_LEN-inmsgLen);
				
			while(inmsgLen<IN_MSG_LEN&&inBytes<(UINT)ret)
			{
				inMsg[inmsgLen] = buffer[inBytes];
				inBytes++;
				inmsgLen++;
				if(inMsg[inmsgLen-1]=='\n')
				{
					packageLen = inmsgLen;
					inmsgLen=0;
					break;
				}
			}

			//没有收到结束字符"\r\n"
			if(inmsgLen!=0)
			{
				//消息缓冲区已满，清空数据，继续处理
				if(inmsgLen==IN_MSG_LEN)
				{
					inmsgLen = 0;
					break;
				}
				//如果接收缓冲区的数据已经处理完毕，则退出接收过程，
				//未处理完的数据留着下次接收时，合并处理
				else
				{
					break;
				}
			}
				
			cout<<"Socket#"<<clientSkt<<" is received message=="<<inMsg<<endl;

			memset(outMsg,0,OUT_MSG_LEN);
			outmsgLen = 0;
			string output;

			if(g_pManagerIF->IsConnected() == false)
		    {
			    memcpy(outMsg,"ERROR\r\nMT is disconnected\r\nend",30);
			    outmsgLen = 30;
			}
			else if(memcmp(inMsg,"GETONLINEUSER",13)==0)
			{
				g_pManagerIF->GetOnlineUser(output);
				memcpy(outMsg,"GETONLINEUSER OK ",17);
				memcpy(&outMsg[17],output.c_str(),output.size());
				outmsgLen = 17+output.size();
			}
			else if(memcmp(inMsg,"GETBALANCE",10)==0)
			{
				int    login = 0;
				GetIntParam(inMsg,"LOGIN=",&login);
				 
				if(g_pManagerIF->GetBalance(login,output) == true)
				{
					memcpy(outMsg,"GETBALANCE OK ",14);
					memcpy(&outMsg[14],output.c_str(),output.size());
					outmsgLen = 14+output.size();
				}
				else
				{
					memcpy(outMsg,"GETBALANCE ERROR",16);
					outmsgLen = 16;
				}
			}
			else if(memcmp(inMsg,"GETENDOFDAY",11)==0)
			{
				if(g_pManagerIF->GetEndOfDay(output) == true)
				{
					memcpy(outMsg,"GETENDOFDAY OK ",15);
					memcpy(&outMsg[15],output.c_str(),output.size());
					outmsgLen = 15+output.size();
				}
				else
				{
					memcpy(outMsg,"GETENDOFDAY ERROR",17);
					outmsgLen = 17;
				}
			}
			else
			{
				struct sockaddr_in sa;
				int len = sizeof(sa);
				getpeername(clientSkt,(struct sockaddr *)&sa, &len);
				if(g_pManagerIF->ProcessWebIF(sa.sin_addr.S_un.S_addr,inMsg+1,packageLen-1,output) == -1)//去掉命令之前的字母W
				{
					memcpy(outMsg,"ERROR\r\ninvalid CMD\r\nend",23);
					outmsgLen = 23;
				}
				else
				{
					memcpy(&outMsg[0],output.c_str(),output.size());
					outmsgLen = output.size();
				}
			}

			memcpy(&outMsg[outmsgLen],"\r\n",2);
			outmsgLen += 2;
			cout<<"Socket#"<<clientSkt<<" is sent message=="<<outMsg<<endl;

			if(send(clientSkt,outMsg,outmsgLen,0)==SOCKET_ERROR)
			{
				cout<<"Socket#"<<clientSkt<<" is closed!"<<endl;
				closesocket(clientSkt);
				bRun = false;
				break;
			}
			else
			{
				cout<<"Socket#"<<clientSkt<<" is closed!"<<endl;
				closesocket(clientSkt);
				bRun = false;
				break;
			}
		}
	}


	delete [] inMsg;
	delete [] outMsg;
	delete [] buffer;

	return;
}