#include "StdAfx.h"
#include <ctime>  // for std::tm, std::mktime
#include "MT5Manager.h"
#include "MessageCenter.h"

extern CString					g_currentPath;
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+

MT5Manager::MT5Manager(void) : m_manager(NULL),m_admin(NULL),m_deal_array(NULL),m_user(NULL),m_account(NULL),m_bConnected(false)
  {
  }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
MT5Manager::~MT5Manager(void)
  {
   Shutdown();
  }

bool MT5Manager::Init()
{
	if(Initialize() == false) return false;

	int STR_MAX_LEN = 512;
	CString fileName = g_currentPath + "\\MTInterfaceGW.ini";

	m_port = GetPrivateProfileInt("SYSTEM","PORT",0,fileName.GetBuffer());
	GetPrivateProfileString("MT","SERVER","",m_serverAddr.GetBuffer(STR_MAX_LEN),STR_MAX_LEN,fileName.GetBuffer());
	m_serverAddr.ReleaseBuffer();
	GetPrivateProfileString("MT","PASSWORD","",m_passwd.GetBuffer(STR_MAX_LEN),STR_MAX_LEN,fileName.GetBuffer());
	m_passwd.ReleaseBuffer();
	GetPrivateProfileString("MT","PASSWORDHASH","",m_passwdHash.GetBuffer(STR_MAX_LEN),STR_MAX_LEN,fileName.GetBuffer());
	m_passwdHash.ReleaseBuffer();
	m_usrID = GetPrivateProfileInt("MT","USER",0,fileName.GetBuffer());

	if(m_port==0 || m_usrID == 0 || m_serverAddr=="")
	{
		return false;
	}

	if(m_passwd=="")
	{
		if(m_passwdHash=="")
		{
			return false;
		}
	}
	else
	{
		string passwordHash="";
		EncryptPassword(string(m_passwd.GetBuffer()),passwordHash);

		m_passwdHash = passwordHash.c_str();

		if( WritePrivateProfileString("MT","PASSWORDHASH",m_passwdHash.GetBuffer(),fileName.GetBuffer()) == FALSE )
		{
			return false;
		}

		if( WritePrivateProfileString("MT","PASSWORD","",fileName.GetBuffer()) == FALSE )
		{
			return false;
		}
	}
	
	string password="";
	DecryptPassword(string(m_passwdHash.GetBuffer()),password);

	m_passwd = password.c_str();

	AfxBeginThread(MT5Manager::Reconnect,NULL);

	return true;
}

bool  MT5Manager::Ping()
{	
	return (bool)m_bConnected;
}

bool MT5Manager::GetOnlineUser(string & output)
{
	int userOnline=0;
	
	IMTOnline * online =  m_manager->OnlineCreate();

	CString tmp="";

	for(int j = 0; j < m_manager->OnlineTotal(); j++)
	{

		if( m_manager->OnlineNext(j,online) != MT_RET_OK) break;

		if(output.size() > (OUT_MSG_LEN-100))
		{
			break;
		}

		tmp.Format("LOGIN=%d|",online->Login());
		output += tmp;
	}
	
	online->Release();

	return true;
}

bool MT5Manager::GetEndOfDay(string & output)
{
	bool ret = false;
	
	IMTConServer * conServer = m_admin->NetServerCreate();
	for(int pos = 0; pos < m_admin->NetServerTotal(); pos++)
	{
		if( m_admin->NetServerNext(pos,conServer) != MT_RET_OK) break;

		if(conServer->Type() != IMTConServer::NET_MAIN_TRADE_SERVER) continue;

		IMTConServerTrade * tradeServer = conServer->TradeServer();

		if(tradeServer == NULL) break;

		CString tmp="";
		tmp.Format("%02d:%02d",tradeServer->OvernightTime()/60,tradeServer->OvernightTime()%60);
		output += tmp.GetBuffer();
		ret = true;
	}

	conServer->Release();

	return ret;
}

