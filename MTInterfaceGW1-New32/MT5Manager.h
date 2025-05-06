#pragma once
#include "StdAfx.h"
#include "ManagerIF.h"
//+------------------------------------------------------------------+
//| Manager                                                          |
//+------------------------------------------------------------------+
class MT5Manager:public ManagerIF,public IMTManagerSink
  {
private:
   enum              constants
     {
      MT5_CONNECT_TIMEOUT=60000,       // connect timeout in milliseconds
     };
   IMTManagerAPI    *m_manager;
   IMTAdminAPI      *m_admin;
   CMTManagerAPIFactory m_factory;
   IMTDealArray*     m_deal_array;
   IMTUser*          m_user;
   IMTAccount*       m_account;

    CString	m_serverAddr;
	UINT64 	m_usrID;
	CString	m_passwd;
	CString m_passwdHash;
	volatile LONG     m_bConnected;
	int		m_port;

public:
                     MT5Manager(void);
                    ~MT5Manager(void);
   //--- initialize, login
   bool              Initialize();
   bool              Login();
   void              Logout();
   //--- dealer operation
   bool              DealerBalance(const UINT64 login,const double amount,const UINT type,const LPCWSTR comment,bool deposit);
   //--- get info
   bool              GetUserDeal(IMTDealArray*& deals,const UINT64 login,SYSTEMTIME &time_from,SYSTEMTIME &time_to);
   bool              GetUserInfo(UINT64 login,CMTStr &str);
   bool              GetAccountInfo(UINT64 login,CMTStr &str);
   static UINT       Reconnect(VOID* param);
   bool IsConnected(){return (bool)m_bConnected;}

   bool Init();
   int GetPort(){return m_port;};
   bool Ping();
   int ProcessWebIF(const ULONG ip,char *buffer,const int size,string & output);
   bool GetOnlineUser(string & output);
   bool GetEndOfDay(string & output);
   bool GetBalance(int login,string & output);
   void  GetCXRecord(TradeRecord & trade,int login,int order){};
   void  GetCreditNO(int & order, int login, LPCSTR comment){};

   //--- IMTManagerSink implementation
   virtual void      OnDisconnect(void);

private:
   void              Shutdown();

  };
//+------------------------------------------------------------------+
extern MT5Manager g_mt5manager;