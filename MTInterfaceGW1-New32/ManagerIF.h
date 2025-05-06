#pragma once

class ManagerIF
{
public:
	virtual ~ManagerIF(){};
	virtual bool Init()=0;
	virtual int GetPort()=0;
	virtual bool Ping()=0;
	virtual bool IsConnected()=0;
	virtual int ProcessWebIF(const ULONG ip,char *buffer,const int size,string & output)=0;
	virtual bool GetOnlineUser(string & output)=0;
	virtual bool GetEndOfDay(string & output)=0;
	virtual bool GetBalance(int login,string & output)=0;

	static byte m_key[AES::DEFAULT_KEYLENGTH];
	static bool EncryptPassword(string & src,string & sin);
	static bool DecryptPassword(string & src,string & sin);
};

extern ManagerIF * g_pManagerIF;