bool MT5Manager::GetBalance(int login,string & output)
{
	bool ret = false;
	
	IMTUser * user = m_manager->UserCreate();
	MTAPIRES res;
	if( (res = m_manager->UserGet(login,user)) == MT_RET_OK )
	{
		CString tmp="";
		tmp.Format("LOGIN=%d|",login);
		output += tmp;

		tmp.Format("BALANCE=%.2lf|",user->Balance());
		output += tmp;

		tmp.Format("CREDIT=%.2lf|",user->Credit());
		output += tmp;

		ret = true;
	}

	user->Release();

	return ret;
}

 int MT5Manager::ProcessWebIF(const ULONG ip,char *buffer,const int size,string & output)
 {
   IMTUser     * pUser =NULL;
   UserRecord    user = {0};
   IMTConGroup * pGroup = NULL;
   IMTAccount * pAccount = NULL;
   char       group_name[64]={0};
   int        user_lang = 9;//Ĭ��������Ӣ��
   char       temp[256]={0};
   int        grpId = 0;
   double     deposit = 0.0;
   double     credit = 0.0;
   double     balance = 0.0;
   int	      cmd = 0;
   BOOL       investor,drop_key;
   time_t	  lTime;
   int        creditNo = 0;
   int        eqLimit = 0;
   int        force = 0;
   int        creditWay = 0;
   int        bnsRate = 0;
   int        bnsDay = 0;
   char       bnsCode[32] = {0};
   int        balSn = 0;
   bool		  bReject = false;
   bool       bAllReject = false;
   int        cxlBalSn = 0;
   int        cxlBalTick = 0;
   char       creditComm[64] = {0};
   char       creditDetail[1024] = {0};
   string     strCreditDetail = "";
   int        dpTicket = 0;
   CString    tmpoutput;
   GroupCommandInfo gcInfo = {0};
   TradeTransInfo info = {0};
   int userTotal = 1;
   UINT64     dealerID = m_usrID;

   double margin,freemargin,equity,withdrawal;

   in_addr addr; 
   addr.s_addr = ip; 
   string ipStr = inet_ntoa(addr); 

   int         ret = MT_RET_OK;
   UINT64      userRights = 0;
   UINT64      tradeFlags;
   bool			enableEA = false;
   bool			enableTrailing = false;
   int          passwordNum;
   CStringW		wCity = L"";
   CStringW		wState = L"";

//---- checks
   if(buffer==NULL || size<1) 
     { 
      return -1;
     }

//---- check command
   if(memcmp(buffer,"NEWACCOUNT",10)==0)
     { 
      cmd = 0;
     }
   else if(memcmp(buffer,"DEPOSIT",7)==0)
    {
	   cmd = 1;
    }
   else if(memcmp(buffer,"DELACCOUNT",10)==0)
    {
	   cmd = 2;
    }
    else if(memcmp(buffer,"CHANGEPASS",10)==0)
    {
	   cmd = 3;
    }
   else if(memcmp(buffer,"CHECKPASS",9)==0)
    {
	   cmd = 4;
    }
	else if(memcmp(buffer,"CREDIT",6)==0)
    {
	   cmd = 5;
    }
   	else if(memcmp(buffer,"UPDATEACCOUNT",13)==0)
    {
	   cmd = 6;
    }
	else if(memcmp(buffer,"CHKACCOUNT",10)==0)
    {
	   cmd = 7;
    }
	else if(memcmp(buffer,"CHKFREEMARGIN",13)==0)
    {
	   cmd = 8;
    }
   else
   {
      return -1;
   }

   string outStr = buffer;
   int passwordPos,startPos,endPos;
   passwordPos = outStr.find("|PASSWORD=");
   if(passwordPos != string::npos)
   {
	   startPos = passwordPos+10;
	   endPos = outStr.find('|',startPos);
	   if(endPos != string::npos)
	   {
		   outStr.erase(startPos,(endPos-startPos));
	   }
   }

   m_admin->LoggerOut(CmdOK,L"%s 'web': receive command '%s'",(CStringW)(ipStr.c_str()),(CStringW)(outStr.c_str()));

  switch(cmd)
  {
	case 0:
		GetStrParam(buffer,"GROUP=",group_name,sizeof(group_name)-1);

		//---- receive group overview
		pGroup = m_manager->GroupCreate();
		if(m_manager->GroupGet((CStringW)group_name,pGroup)!=MT_RET_OK)
		{
			pGroup->Release();
			output = "ERROR\r\ninvalid GROUP\r\nend";
			return 0;
		}

		//COPY_STR(user.group,group_name);
		
		user.enable                =TRUE;
		user.enable_change_password=TRUE;
		user.leverage              =pGroup->DemoLeverage();
		user.user_color            =0xff000000;
		user.enable_read_only      = FALSE;
		user.send_reports          = TRUE;
		GetIntParam(buffer,"ENABLE=",&user.enable);
		GetIntParam(buffer,"CHANGE_PASSWORD=",&user.enable_change_password);
		GetIntParam(buffer,"LEVERAGE=",     &user.leverage);
		GetIntParam(buffer,"READONLY=",		&user.enable_read_only);
		GetIntParam(buffer,"SEND_REPORTS=", &user.send_reports);
		passwordNum = pGroup->AuthPasswordMin();
		tradeFlags = pGroup->TradeFlags();
		if((tradeFlags&IMTConGroup::TRADEFLAGS_EXPERTS) != 0) enableEA = true;
		if((tradeFlags&IMTConGroup::TRADEFLAGS_TRAILING) != 0) enableTrailing = true;
		pGroup->Release();
		
		if(GetStrParam(buffer,"NAME=",          user.name,             sizeof(user.name)-1)==FALSE)
		{
			output = "ERROR\r\nmiss NAME\r\nend";
			return 0;
		}
		if(GetStrParam(buffer,"PASSWORD=",      user.password,         sizeof(user.password)-1)==FALSE)
		{
			output = "ERROR\r\nmiss PASSWORD\r\nend";
			return 0;
		}
		if(GetStrParam(buffer,"INVESTOR=",      user.password_investor,sizeof(user.password_investor)-1)==FALSE)
		{
			output = "ERROR\r\nmiss INVESTOR\r\nend";
			return 0;
		}

		GetStrParam(buffer,"EMAIL=",         user.email,            sizeof(user.email)-1);
#ifndef VEXNB
		GetStrParam(buffer,"COUNTRY=",       user.country,          sizeof(user.country)-1);
#endif
		GetStrParam(buffer,"STATE=",         user.state,            sizeof(user.state)-1);
		GetStrParam(buffer,"CITY=",          user.city,             sizeof(user.city)-1);
		GetStrParam(buffer,"ADDRESS=",       user.address,          sizeof(user.address)-1);
		GetStrParam(buffer,"COMMENT=",       user.comment,          sizeof(user.comment)-1);
		if(user.comment[0]=='(') memcpy(user.unused,user.comment+1,3);
		GetStrParam(buffer,"PHONE=",         user.phone,            sizeof(user.phone)-1);
		GetStrParam(buffer,"PHONE_PASSWORD=",user.password_phone,   sizeof(user.password_phone)-1);
		GetStrParam(buffer,"STATUS=",        user.status,           sizeof(user.status)-1);
		GetStrParam(buffer,"ZIPCODE=",       user.zipcode,          sizeof(user.zipcode)-1);
		GetStrParam(buffer,"ID=",            user.id,               sizeof(user.id)-1);
		GetIntParam(buffer,"AGENT=",        &user.agent_account);
		GetIntParam(buffer,"LOGIN=",        &user.login);
		GetFltParam(buffer,"INTEREST_RATE=",			&user.interestrate);
		GetIntParam(buffer,"LIMIT=",         &eqLimit);
		memcpy(user.api_data,&eqLimit,sizeof(eqLimit));
		GetFltParam(buffer,"BALANCE=",      &balance);
		GetIntParam(buffer,"LANGUAGE=",     &user_lang);

		//---- checks
		if(user.send_reports!=0)         user.send_reports =TRUE;
		if(user.enable!=0)				 user.enable =TRUE;
		if(user.enable_change_password!=0)         user.enable_change_password =TRUE;
		if(user.enable_read_only!=0)     user.enable_read_only =TRUE;
		if(user.leverage<1)              user.leverage=1;

		if(user.interestrate<-0.0000001 || user.interestrate>(100+0.0000001))
		{
			output = "ERROR\r\ninvalid INTEREST_RATE\r\nend";
			return 0;
		}

		//---- check complexity of password
		if(CheckPassword(user.password,passwordNum)==FALSE)
		{
			output = "ERROR\r\ninvalid PASSWORD\r\nend";
			return 0;
		}
		//---- check user record
		if(user.name[0]==0)
		{
			output = "ERROR\r\ninvalid NAME\r\nend";
			return 0;
		}
		if(user.leverage<1)
		{
			output = "ERROR\r\ninvalid LEVERAGE\r\nend";
			return 0;
		}
		if(user.agent_account<0)
		{
			output = "ERROR\r\ninvalid AGENT\r\nend";
			return 0;
		}

		pUser = m_manager->UserCreate();
		
		if(user.enable == TRUE) userRights = userRights|IMTUser::USER_RIGHT_ENABLED;
		if(user.enable_read_only == TRUE) userRights = userRights|IMTUser::USER_RIGHT_TRADE_DISABLED;
		if(user.enable_change_password == TRUE) userRights = userRights|IMTUser::USER_RIGHT_PASSWORD;
		if(user.send_reports == TRUE) userRights = userRights|IMTUser::USER_RIGHT_REPORTS;
		if(enableEA == true) userRights = userRights|IMTUser::USER_RIGHT_EXPERT;
		if(enableTrailing == true) userRights = userRights|IMTUser::USER_RIGHT_TRAILING;
		pUser->Rights(userRights);
		pUser->Login(user.login);
		pUser->Group((CStringW)group_name);
		pUser->Name((CStringW)user.name);
		pUser->Country((CStringW)user.country);
		pUser->Language(user_lang);
		
		pUser->City(wCity);
		pUser->State(wState);
		pUser->ZIPCode((CStringW)user.zipcode);
		pUser->Address((CStringW)user.address);
		pUser->Phone((CStringW)user.phone);
		pUser->EMail((CStringW)user.email);
		pUser->ID((CStringW)user.id);
		pUser->Status((CStringW)user.status);
		pUser->Comment((CStringW)user.comment);
		pUser->Color(user.user_color);
		pUser->PhonePassword((CStringW)user.password_phone);
		pUser->Leverage(user.leverage);
		pUser->Agent(user.agent_account);
		pUser->ApiDataSet(1,1,(UINT64)eqLimit);
	   
		//---- creating new account
		if((ret = m_manager->UserAdd(pUser,(CStringW)user.password,(CStringW)user.password_investor)) != MT_RET_OK)
		{
			m_admin->LoggerOut(CmdErr,L"%s 'web': new account '%d' failed#%d",(CStringW)ipStr.c_str(),user.login,ret);
			output = "ERROR\r\naccount create failed\r\nend";
			pUser->Release();
			return 0;
		}
		else
		{
			pUser->Release();

			if(balance >= 0.01)
			{
				if(GetStrParam(buffer,"BALCOMMENT=",      temp,             sizeof(temp)-1)==FALSE)
				{
					temp[0]=0;
				}

				if(memcmp(temp,"WD#A",4) == 0)
				{
					GetIntParam(temp,"#A",&balSn);
				}
				else
				{
					GetIntParam(temp,"#",&balSn);
				}

				memset(&info,0,sizeof(TradeTransInfo));
				info.type = TT_BR_BALANCE;
				info.cmd = OP_BALANCE;
				info.orderby = user.login;
				info.price = balance;
				strcpy(info.comment,temp);
				if( m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_BALANCE,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE )
				{
					m_admin->LoggerOut(CmdOK,L"%s 'web': new account '%d' - %s",(CStringW)(ipStr.c_str()),user.login,(CStringW)(user.name));
					m_admin->LoggerOut(CmdOK,L"%s 'web': new and balance %.2lf for '%d' - '%s'",(CStringW)(ipStr.c_str()),balance,user.login,(CStringW)temp);

					GetIntParam(buffer,"CREDITWAY=",&creditWay);
					if(creditWay == 1)//bonus code
					{
						if(GetStrParam(buffer,"BNSCODE=",      bnsCode,             sizeof(bnsCode)-1)==FALSE)
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)(ipStr.c_str()),user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\nmiss BNSCODE\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}

						GetIntParam(buffer,"BNSDAY=",&bnsDay);
						if(bnsDay<=0)
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)(ipStr.c_str()),user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\ninvalid BNSDAY\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}
						GetIntParam(buffer,"BNSRATE=",&bnsRate);
						if(bnsRate<=0)
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)(ipStr.c_str()),user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\ninvalid BNSRATE\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}

						sprintf(creditComm,"BCI%d@%s#%d",bnsRate,bnsCode,balSn);
						credit = bnsRate*balance/100.0;
						info.type = TT_BR_BALANCE;
						info.cmd = OP_CREDIT;
						info.orderby = user.login;
						info.price = credit;
						strcpy(info.comment,creditComm);
						info.expiration = bnsDay;

						if((m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID)) == MT_RET_REQUEST_DONE)
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
						}
						else
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
						}
					}
					else if(creditWay == 2)//credit details
					{
						if(GetStrParam(buffer,"CREDITDETAIL=",      creditDetail,             sizeof(creditDetail)-1)==FALSE)
						{
							m_admin->LoggerOut(CmdOK,L"%s 'web': no CREDITDETAIL to BCI for '%d#%.2lf'",(CStringW)ipStr.c_str(),user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\nmiss CREDITDETAIL\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}

						string::size_type pos;
						string pattern = ",";
						strCreditDetail = creditDetail;
						int count = 0;
						for(int i=1; i<strCreditDetail.size(); i++)
						{
							pos=strCreditDetail.find(pattern,i);
							if(pos<strCreditDetail.size())
							{
							  string subStr = strCreditDetail.substr(i,pos-i);
							  if(count%4 == 0)
							  {
								  creditNo = atoi(subStr.c_str());
							  }
							  else if(count%4 == 1)
							  {
								  credit = atof(subStr.c_str());
								  credit = -credit;
							  }
							  else if(count%4 == 2)
							  {
								  bnsDay = atoi(subStr.c_str());
							  }
							  else
							  {
								  sprintf(creditComm,"%s%d-%d",subStr.c_str(),balSn,creditNo);
								  info.type = TT_BR_BALANCE;
								  info.cmd = OP_CREDIT;
								  info.orderby = user.login;
						          info.price = credit;
						          strcpy(info.comment,creditComm);
						          info.expiration = bnsDay;
								  if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
								  {
									 m_admin->LoggerOut(CmdOK,L"%s 'web': BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
								  }
								  else
								  {
									 m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
								  }
							  }
							  i=pos+pattern.size()-1;
							  count++;
							}
						}
					}

					tmpoutput.Format("OK\r\nLOGIN=%d\r\nend",user.login);
					output = tmpoutput;
					return 0;
				}
				else
				{
					m_manager->UserDelete(user.login);
					output = "ERROR\r\naccount create failed\r\nend";
					return 0;
				}
			}
			else
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': new account '%d' - %s",(CStringW)ipStr.c_str(),user.login,(CStringW)user.name);
				tmpoutput.Format("OK\r\nLOGIN=%d\r\nend",user.login);
				output = tmpoutput;
				return 0;
			}
		}

	    break;
	case 1:
		if(GetIntParam(buffer,"LOGIN=",&user.login) ==FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}
		if(GetFltParam(buffer,"DEPOSIT=",      &deposit) == FALSE)
		{
			output = "ERROR\r\nmiss DEPOSIT\r\nend";
			return 0;
		}
		if(GetStrParam(buffer,"COMMENT=",      temp,             sizeof(temp)-1)==FALSE)
		{
			temp[0]=0;
		}

		if(memcmp(temp,"WD#A",4) == 0)
		{
			GetIntParam(temp,"#A",&balSn);
		}
		else
		{
			GetIntParam(temp,"#",&balSn);
		}

		
		if(memcmp(temp,"RJT#",4) == 0 || memcmp(temp,"RFD#",4) == 0 || memcmp(temp,"CXL#",4) == 0)
		{
			GetIntParam(temp,"-",&cxlBalTick);
			TradeRecord cxlRecord = {0};
			GetCXRecord(cxlRecord,user.login,cxlBalTick);
			bReject = true;

			if(abs(deposit+cxlRecord.profit) < 0.0000001 && user.login == cxlRecord.login)
			{
				bAllReject = true;
				
				if(memcmp(cxlRecord.comment,"WD#A",4) == 0)
				{
					GetIntParam(cxlRecord.comment,"#A",&cxlBalSn);
				}
				else
				{
					GetIntParam(cxlRecord.comment,"#",&cxlBalSn);
				}

				m_admin->LoggerOut(CmdOK,L"%s 'web': start to cancel balSn#%d",(CStringW)ipStr.c_str(),cxlBalSn);
			}
			else
			{
				bAllReject = false;
				m_admin->LoggerOut(CmdOK,L"%s 'web': start to balSn#%d without handle credit",(CStringW)ipStr.c_str(),cxlBalSn);
			}
		}
		
		GetIntParam(buffer,"FORCE=",&force);

		if(deposit<-0.0000001)
		{
			pAccount = m_manager->UserCreateAccount();
			if(m_manager->UserAccountRequest(user.login,pAccount) == MT_RET_OK)
			{
				margin = pAccount->Margin();
				freemargin = pAccount->MarginFree();
				equity = pAccount->Equity();
				user.credit = pAccount->Credit();
				pAccount->Release();

				if(force == FALSE)
				{
					float tmp = freemargin+deposit-user.credit;
					if( tmp<-0.0000001 )
					{
						output = "ERROR\r\ndeposit failed\r\nend";
						return 0;
					}
				}
				else
				{
					
				}
			}
			else
			{
				pAccount->Release();
				output = "ERROR\r\ndeposit failed\r\nend";
				return 0;
			}
		}

		GetIntParam(buffer,"CREDITWAY=",&creditWay);
		if(creditWay >= 3 && creditWay < 6 && deposit > 0.0000001)//BZB
		{
			GetIntParam(temp,"-",&dpTicket);
			map<int,BNSDetail> bnsMap;
			map<int,BNSDetail>::iterator bnsIt;
			lTime = m_manager->TimeServer();
			time_t fromTime;

			if(creditWay == 3)
				fromTime = 1262275200;//2010-01-01 00:00:00
			else
				fromTime = 1433088000;//2015-06-01 00:00:00

			IMTDealArray * pTrades = NULL;
			int total = 0;
			pTrades = m_manager->DealCreateArray();
			m_manager->DealRequest(user.login,fromTime,lTime,pTrades);
			total = pTrades->Total();
			
			for(int i = 0; i < total; i++)
			{
				IMTDeal * pTrade = pTrades->Next(i);
				if( pTrade->Action() != IMTDeal::EnDealAction::DEAL_CREDIT) continue;
				if( pTrade->Deal() > dpTicket && pTrade->Profit() > 0.0000001) continue;//�����������ˮ��֮�������

				int bciNo = 0;
				BNSDetail bnsDetail = {0};
				char dealComment[64] = {0};
				strcpy(dealComment,(CString)pTrade->Comment());
				if(memcmp(dealComment,"BCI",3) == 0 || memcmp(dealComment,"Credit In",9) == 0)
				{
					bnsIt = bnsMap.find(pTrade->Deal());
					if(bnsIt == bnsMap.end())
					{
						if(memcmp(dealComment,"BCI",3) == 0)
						{
							bnsDetail.rate = atoi(&dealComment[3]);
							sscanf(dealComment,"%*[^@]@%[^#]",bnsDetail.bnsCode);
						}
						bnsDetail.bnsDay = pTrade->Time();
						bnsDetail.value = pTrade->Profit();
						bnsMap[pTrade->Deal()] = bnsDetail;
					}
					else
					{
						if(memcmp(dealComment,"BCI",3) == 0)
						{
							bnsIt->second.rate = atoi(&dealComment[3]);
							sscanf(dealComment,"%*[^@]@%[^#]",bnsIt->second.bnsCode);
						}
						bnsIt->second.value += pTrade->Profit();
						bnsIt->second.bnsDay = pTrade->Time();
					}
				}
				else if(memcmp(dealComment,"BCO",3) == 0 || memcmp(dealComment,"Credit Out",10) == 0)
				{
					GetIntParam(dealComment,"-",&bciNo);
					bnsIt = bnsMap.find(bciNo);
					if(bnsIt == bnsMap.end())
					{
						bnsDetail.value = pTrade->Profit();
						bnsMap[bciNo] = bnsDetail;
					}
					else
					{
						bnsIt->second.value += pTrade->Profit();
					}
				}
				else if(memcmp(dealComment,"BCX",3) == 0)
				{
					GetIntParam(dealComment,"#",&bciNo);
					bnsIt = bnsMap.find(bciNo);
					if(bnsIt == bnsMap.end())
					{
						bnsDetail.value = pTrade->Profit();
						bnsMap[bciNo] = bnsDetail;
					}
					else
					{
						bnsIt->second.value += pTrade->Profit();
					}
				}
			}
			pTrades->Release();

			bool bzbStop = false;
			credit = -deposit;
			char  wdType;
			map<string,float> bzbMap;
			map<string,float>::iterator bzbIt;
			for(int wdCount = 0; wdCount < 3; wdCount++)
			{
				if(wdCount == 0) wdType = 'A';
				if(wdCount == 1) wdType = 'B';
				if(wdCount == 2)
				{
					wdType = 0;
					if(creditWay != 3) break;
				}

				for(bnsIt = bnsMap.begin(); bnsIt != bnsMap.end(); bnsIt++)
				{
					if( (credit + 0.01) > 0.0000001 )
					{
						if( creditWay == 5 ) break;

						bzbStop = true;
					}

					if( (bnsIt->second.value - 0.01) < -0.0000001 ) continue;
					
					if( wdCount < 2 && (bnsIt->second.bnsCode[0] > wdType || bnsIt->second.bnsCode[0] == 0) ) continue;//���ȿ۳��ȼ�ΪA�Ļ��û�еȼ��Ļ,Ȼ��۳��ȼ�ΪB�Ļ,���۳�������

					double creditOut = 0.0;
					double bzbAmount = 0.0;
					if((credit + bnsIt->second.value) > -0.0000001)
					{
						if(creditWay == 5)
						{
							creditOut = credit;
						}
						else
						{
							creditOut = -bnsIt->second.value;
						}
						bzbAmount = credit;
						credit = 0.0;
					}
					else
					{
						creditOut = -bnsIt->second.value;
						credit += bnsIt->second.value;
						bzbAmount = creditOut;
					}

					if(bnsIt->second.bnsCode[0] != 0)
						sprintf(creditComm,"BCO%d@%s#%d-%d",bnsIt->second.rate,bnsIt->second.bnsCode,balSn,bnsIt->first);
					else
						sprintf(creditComm,"Credit Out#%d-%d",balSn,bnsIt->first);

					
					pAccount = m_manager->UserCreateAccount();
					user.credit = 0.0;
					if(m_manager->UserAccountRequest(user.login,pAccount) == MT_RET_OK)
					{
						user.credit = pAccount->Credit();
					}
					pAccount->Release();

					if(user.credit < 0.0000001)
					{
						credit = 0.0;
						break;
					}

					if((creditOut+user.credit) < -0.0000001)
					{
						creditOut = (-1.0)*user.credit;
						credit = 0.0;

						if(bzbAmount < creditOut) bzbAmount = creditOut;
					}

					bnsIt->second.value += creditOut;
					
					memset(&info,0,sizeof(TradeTransInfo));
					info.type = TT_BR_BALANCE;
					info.cmd = OP_CREDIT;
					info.orderby = user.login;
					info.price = creditOut;
					strcpy(info.comment,creditComm);
					info.expiration = bnsIt->second.bnsDay;
						
					if( m_manager->DealerBalanceRaw(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE )
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': BZB credit out %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),creditOut,user.login,(CStringW)creditComm);
					}
					else
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BZB credit out %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),creditOut,user.login,(CStringW)creditComm);
					}

					if(bnsIt->second.bnsCode[0] != 0 && bzbStop == false)
					{
						bzbIt = bzbMap.find(bnsIt->second.bnsCode);
						if(bzbIt == bzbMap.end())
						{
							bzbMap[bnsIt->second.bnsCode] = bzbAmount;
						}
						else
						{
							bzbIt->second += bzbAmount;
						}
					}
				}
			}

			for(bzbIt = bzbMap.begin(); bzbIt != bzbMap.end(); bzbIt++)
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': BZB enter bzb map",(CStringW)ipStr.c_str());

				sprintf(creditComm,"BZB@%s#%d-%d",bzbIt->first.c_str(),balSn,dpTicket);

				memset(&info,0,sizeof(TradeTransInfo));
				info.type = TT_BR_BALANCE;
				info.cmd = OP_BALANCE;
				info.orderby = user.login;
				info.price = -bzbIt->second;
				strcpy(info.comment,creditComm);

				if( m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_BALANCE,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE )
				{
					m_admin->LoggerOut(CmdOK,L"%s 'web': BZB balance %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),-bzbIt->second,user.login,(CStringW)creditComm);
				}
				else
				{
					m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BZB balance %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),-bzbIt->second,user.login,(CStringW)creditComm);
				}
			}

			output = "OK\r\nbzb successful\r\nend";
			return 0;
		}
		else 
		{
			memset(&info,0,sizeof(TradeTransInfo));
			info.type = TT_BR_BALANCE;
			info.cmd = OP_BALANCE;
			info.orderby = user.login;
			info.price = deposit;
			strcpy(info.comment,temp);

			if(force == TRUE)
			{
				if( m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CORRECTION,(CStringW)info.comment,dealerID) != MT_RET_REQUEST_DONE )
				{
					output = "ERROR\r\ndeposit failed\r\nend";
					return 0;
				}

				m_admin->LoggerOut(CmdOK,L"%s 'web': changed balance force %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),deposit,user.login,(CStringW)temp);
			}
			else
			{
				if( m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_BALANCE,(CStringW)info.comment,dealerID) != MT_RET_REQUEST_DONE )
				{
					output = "ERROR\r\ndeposit failed\r\nend";
					return 0;
				}

				m_admin->LoggerOut(CmdOK,L"%s 'web': changed balance %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),deposit,user.login,(CStringW)temp);
			}

			if(deposit<-0.0000001)
			{
				creditWay = 1;//Ĭ�Ͽ۳�����
				GetIntParam(buffer,"CREDITWAY=",&creditWay);

				if(bReject == true)
				{
					if(bAllReject == true)
					{
						lTime = m_manager->TimeServer();
						time_t fromTime = 1433088000;//2015-06-01 00:00:00
						IMTDealArray * pTrades = NULL;
						int total = 0;
						double cxlCredit = 0.0;
						pTrades = m_manager->DealCreateArray();
						m_manager->DealRequest(user.login,fromTime,lTime,pTrades);
						total = pTrades->Total();

						for(int i = 0; i < total; i++)
						{
							IMTDeal * pTrade = pTrades->Next(i);

							if(user.credit < 0.0000001) break;

							if(pTrade->Action() != IMTDeal::EnDealAction::DEAL_CREDIT) continue;
							char dealComment[64] = {0};
							strcpy(dealComment,(CString)pTrade->Comment());
							if(memcmp(dealComment,"BCI",3) != 0) continue;
							if(pTrade->Profit() < 0.0000001) continue;

							int creditSn = 0;
							GetIntParam(dealComment,"#",&creditSn);
							int creditBalTick = 0;
							GetIntParam(dealComment,"-",&creditBalTick);
							if(creditSn == cxlBalSn || creditBalTick == cxlBalTick)//�Զ�bci���ֶ�bci
							{
								if( (user.credit - pTrade->Profit()) > -0.0000001 )
								{
									cxlCredit = -pTrade->Profit();
								}
								else
								{
									cxlCredit = -user.credit;
								}
								BNSDetail bnsDetail = {0};
								bnsDetail.rate = atoi(&dealComment[3]);
								bnsDetail.bnsDay = pTrade->Time();
								sscanf(dealComment,"%*[^@]@%[^#]",bnsDetail.bnsCode);

								sprintf(creditComm,"BCO%d@%s#%d-%d",bnsDetail.rate,bnsDetail.bnsCode,balSn,pTrade->Deal());
								info.type = TT_BR_BALANCE;
								info.cmd = OP_CREDIT;
								info.orderby = user.login;
								info.price = cxlCredit;
								strcpy(info.comment,creditComm);
								info.expiration = bnsDetail.bnsDay;
								if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
								{
									m_admin->LoggerOut(CmdOK,L"%s 'web': BCO %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),cxlCredit,user.login,(CStringW)creditComm);
								}
								else
								{
									m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCO %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),cxlCredit,user.login,(CStringW)creditComm);
								}

								break;
							}
						}
						pTrades->Release();
					}
				}
				else if(creditWay != 0)
				{
					map<int,BNSDetail> bnsMap;
					map<int,BNSDetail>::iterator bnsIt;
					lTime = m_manager->TimeServer();
					time_t fromTime = 1433088000;//2015-06-01 00:00:00
					IMTDealArray * pTrades = NULL;
					pTrades = m_manager->DealCreateArray();
					int total = 0;
					m_manager->DealRequest(user.login,fromTime,lTime,pTrades);
					total = pTrades->Total();

					for(int i = 0; i < total; i++)
					{
						IMTDeal * pTrade = pTrades->Next(i);
						if(pTrade->Action() != IMTDeal::EnDealAction::DEAL_CREDIT) continue;

						int bciNo = 0;
						BNSDetail bnsDetail = {0};
						char dealComment[64] = {0};
						strcpy(dealComment,(CString)pTrade->Comment());
						if(memcmp(dealComment,"BCI",3) == 0)
						{
							bnsIt = bnsMap.find(pTrade->Deal());
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrade->Profit();
								bnsDetail.rate = atoi(&dealComment[3]);
								bnsDetail.bnsDay = pTrade->Time();
								sscanf(dealComment,"%*[^@]@%[^#]",bnsDetail.bnsCode);
								bnsMap[pTrade->Deal()] = bnsDetail;
							}
							else
							{
								bnsIt->second.rate = atoi(&dealComment[3]);
								bnsIt->second.value += pTrade->Profit();
								bnsIt->second.bnsDay = pTrade->Time();
								sscanf(dealComment,"%*[^@]@%[^#]",bnsIt->second.bnsCode);
							}
						}
						else if(memcmp(dealComment,"BCO",3) == 0)
						{
							GetIntParam(dealComment,"-",&bciNo);
							bnsIt = bnsMap.find(bciNo);
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrade->Profit();
								bnsMap[bciNo] = bnsDetail;
							}
							else
							{
								bnsIt->second.value += pTrade->Profit();
							}
						}
						else if(memcmp(dealComment,"BCX",3) == 0)
						{
							GetIntParam(dealComment,"#",&bciNo);
							bnsIt = bnsMap.find(bciNo);
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrade->Profit();
								bnsMap[bciNo] = bnsDetail;
							}
							else
							{
								bnsIt->second.value += pTrade->Profit();
							}
						}
					}
					pTrades->Release();

					credit = deposit;
					char  wdType;
					for(int wdCount = 0; wdCount < 2; wdCount++)
					{
						if(wdCount == 0) wdType = 'A';
						if(wdCount == 1) wdType = 'B';

						for(bnsIt = bnsMap.begin(); bnsIt != bnsMap.end(); bnsIt++)
						{
							if( (credit + 0.01) > 0.0000001 ) break;

							if(bnsIt->second.rate == 0) continue; //���۳�����Ϊ0������
							if( (bnsIt->second.value - 0.01) < -0.0000001 ) continue;
							if( bnsIt->second.bnsCode[0] > wdType ) continue;//���ȿ۳��ȼ�ΪA�Ļ��û�еȼ��Ļ,Ȼ��۳��ȼ�ΪB�Ļ

							double creditOut = 0.0;
							if((credit + bnsIt->second.value*100.0/bnsIt->second.rate) > -0.0000001)
							{
								creditOut = credit*bnsIt->second.rate/100.0;
								credit = 0.0;
							}
							else
							{
								creditOut = -bnsIt->second.value;
								credit += bnsIt->second.value*100.0/bnsIt->second.rate;
							}

							sprintf(creditComm,"BCO%d@%s#%d-%d",bnsIt->second.rate,bnsIt->second.bnsCode,balSn,bnsIt->first);

							pAccount = m_manager->UserCreateAccount();
							if(m_manager->UserAccountRequest(user.login,pAccount) == MT_RET_OK)
							{
								user.credit = pAccount->Credit();
							}
							pAccount->Release();

							if(user.credit < 0.0000001)
							{
								credit = 0.0;
								break;
							}

							if((creditOut+user.credit) < -0.0000001)
							{
								creditOut = (-1.0)*user.credit;
								credit = 0.0;
							}

							bnsIt->second.value += creditOut;
							info.type = TT_BR_BALANCE;
							info.cmd = OP_CREDIT;
							info.orderby = user.login;
							info.price = creditOut;
							strcpy(info.comment,creditComm);
							info.expiration = bnsIt->second.bnsDay;
							if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
							{
								m_admin->LoggerOut(CmdOK,L"%s 'web': BCO %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),creditOut,user.login,(CStringW)creditComm);
								GetCreditNO(creditNo,user.login,creditComm);
								sprintf(creditDetail,"%d,%.2lf,%d,BCI%d@%s#,",creditNo,creditOut,bnsIt->second.bnsDay,bnsIt->second.rate,bnsIt->second.bnsCode);
								strCreditDetail = strCreditDetail + creditDetail;
							}
							else
							{
								m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCO %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),creditOut,user.login,(CStringW)creditComm);
							}
						}
					}
					
					if(strCreditDetail.size() != 0)
					{
						strCreditDetail = "," + strCreditDetail;
						tmpoutput.Format("OK\r\ndeposit successful!CREDITDETAIL=%s\r\nend",strCreditDetail.c_str());
						output = tmpoutput;
						return 0;
					}
				}
			}
			else
			{
				creditWay = 0;//Ĭ�ϲ�������
				GetIntParam(buffer,"CREDITWAY=",&creditWay);
				if(bReject == true)
				{
					if(bAllReject == true)
					{
						lTime = m_manager->TimeServer();
						time_t fromTime = 1433088000;//2015-06-01 00:00:00
						IMTDealArray * pTrades = NULL;
						pTrades = m_manager->DealCreateArray();
						int total = 0;
						double cxlCredit = 0.0;
						m_manager->DealRequest(user.login,fromTime,lTime,pTrades);
						total = pTrades->Total();

						for(int i = 0; i < total; i++)
						{
							IMTDeal * pTrade = pTrades->Next(i);

							if(pTrade->Action() != IMTDeal::EnDealAction::DEAL_CREDIT) continue;
							char  dealComment[64] = {0};
							strcpy(dealComment,(CString)pTrade->Comment());
							if(memcmp(dealComment,"BCO",3) != 0) continue;
							if(pTrade->Profit() > -0.0000001) continue;

							int creditSn = 0;
							GetIntParam(dealComment,"#",&creditSn);
							if(creditSn == cxlBalSn)
							{
								cxlCredit = -pTrade->Profit();
								BNSDetail bnsDetail = {0};
								bnsDetail.rate = atoi(&dealComment[3]);
								sscanf(dealComment,"%*[^@]@%[^#]",bnsDetail.bnsCode);

								int srcCreditInNo = 0;
								GetIntParam(dealComment,"-",&srcCreditInNo);
								TradeRecord cxlRecord = {0};
								GetCXRecord(cxlRecord,user.login,srcCreditInNo);
								if(cxlRecord.close_time != 0)
								{
									bnsDetail.bnsDay = cxlRecord.close_time;
								}
								else
								{
									bnsDetail.bnsDay = pTrade->Time();
								}

								sprintf(creditComm,"BCI%d@%s#%d-%d",bnsDetail.rate,bnsDetail.bnsCode,balSn,pTrade->Deal());
								info.type = TT_BR_BALANCE;
								info.cmd = OP_CREDIT;
								info.orderby = user.login;
								info.price = cxlCredit;
								strcpy(info.comment,creditComm);
								info.expiration = bnsDetail.bnsDay;
								if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
								{
									m_admin->LoggerOut(CmdOK,L"%s 'web': BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),creditNo,cxlCredit,user.login,(CStringW)creditComm);
								}
								else
								{
									m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),cxlCredit,user.login,(CStringW)creditComm);
								}
							}
						}
						pTrades->Release();
					}
				}
				else if(creditWay == 1)//bonus code
				{
					if(GetStrParam(buffer,"BNSCODE=",      bnsCode,             sizeof(bnsCode)-1)==FALSE)
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)ipStr.c_str(),user.login,deposit);
						output = "OK\r\nmiss BNSCODE\r\nend";
						return 0;
					}

					GetIntParam(buffer,"BNSDAY=",&bnsDay);
					if(bnsDay<=0)
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)ipStr.c_str(),user.login,deposit);
						output = "OK\r\ninvalid BNSDAY\r\nend";
						return 0;
					}
					GetIntParam(buffer,"BNSRATE=",&bnsRate);
					if(bnsRate<=0)
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI for '%d#%.2lf'",(CStringW)ipStr.c_str(),user.login,deposit);
						output = "OK\r\ninvalid BNSRATE\r\nend";
						return 0;
					}

					sprintf(creditComm,"BCI%d@%s#%d",bnsRate,bnsCode,balSn);
					credit = bnsRate*deposit/100.0;
					info.type = TT_BR_BALANCE;
					info.cmd = OP_CREDIT;
					info.orderby = user.login;
					info.price = credit;
					strcpy(info.comment,creditComm);
					info.expiration = bnsDay;
					if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
					}
					else
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
					}
				}
				else if(creditWay == 2)//credit details
				{
					if(GetStrParam(buffer,"CREDITDETAIL=",      creditDetail,             sizeof(creditDetail)-1)==FALSE)
					{
						m_admin->LoggerOut(CmdOK,L"%s 'web': no CREDITDETAIL to BCI for '%d#%.2lf'",(CStringW)ipStr.c_str(),user.login,deposit);
						output = "OK\r\nmiss CREDITDETAIL\r\nend";
						return 0;
					}

					string::size_type pos;
					string pattern = ",";
					strCreditDetail = creditDetail;
					int count = 0;
					for(int i=1; i<strCreditDetail.size(); i++)
					{
						pos=strCreditDetail.find(pattern,i);
						if(pos<strCreditDetail.size())
						{
						  string subStr = strCreditDetail.substr(i,pos-i);
						  if(count%4 == 0)
						  {
							  creditNo = atoi(subStr.c_str());
						  }
						  else if(count%4 == 1)
						  {
							  credit = atof(subStr.c_str());
							  credit = -credit;
						  }
						  else if(count%4 == 2)
						  {
							  bnsDay = atoi(subStr.c_str());
						  }
						  else
						  {
							  sprintf(creditComm,"%s%d-%d",subStr.c_str(),balSn,creditNo);
							  info.type = TT_BR_BALANCE;
							  info.cmd = OP_CREDIT;
							  info.orderby = user.login;
					          info.price = credit;
					          strcpy(info.comment,creditComm);
					          info.expiration = bnsDay;
							  if(m_manager->DealerBalance(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
							  {
								 m_admin->LoggerOut(CmdOK,L"%s 'web': BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
							  }
							  else
							  {
								 m_admin->LoggerOut(CmdOK,L"%s 'web': failed to BCI %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)creditComm);
							  }
						  }
						  i=pos+pattern.size()-1;
						  count++;
						}
					}
				}
			}

			output = "OK\r\ndeposit successful\r\nend";
			return 0;
		}
		
		break;
	case 2:
		if(GetIntParam(buffer,"LOGIN=",&user.login) ==FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		if(m_manager->UserDelete(user.login) != MT_RET_OK)
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': account '%d' deleted",(CStringW)ipStr.c_str(),user.login);
			output = "OK\r\ndelete account successful\r\nend";
			return 0;
		}
		else
		{
			output = "ERROR\r\ndelete account failed\r\nend";
			return 0;
		}

		break;
	case 3:
		if(GetIntParam(buffer,"LOGIN=",&user.login) ==FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		if(GetStrParam(buffer,"PASSWORD=",user.password,sizeof(user.password)-1) ==FALSE)
		{
			output = "ERROR\r\nmiss PASSWORD\r\nend";
			return 0;
		}

		if(GetIntParam(buffer,"INVESTOR=",&investor) ==FALSE)
		{
			output = "ERROR\r\nmiss INVESTOR\r\nend";
			return 0;
		}

		if(GetIntParam(buffer,"DROPKEY=",&drop_key) ==FALSE)
		{
			output = "ERROR\r\nmiss DROPKEY\r\nend";
			return 0;
		}

		//if(investor!=0)         investor =TRUE;				//xbc ��Ϊ  0.�����룬1.Ͷ�������룬2.ͬʱ��
		if(drop_key!=0)			drop_key =TRUE;
	
		if(CheckPassword(user.password)==FALSE)
		{
			output = "ERROR\r\ninvalid PASSWORD\r\nend";
			return 0;
		}

		//���INVESTOR��Ϊ2����ôͬʱ��
		if(investor == 2)
		{
			int bUser = m_manager->UserPasswordChange(IMTUser::USER_PASS_MAIN,user.login,(CStringW)user.password);
			int bInves = m_manager->UserPasswordChange(IMTUser::USER_PASS_INVESTOR,user.login,(CStringW)user.password);
			if(bUser == MT_RET_OK && bInves == MT_RET_OK)
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': set investor and master password for '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
				output = "OK\r\nchange both password successful\r\nend";
				return 0;
			}
			else
			{
				if(bUser == MT_RET_OK)
				{
					m_admin->LoggerOut(CmdOK,L"%s 'web': set master password for '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
					output = "ERROR\r\nchange master password successful but investor failed\r\nend";
					return 0;
				}
				else if(bInves == MT_RET_OK)
				{
					m_admin->LoggerOut(CmdOK,L"%s 'web': set investor password for '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
					output = "ERROR\r\nchange investor password successful but user failed\r\nend";
					return 0;
				}
				else
				{
					output = "ERROR\r\nchange both password failed\r\nend";
					return 0;
				}
			}
		}
		else if(investor == 0)
		{
			if(m_manager->UserPasswordChange(IMTUser::USER_PASS_MAIN,user.login,(CStringW)user.password) == MT_RET_OK)
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': set master password for '%d' [successful]",(CStringW)ipStr.c_str(),user.login);

				output = "OK\r\nchange user password successful\r\nend";
				return 0;
			}
			else
			{
				output = "ERROR\r\nchange user password failed\r\nend";
				return 0;
			}
		}
		else
		{
			if(m_manager->UserPasswordChange(IMTUser::USER_PASS_INVESTOR,user.login,(CStringW)user.password) == MT_RET_OK)
			{
		
				m_admin->LoggerOut(CmdOK,L"%s 'web': set investor password for '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
				output = "OK\r\nchange user password successful\r\nend";
				return 0;
			}
			else
			{
				output = "ERROR\r\nchange user password failed\r\nend";
				return 0;
			}
		}

		break;
	case 4:
		if(GetIntParam(buffer,"LOGIN=",&user.login) ==FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		if(GetStrParam(buffer,"PASSWORD=",user.password,sizeof(user.password)-1) ==FALSE)
		{
			output = "ERROR\r\nmiss PASSWORD\r\nend";
			return 0;
		}

		if(GetIntParam(buffer,"INVESTOR=",&investor) ==FALSE)
		{
			output = "ERROR\r\nmiss INVESTOR\r\nend";
			return 0;
		}

		if(investor!=0)         investor = TRUE;
		UINT passType;
		if(investor == FALSE)
			passType = IMTUser::USER_PASS_MAIN;
		else
			passType = IMTUser::USER_PASS_INVESTOR;
		
		if(m_manager->UserPasswordCheck(passType,user.login,(CStringW)user.password) == MT_RET_OK)
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': checking password of '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
			output = "OK\r\npassword is right\r\nend";
			return 0;
		}
		else
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': checking password of '%d' [failed]",(CStringW)ipStr.c_str(),user.login);
			output = "ERROR\r\npassword is wrong\r\nend";
			return 0;
		}

		break;
	case 5:
		if(GetIntParam(buffer,"LOGIN=",&user.login) == FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		if(GetFltParam(buffer,"CREDIT=",      &credit) == FALSE)
		{
			output = "ERROR\r\nmiss CREDIT\r\nend";
			return 0;
		}
	
		if(GetStrParam(buffer,"COMMENT=",      temp,             sizeof(temp)-1)==FALSE)
		{
			temp[0]=0;
		}

		pUser = m_manager->UserCreate();
		if(m_manager->UserRequest(user.login,pUser) == MT_RET_OK)
		{
			user.credit = pUser->Credit();
			pUser->Release();
		}
		else
		{
			pUser->Release();
			output = "ERROR\r\ncredit failed\r\nend";
			return 0;
		}

		if( credit < -0.0000001 && (user.credit+credit) < -0.0000001 )
		{
			output = "ERROR\r\ncredit failed\r\nend";
			return  0;
		}

		GetIntParam(buffer,"BNSDAY=",&bnsDay);
		if(bnsDay<=0)
		{
			lTime = m_manager->TimeServer()+180*24*3600;//Ĭ�ϵ�������180���

		}
		else
		{
			lTime = bnsDay;
		}

		info.type = TT_BR_BALANCE;
		info.cmd = OP_CREDIT;
		info.orderby = user.login;
		info.price = credit;
		strcpy(info.comment,temp);
		info.expiration = lTime;
		if( m_manager->DealerBalanceRaw(info.orderby,info.price,IMTDeal::EnDealAction::DEAL_CREDIT,(CStringW)info.comment,dealerID) == MT_RET_REQUEST_DONE)
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': changed credit %.2lf for '%d' - '%s'",(CStringW)ipStr.c_str(),credit,user.login,(CStringW)temp);
			output = "OK\r\ncredit successful\r\nend";
			return 0;
		}
		else
		{
			output = "ERROR\r\ncredit failed\r\nend";
			return 0;
		}
		break;
	case 6:
		if(GetIntParam(buffer,"LOGIN=",&user.login) ==FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		pUser = m_manager->UserCreate();
		if(m_manager->UserRequest(user.login,pUser) != MT_RET_OK)
		{
			pUser->Release();
			output = "ERROR\r\ninvalid LOGIN\r\nend";
			return 0;
		}
		
		if(GetStrParam(buffer,"GROUP=",group_name,sizeof(group_name)-1) == TRUE)
		{
		
			pGroup = m_manager->GroupCreate();
			pGroup->Group((CStringW)group_name);
			if(m_manager->GroupGet((CStringW)group_name,pGroup)!=MT_RET_OK)
			{
				pUser->Release();
				pGroup->Release();
				output = "ERROR\r\ninvalid GROUP\r\nend";
				return 0;
			}
			else
			{
				pGroup->Release();
				pUser->Group((CStringW)group_name);
			}
		}
		
		userRights = pUser->Rights();

		if(GetIntParam(buffer,"ENABLE=",&user.enable) == TRUE)
		{
			if(user.enable!=0) 
				userRights = userRights|IMTUser::USER_RIGHT_ENABLED;
			else
				userRights = userRights&(~IMTUser::USER_RIGHT_ENABLED);
		}

		if(GetIntParam(buffer,"CHANGE_PASSWORD=",&user.enable_change_password) == TRUE)
		{
			if(user.enable_change_password!=0)
				userRights = userRights|IMTUser::USER_RIGHT_PASSWORD;
			else
				userRights = userRights&(~IMTUser::USER_RIGHT_PASSWORD);
		}

		if(GetIntParam(buffer,"COLOR=",(int*)&user.user_color) == TRUE)
		{
			pUser->Color(user.user_color);
		}
		
		if(GetStrParam(buffer,"NAME=",          user.name,             sizeof(user.name)-1) == TRUE)
		{
			if(user.name[0]==0)
			{
				pUser->Release();
				output = "ERROR\r\ninvalid NAME\r\nend";
				return 0;
			}
			else
			{
				pUser->Name((CStringW)user.name);
			}
		}

		if(GetStrParam(buffer,"EMAIL=",         user.email,            sizeof(user.email)-1) == TRUE)
		{
			pUser->EMail((CStringW)user.email);
		}
#ifndef VEXNB
		if(GetStrParam(buffer,"COUNTRY=",       user.country,          sizeof(user.country)-1) == TRUE)
		{
			pUser->Country((CStringW)user.country);
		}
#endif
		if(GetStrParam(buffer,"STATE=",         user.state,            sizeof(user.state)-1) == TRUE)
		{
			pUser->State(wState);
		}

		if(GetStrParam(buffer,"CITY=",          user.city,             sizeof(user.city)-1) == TRUE)
		{
			pUser->City(wCity);
		}

		if(GetStrParam(buffer,"ADDRESS=",       user.address,          sizeof(user.address)-1) == TRUE)
		{
			pUser->Address((CStringW)user.address);
		}

		if(GetStrParam(buffer,"COMMENT=",       user.comment,          sizeof(user.comment)-1) == TRUE)
		{
			pUser->Comment((CStringW)user.comment);
		}

		if(GetStrParam(buffer,"PHONE=",         user.phone,            sizeof(user.phone)-1) == TRUE)
		{
			pUser->Phone((CStringW)user.phone);
		}

		if(GetStrParam(buffer,"PHONE_PASSWORD=",user.password_phone,   sizeof(user.password_phone)-1) == TRUE)
		{
			pUser->PhonePassword((CStringW)user.password_phone);
		}

		if(GetStrParam(buffer,"STATUS=",        user.status,           sizeof(user.status)-1) == TRUE)
		{
			pUser->Status((CStringW)user.status);
		}

		if(GetStrParam(buffer,"ZIPCODE=",       user.zipcode,          sizeof(user.zipcode)-1) == TRUE)
		{
			pUser->ZIPCode((CStringW)user.zipcode);
		}

		if(GetStrParam(buffer,"ID=",            user.id,               sizeof(user.id)-1) == TRUE)
		{
			pUser->ID((CStringW)user.id);
		}

		if(GetIntParam(buffer,"LANGUAGE=",      &user_lang) == TRUE)
		{
			pUser->Language(user_lang);
		}

		if(GetIntParam(buffer,"LEVERAGE=",     &user.leverage) == TRUE)
		{
			if(user.leverage<1)
			{
				pUser->Release();
				output = "ERROR\r\ninvalid LEVERAGE\r\nend";
				return 0;
			}
			else
			{
				pUser->Leverage(user.leverage);
			}
		}

		if(GetIntParam(buffer,"AGENT=",        &user.agent_account) == TRUE)
		{
			if(user.agent_account<=0)
			{
				pUser->Release();
				output = "ERROR\r\ninvalid AGENT\r\nend";
				return 0;
			}
			else
			{
				pUser->Agent(user.agent_account);
			}
		}

		if(GetIntParam(buffer,"SEND_REPORTS=", &user.send_reports) == TRUE)
		{
			if(user.send_reports!=0)
			{
				userRights = userRights|IMTUser::USER_RIGHT_REPORTS;
			}
			else
			{
				userRights = userRights&(~IMTUser::USER_RIGHT_REPORTS);
			}
		}
		
		if(GetIntParam(buffer,"READONLY=",		&user.enable_read_only) == TRUE)
		{
			if(user.enable_read_only!=0)
			{
				userRights = userRights|IMTUser::USER_RIGHT_TRADE_DISABLED;
			}
			else
			{
				userRights = userRights&(~IMTUser::USER_RIGHT_TRADE_DISABLED);
			}
		}
		
		if(GetFltParam(buffer,"INTEREST_RATE=",			&user.interestrate) == TRUE)
		{
			if(user.interestrate<-0.0000001 || user.interestrate>(100+0.0000001))
			{
				pUser->Release();
				output = "ERROR\r\ninvalid INTEREST_RATE\r\nend";
				return 0;
			}
		}
		
		if(GetIntParam(buffer,"LIMIT=",         &eqLimit)==TRUE)
		{
			pUser->ApiDataSet(1,1,(UINT64)eqLimit);
		}

		pUser->Rights(userRights);

		if(m_manager->UserUpdate(pUser) == MT_RET_OK)
		{
			pUser->Release();
			m_admin->LoggerOut(CmdOK,L"%s 'web': update information of '%d' [successful]",(CStringW)ipStr.c_str(),user.login);
			output = "OK\r\nuser update successful\r\nend";
			return 0;
		}
		else
		{
			pUser->Release();
			output = "ERROR\r\nuser update failed\r\nend";
			return 0;
		}

	    break;

	case 7:
		if(GetIntParam(buffer,"LOGIN=",&user.login) == FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		pUser = m_manager->UserCreate();
		if(m_manager->UserRequest(user.login,pUser) == MT_RET_OK)
		{
			UINT64 userRight = pUser->Rights();
			pUser->Release();
			if((userRight&IMTUser::USER_RIGHT_ENABLED) != 0)
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': check state of '%d' [enable]",(CStringW)ipStr.c_str(),user.login);
				output = "OK\r\nuser check successful\r\nend";
				return 0;
			}
			else
			{
				m_admin->LoggerOut(CmdOK,L"%s 'web': check state of '%d' [disable]",(CStringW)ipStr.c_str(),user.login);
				output = "Disable\r\nuser check successful\r\nend";
				return 0;
			}
		}
		else
		{
			pUser->Release();
			output = "ERROR\r\nuser check failed\r\nend";
			return 0;
		}
		break;

	case 8:
		if(GetIntParam(buffer,"LOGIN=",&user.login) == FALSE)
		{
			output = "ERROR\r\nmiss LOGIN\r\nend";
			return 0;
		}

		if(GetFltParam(buffer,"WITHDRAWAL=",&withdrawal) == FALSE)
		{
			output = "ERROR\r\nmiss WITHDRAWAL\r\nend";
			return 0;
		}

		pAccount = m_manager->UserCreateAccount();
		if( m_manager->UserAccountRequest(user.login,pAccount) != MT_RET_OK )
		{
			pAccount->Release();
			output = "ERROR\r\nfreemargin check failed\r\nend";
			return  0;
		}

		margin = pAccount->Margin();
		freemargin = pAccount->MarginFree();
		equity = pAccount->Equity();
		credit = pAccount->Credit();
		if((freemargin-withdrawal-credit)>-0.0000001)
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': check free margin of '%d' [enable]",(CStringW)ipStr.c_str(),user.login);
			output = "OK\r\nfreemargin check successful\r\nend";
			return 0;
		}
		else
		{
			m_admin->LoggerOut(CmdOK,L"%s 'web': check free margin of '%d' [disable]",(CStringW)ipStr.c_str(),user.login);
			output = "Disable\r\nfreemargin check successful\r\nend";
			return 0;
		}
		
		break;
  }

   return 0;
}

void MT5Manager::OnDisconnect(void)
  {
//--- need to reconnect
  if(InterlockedExchange(&m_bConnected,false) == false)
	  return;

  AfxBeginThread(MT5Manager::Reconnect,NULL);
  }

UINT MT5Manager::Reconnect(VOID* param)
{
	g_mt5manager.Logout();
	while(g_mt5manager.Login() == false)
	{
		Sleep(5000);
	}

	return TRUE;
}

//+------------------------------------------------------------------+
//| Initialize library                                               |
//+------------------------------------------------------------------+
bool MT5Manager::Initialize()
  {
   MTAPIRES  res=MT_RET_OK_NONE;
   CMTStr256 message;

//--- loading manager API
   if((res=m_factory.Initialize(L".\\"))!=MT_RET_OK)
     {
      return(false);
	}

//--- creating manager interface
   if((res=m_factory.CreateManager(MTManagerAPIVersion,&m_manager))!=MT_RET_OK || (res=m_factory.CreateAdmin(MTManagerAPIVersion,&m_admin))!=MT_RET_OK)
     {
      m_factory.Shutdown();
      return(false);
     }

   m_manager->Subscribe(this);
   m_admin->Subscribe(this);
//--- create deal array
   if(!(m_deal_array=m_manager->DealCreateArray()))
     {
      m_manager->LoggerOut(MTLogErr,L"DealCreateArray fail");
      return(false);
     }
//--- create user interface
   if(!(m_user=m_manager->UserCreate()))
     {
      m_manager->LoggerOut(MTLogErr,L"UserCreate fail");
      return(false);
     }
//--- create account interface
   if(!(m_account=m_manager->UserCreateAccount()))
     {
      m_manager->LoggerOut(MTLogErr,L"UserCreateAccount fail");
      return(false);
     }


//--- all right
   return(true);
  }
//+------------------------------------------------------------------+
//| Login                                                            |
//+------------------------------------------------------------------+
bool MT5Manager::Login()
  {

	cout<<"MT5Manager - Connect"<<endl;
//--- connect

	m_usrID = 1010;
	m_passwd = "Aa@111111";
    MTAPIRES res=m_manager->Connect((CStringW)m_serverAddr,m_usrID,(CStringW)m_passwd,NULL,IMTManagerAPI::PUMP_MODE_FULL,MT5_CONNECT_TIMEOUT);

   if(res!=MT_RET_OK)
     {
	 cout<<"Connection manager failed!"<<endl;
      m_manager->LoggerOut(MTLogErr,L"Connection manager failed (%u)",res);
      return(false);
     }

  res = m_admin->Connect((CStringW)m_serverAddr,m_usrID,(CStringW)m_passwd,NULL,0,MT5_CONNECT_TIMEOUT);
   if(res!=MT_RET_OK)
     {
	cout<<"Connection manager failed2!"<<endl;
	  m_manager->Disconnect();
      m_admin->LoggerOut(MTLogErr,L"Connection admin failed (%u)",res);
      return(false);
     }

   InterlockedExchange(&m_bConnected,true);

    cout<<"Connection mt5 server successful!"<<endl;
    m_admin->LoggerOut(MTLogOK,L"Connection mt5 server successful");

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	UINT64 login = 100009; //123456789;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DealRequest ok

	/*
	IMTDealArray* deals = nullptr;
	std::tm tm_from = {};
	tm_from.tm_year = 2020 - 1900;  // 年份從 1900 開始算
	tm_from.tm_mon = 0;             // 1月是0
	tm_from.tm_mday = 1;            // 1號
	tm_from.tm_hour = 0;
	tm_from.tm_min = 0;
	tm_from.tm_sec = 0;

	// 轉成 Unix Timestamp
	INT64 from = static_cast<INT64>(mktime(&tm_from));
	//INT64 from = 0;            // 從 1970-01-01 開始
	INT64 to = time(nullptr);  // 到現在時間

	// Create a deals array
	deals = m_manager->DealCreateArray();
	if (!deals) {
		printf("Failed to create deal array\n");
		return MT_RET_ERR_MEM;
	}

	// Request deals
	res = m_manager->DealRequest(login, from, to, deals);
	if (res != MT_RET_OK) {
		printf("DealsRequest failed, code: %u\n", res);
		deals->Release();
		return res;
	}

	// Loop through the deals
	for (UINT i = 0; i < deals->Total(); ++i) {
		const IMTDeal* deal = deals->Next(i);
		if (!deal) continue;

		printf("Deal ID: %llu\n", deal->Deal());
		printf("Symbol: %s\n", deal->Symbol());
		printf("Volume: %.2f\n", deal->Volume());
		printf("Price: %.5f\n", deal->Price());
		printf("Type: %d\n", deal->Action()); // BUY, SELL, etc.
		printf("Profit: %.2f\n", deal->Profit());
		printf("----------------------\n");
	}
	deals->Release();
	*/
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//UserRequest ok
	/*
    IMTUser* user = m_manager->UserCreate();
    if (!user) {
        std::cerr << "Failed to create user object." << std::endl;
        return(true);;
    }

    // Request user data using login
    res = m_manager->UserRequest(login, user);
    if (res != MT_RET_OK) {
        std::cerr << "UserRequest failed, code: " << res << std::endl;
        user->Release();
        return(true);
    }

    // Display user information
    std::wcout << L"Login: " << user->Login() << std::endl;
    std::wcout << L"Name: " << user->Name() << std::endl;
    std::wcout << L"Group: " << user->Group() << std::endl;
    std::wcout << L"Email: " << user->EMail() << std::endl;
    std::wcout << L"Phone: " << user->Phone() << std::endl;
    std::wcout << L"Country: " << user->Country() << std::endl;

    // Release object
    user->Release();
	*/
	

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   // UserAccountRequest ok

  /*

    // Create an account object
    IMTAccount* account = m_manager->UserCreateAccount();
    if (!account) {
        std::cerr << "Failed to create account object." << std::endl;
        return(true);
    }

    // Request account data
    res = m_manager->UserAccountRequest(login, account);
    if (res != MT_RET_OK) {
        std::cerr << "UserAccountRequest failed, code: " << res << std::endl;
        account->Release();
        return(true);
    }

    // Print some basic account info
    std::wcout << L"Login: " << account->Login() << std::endl;
    std::wcout << L"Balance: " << account->Balance() << std::endl;
    std::wcout << L"Equity: " << account->Equity() << std::endl;
    std::wcout << L"Margin: " << account->Margin() << std::endl;
    std::wcout << L"Free Margin: " << account->MarginFree() << std::endl;

    // Release the object
    account->Release();
	*/
	

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   // UserUpdate (error - 7)
  /*

	// 1. 创建一个用户对象
	UINT64 login = 100009;
    IMTUser* user = m_manager->UserCreate();
    if (!user) {
        std::cerr << "Failed to create user object." << std::endl;
        return(true);
    }

    // 2. 请求用户数据
    res = m_manager->UserRequest(login, user);
    if (res != MT_RET_OK) {
        std::cerr << "UserRequest failed, code: " << res << std::endl;
        user->Release();
        return(true);
    }

	// 7 = 参数错误
	// 13 - 无效请求” Invalid Request

	std::cerr << "User Name: " <<   user->Name() << std::endl;
	 CString new_name = CString(user->Name());

    // 3. 修改需要更新的字段（这里只改名字，你也可以改邮箱、组别等等）
    user->Name((CStringW)new_name);  // 更新名字

    // 4. 把修改过的 user 对象提交回服务器
    res = m_manager->UserUpdate(user);
    if (res != MT_RET_OK) {
        std::cerr << "UserRecordUpdate failed, code: " << res << std::endl;
    } else {
        std::cout << "UserRecordUpdate succeeded!" << std::endl;
    }

    // 5. 释放资源
    user->Release();
	*/

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   // UserRequestArray - fail!
   /*
	IMTUserArray* user_array = m_manager->UserCreateArray();
	CString group = "";//"real";

	if (!user_array) {
		printf("Failed to create user array\n");
		return(true); // return MT_RET_ERR_MEM; 
	}

	res = m_manager->UserRequestArray((LPCWSTR)"", user_array);
	if (res != MT_RET_OK) {
		// Handle failure (e.g., no users retrieved, or error occurred)
		printf("Failed to fetch users. Error code: %d\n", res);
		user_array->Release();
		return(true);
	}
	UINT total = user_array->Total();  

	for (UINT i = 0; i < total; i++) {
		IMTUser* user = user_array->Next(i);
		if (user) {
			// Print user details (e.g., login, group, and name)
			wprintf(L"User Login: %I64u, Group: %s, Name: %s\n", user->Login(), user->Group(), user->Name());

			if (user_array) {
				user_array->Add(user);  // Assuming IMTUserArray has an Add method to add users to it
			}
		}
	}
	user_array->Release();
	*/


   return(true);
  }


void MT5Manager::Logout()
  {
//--- disconnect manager
   if(m_manager)
      m_manager->Disconnect();

    if(m_admin)
      m_admin->Disconnect();
  }
//+------------------------------------------------------------------+
//| Shutdown                                                         |
//+------------------------------------------------------------------+
void MT5Manager::Shutdown()
  {
   if(m_deal_array)
     {
      m_deal_array->Release();
      m_deal_array=NULL;
     }
   if(m_manager)
     {
      m_manager->Release();
      m_manager=NULL;
     }
   if(m_user)
     {
      m_user->Release();
      m_user=NULL;
     }
   if(m_account)
     {
      m_account->Release();
      m_account=NULL;
     }
   if(m_admin)
   {
	  m_admin->Release();
      m_admin=NULL;
   }
   m_factory.Shutdown();
  }
//+------------------------------------------------------------------+
//| Get array of dealer balance operation                            |
//+------------------------------------------------------------------+
bool MT5Manager::GetUserDeal(IMTDealArray*& deals,const UINT64 login,SYSTEMTIME &time_from,SYSTEMTIME &time_to)
  {
//--- request array
   MTAPIRES res=m_manager->DealRequest(login,SMTTime::STToTime(time_from),SMTTime::STToTime(time_to),m_deal_array);
   if(res!=MT_RET_OK)
     {
      m_manager->LoggerOut(MTLogErr,L"DealRequest fail(%u)",res);
      return(false);
     }
//---
   deals=m_deal_array;
   return(true);
  }
//+------------------------------------------------------------------+
//| Get user info string                                             |
//+------------------------------------------------------------------+
bool MT5Manager::GetUserInfo(UINT64 login,CMTStr &str)
  {
//--- request user from server
   m_user->Clear();
   MTAPIRES res=m_manager->UserRequest(login,m_user);
   if(res!=MT_RET_OK)
     {
      m_manager->LoggerOut(MTLogErr,L"UserRequest error (%u)",res);
      return(false);
     }
//--- format string
   str.Format(L"%s,%I64u,%s,1:%u",m_user->Name(),m_user->Login(),m_user->Group(),m_user->Leverage());
//---
   return(true);
  }
//+------------------------------------------------------------------+
//| Get user info string                                             |
//+------------------------------------------------------------------+
bool MT5Manager::GetAccountInfo(UINT64 login,CMTStr &str)
  {
//--- request account from server
   m_account->Clear();
   MTAPIRES res=m_manager->UserAccountRequest(login,m_account);
   if(res!=MT_RET_OK)
     {
      m_manager->LoggerOut(MTLogErr,L"UserAccountRequest error (%u)",res);
      return(false);
     }
//--- format string
   str.Format(L"Balance: %.2lf  Equity: %.2lf  Margin:%.2lf  Free: %.2lf",m_account->Balance(),m_account->Equity(),m_account->Margin(),m_account->MarginFree());
//---
   return(true);
  }
//+------------------------------------------------------------------+
//| Dealer operation                                                 |
//+------------------------------------------------------------------+
bool MT5Manager::DealerBalance(const UINT64 login,const double amount,const UINT type,const LPCWSTR comment,bool deposit)
  {
   UINT64 deal_id=0;
//--- dealer operation
   MTAPIRES res=m_manager->DealerBalance(login,deposit?amount:-amount,type,comment,deal_id);
   if(res!=MT_RET_REQUEST_DONE)
     {
      m_manager->LoggerOut(MTLogErr,L"DealerBalance failed (%u)",res);
      return(false);
     }
//---
   return(true);
  }
//+------------------------------------------------------------------+
