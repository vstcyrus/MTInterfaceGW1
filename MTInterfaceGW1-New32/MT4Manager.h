#pragma once
#include "ManagerIF.h"

class MT4Manager:public ManagerIF
{
private:
	static CManagerFactory   m_factory;

	CSync   m_lock;
	CString	m_serverAddr;
	int		m_usrID;
	CString	m_passwd;
	CString m_passwdHash;
	BOOL	m_bConnected;
	int		m_port;
	

public:
	CManagerInterface *m_pumping;
	CManagerInterface *m_manager;

	MT4Manager(){
		m_pumping = NULL;
		m_manager = NULL;
	};

	~MT4Manager();

	bool Ping();

	int GetPort()
	{
		return m_port;
	};

	int ProcessWebIF(const ULONG ip,char *buffer,const int size,string & output);
	bool GetOnlineUser(string & output);
	bool GetEndOfDay(string & output);
	bool GetBalance(int login,string & output);
	bool Init();
	void Out(const int code,LPCSTR ip,LPCSTR msg,...);
	void  GetCXRecord(TradeRecord & trade,int login,int order);
	void  GetCreditNO(int & order, int login, LPCSTR comment);
	bool IsConnected(){return (bool)m_bConnected;}

	
	static void __stdcall OnPumping(int code);
	
 };