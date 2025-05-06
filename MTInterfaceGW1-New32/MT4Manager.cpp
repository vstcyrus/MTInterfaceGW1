#include "StdAfx.h"
#include "MT4Manager.h"
#include "MessageCenter.h"

extern CString					g_currentPath;


void MT4Manager::Out(const int code,LPCSTR ip,LPCSTR msg,...)
  {
   char buffer[1024];
//---- check
   if(msg==NULL) return;
//---- format string
   va_list arg_ptr;
   va_start(arg_ptr,msg);
   _vsnprintf(buffer,sizeof(buffer)-1,msg,arg_ptr);
   va_end(arg_ptr);
//---- out to server log
   m_manager->LogsOut(code,ip,buffer);
  }

CManagerFactory MT4Manager::m_factory("mtmanapi.dll");

void __stdcall MT4Manager::OnPumping(int code)
{
}

bool MT4Manager::Init()
{
	WSADATA wsa;
    if(WSAStartup(0x0202,&wsa)!=0)
	{
		printf("Start up winsocket failed!\n");
		return false;
	}

	if(m_factory.IsValid()==FALSE 
		|| (m_pumping=m_factory.Create(ManAPIVersion))==NULL)
    {
		printf("Create MT4:%s pumping interface failure!\n",m_serverAddr.GetBuffer());
		return false;
	}

	if(m_factory.IsValid()==FALSE 
		|| (m_manager=m_factory.Create(ManAPIVersion))==NULL)
    {
		printf("Create MT4:%s manager interface failure!\n",m_serverAddr.GetBuffer());
		return false;
	}

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

	Ping();

	return true;
}

bool MT4Manager::Ping()
{
	m_bConnected = true;

	//ping命令不影响其它线程的mt4操作，可以不加锁
	if(m_pumping->Ping()!=RET_OK)
	{
		m_pumping->Disconnect();
		if(m_pumping->Connect(m_serverAddr.GetBuffer())!=RET_OK || m_pumping->Login(m_usrID,m_passwd.GetBuffer())!=RET_OK)
		{
			printf("MT4:%s pumping connection login failure!\n",m_serverAddr.GetBuffer());
			m_pumping->Disconnect();
			m_bConnected = false;
			return m_bConnected;
		}
		else if(m_pumping->PumpingSwitch(OnPumping,NULL,0,0) != RET_OK)
		{
			printf("MT4:%s pumping switch failure!\n",m_serverAddr.GetBuffer());
			m_pumping->Disconnect();
			m_bConnected = false;
			return m_bConnected;
		}
	}

	if(m_manager->Ping()!=RET_OK)
	{
		m_manager->Disconnect();
		if(m_manager->Connect(m_serverAddr.GetBuffer())!=RET_OK || m_manager->Login(m_usrID,m_passwd.GetBuffer())!=RET_OK)
		{
			printf("MT4:%s manager connection login failure!\n",m_serverAddr.GetBuffer());
			m_manager->Disconnect();
			m_bConnected = false;
			return m_bConnected;
		}
	}

	return m_bConnected;
}

MT4Manager::~MT4Manager()
{
	if(m_pumping != NULL)
	{
		m_pumping->Release();
    }

	if(m_manager != NULL)
	{
		m_manager->Release();
    }

	WSACleanup();
}

bool MT4Manager::GetOnlineUser(string & output)
{
	int userOnline=0;
	OnlineRecord * pOnline = NULL;

	m_lock.Lock();
	pOnline = m_pumping->OnlineGet(&userOnline);
	CString tmp="";

	for(int j = 0; j < userOnline; j++)
	{
		if(output.size() > (OUT_MSG_LEN-100))
		{
			break;
		}

		tmp.Format("LOGIN=%d|",pOnline[j].login);
		output += tmp;
	}
	m_pumping->MemFree(pOnline);
	m_lock.Unlock();

	return true;
}

bool MT4Manager::GetEndOfDay(string & output)
{
	bool ret = false;

	m_lock.Lock();
	ConCommon con = {0};
	if(m_manager->CfgRequestCommon(&con) == RET_OK)
	{
		CString tmp="";
		tmp.Format("%02d:%02d",con.endhour,con.endminute);
		output += tmp.GetBuffer();
		ret = true;
	}

	m_lock.Unlock();

	return ret;
}

bool MT4Manager::GetBalance(int login,string & output)
{
	UserRecord user = {0};

	m_lock.Lock();

	int userTotal = 1;
	UserRecord * pUsers = NULL;
	pUsers = m_manager->UserRecordsRequest(&login,&userTotal);
	if(pUsers != NULL)
	{
		memcpy(&user,&pUsers[0],sizeof(UserRecord));
		m_manager->MemFree(pUsers);
	}
	else
	{
		m_lock.Unlock();
		return false;
	}
	m_lock.Unlock();

	CString tmp="";
	tmp.Format("LOGIN=%d|",login);
	output += tmp;

	tmp.Format("BALANCE=%.2lf|",user.balance);
	output += tmp;

	tmp.Format("CREDIT=%.2lf|",user.credit);
	output += tmp;

	return true;
}

void  MT4Manager::GetCXRecord(TradeRecord & trade,int login,int order)
{
	time_t toTime = m_manager->ServerTime();
	if(toTime == 0) return;

	time_t frTime = toTime - 180*24*3600;//只处理半年内的取消？

	int total = 0;
	TradeRecord * pTrades = NULL;
	pTrades = m_manager->TradesUserHistory(login,frTime,toTime,&total);

	for(int i = 0; i < total; i++)
	{
		if(pTrades[i].order == order)
		{
			memcpy(&trade,&pTrades[i],sizeof(TradeRecord));
			break;
		}
	}

	if(pTrades != NULL) m_manager->MemFree(pTrades);
}

void MT4Manager::GetCreditNO(int & order, int login, LPCSTR comment)
{
	time_t toTime = m_manager->ServerTime();
	if(toTime == 0) return;

	time_t frTime = toTime - 3600*24;//只查询今天生成的订单

	int total = 0;
	TradeRecord * pTrades = NULL;
	pTrades = m_manager->TradesUserHistory(login,frTime,toTime,&total);

	for(int i = 0; i < total; i++)
	{
		if( strcmp(pTrades[i].comment,comment) == 0 )
		{
			order = pTrades[i].order;
			break;
		}
	}

	if(pTrades != NULL) m_manager->MemFree(pTrades);
}

int MT4Manager::ProcessWebIF(const ULONG ip,char *buffer,const int size,string & output)
{
   UserRecord user ={0};
   ConGroup   group={0};
   char       group_name[16]={0};
   char       temp[256]={0};
   int        grpId = 0;
   double     deposit = 0.0;
   double     credit = 0.0;
   double     balance = 0.0;
   int	      cmd = 0;
   BOOL       investor,drop_key;
   time_t	  lTime;
   //int		  balanceNo = 0;
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
   UserRecord * pUsers = NULL;
   int userTotal = 1;

   //UserInfo	usr = {0};
   UserRecord	usrinf = {0};
   double margin,freemargin,equity,withdrawal;

   in_addr addr; 
   addr.s_addr = ip; 
   string ipStr = inet_ntoa(addr); 

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
//---- receive master password and check it
   /*if(GetStrParam(buffer,"MASTER=",temp,sizeof(temp)-1)==FALSE)
   {
      output = "ERROR\r\nmiss MASTER\r\nend";
	  return 0;
   }
   if(strcmp(temp,m_password)!=0)
   {
      output = "ERROR\r\ninvalid MASTER\r\nend";
	  return 0;
   }

   if(CheckGroup(m_ipList,ipStr.c_str())==FALSE)
	{
		output = "ERROR\r\nillegal IP\r\nend\";
		return 0;
	}*/

//---- receive IP
    /*if(GetStrParam(buffer,"IP=",temp,sizeof(temp)-1)==FALSE) 
      return _snprintf(buffer,size-1,"ERROR\r\nmiss IP\r\nend\r\n");

//---- check IP by antiflood
  if(CheckFlooder(temp)==FALSE)
      return _snprintf(buffer,size-1,
             "ERROR\r\nIP is blocked. Please wait %d secs and try again.\r\nend\r\n",
             ANTIFLOOD_PERIOD);*/

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

  Out(CmdOK,ipStr.c_str(),"'web': receive command '%s'",outStr.c_str());

  switch(cmd)
  {
	case 0:
		GetStrParam(buffer,"GROUP=",group_name,sizeof(group_name)-1);

		//---- receive group overview
		if(m_pumping->GroupRecordGet(group_name,&group)!=RET_OK)
		{
			output = "ERROR\r\ninvalid GROUP\r\nend";
			return 0;
		}

		COPY_STR(user.group,group_name);
		
		
		user.enable                =TRUE;
		user.enable_change_password=TRUE;
		user.leverage              =group.default_leverage;
		user.user_color            =0xff000000;
		user.enable_read_only      = FALSE;
		user.send_reports          = TRUE;
		GetIntParam(buffer,"ENABLE=",&user.enable);
		GetIntParam(buffer,"CHANGE_PASSWORD=",&user.enable_change_password);
		GetIntParam(buffer,"OTP=",&user.enable_otp);
		GetIntParam(buffer,"LEVERAGE=",     &user.leverage);
		GetIntParam(buffer,"READONLY=",		&user.enable_read_only);
		GetIntParam(buffer,"SEND_REPORTS=", &user.send_reports);
		GetIntParam(buffer,"COLOR=", (int*)&user.user_color);
		
		
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

		//---- checks
		if(user.send_reports!=0)         user.send_reports =TRUE;
		if(user.enable!=0)				 user.enable =TRUE;
		if(user.enable_change_password!=0)         user.enable_change_password =TRUE;
		if(user.enable_read_only!=0)     user.enable_read_only =TRUE;
		if(user.enable_otp!=0)     user.enable_otp =TRUE;
		if(user.leverage<1)              user.leverage=1;

		if(user.interestrate<-0.0000001 || user.interestrate>(100+0.0000001))
		{
			output = "ERROR\r\ninvalid INTEREST_RATE\r\nend";
			return 0;
		}

		//---- check complexity of password
		if(CheckPassword(user.password)==FALSE)
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
	   
		//---- creating new account
		if(m_manager->UserRecordNew(&user)!=RET_OK)
		{
			output = "ERROR\r\naccount create failed\r\nend";
			return 0;
		}
		else
		{
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
				if( (m_manager->TradeTransaction(&info)) == RET_OK )
				{
					Out(CmdOK,ipStr.c_str(),"'web': new account '%d' - %s",user.login,user.name);
					Out(CmdOK,ipStr.c_str(),"'web': new and balance %.2lf for '%d' - '%s'",balance,user.login,temp);

					GetIntParam(buffer,"CREDITWAY=",&creditWay);
					if(creditWay == 1)//bonus code
					{
						if(GetStrParam(buffer,"BNSCODE=",      bnsCode,             sizeof(bnsCode)-1)==FALSE)
						{
							Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\nmiss BNSCODE\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}

						GetIntParam(buffer,"BNSDAY=",&bnsDay);
						if(bnsDay<=0)
						{
							Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,balance);
							tmpoutput.Format("OK\r\nLOGIN=%d\r\ninvalid BNSDAY\r\nend",user.login);
							output = tmpoutput;
							return 0;
						}
						GetIntParam(buffer,"BNSRATE=",&bnsRate);
						if(bnsRate<=0)
						{
							Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,balance);
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

						if((m_manager->TradeTransaction(&info)) == RET_OK)
						{
							Out(CmdOK,ipStr.c_str(),"'web': BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
						}
						else
						{
							Out(CmdOK,ipStr.c_str(),"'web': failed to BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
						}
					}
					else if(creditWay == 2)//credit details
					{
						if(GetStrParam(buffer,"CREDITDETAIL=",      creditDetail,             sizeof(creditDetail)-1)==FALSE)
						{
							Out(CmdOK,ipStr.c_str(),"'web': no CREDITDETAIL to BCI for '%d#%.2lf'",user.login,balance);
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
								  if((m_manager->TradeTransaction(&info)) == RET_OK)
								  {
									 Out(CmdOK,ipStr.c_str(),"'web': BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
								  }
								  else
								  {
									 Out(CmdOK,ipStr.c_str(),"'web': failed to BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
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
					gcInfo.command = GROUP_DELETE;
					gcInfo.len = 1;
					m_manager->UsersGroupOp(&gcInfo,&user.login);
					output = "ERROR\r\naccount create failed\r\nend";
					return 0;
				}
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': new account '%d' - %s",user.login,user.name);
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

				Out(CmdOK,ipStr.c_str(),"'web': start to cancel balSn#%d",cxlBalSn);
			}
			else
			{
				bAllReject = false;
				Out(CmdOK,ipStr.c_str(),"'web': start to balSn#%d without handle credit",cxlBalSn);
			}
		}
		
		GetIntParam(buffer,"FORCE=",&force);

		if(deposit<-0.0000001)
		{
			if(m_pumping->UserRecordGet(user.login,&user) != RET_OK)
			{
				output = "ERROR\r\n UserRecordGet deposit failed\r\nend";
				return 0;
			}

			MarginLevel marginLevel = {0};
			//if(m_pumping->MarginLevelGet(user.login,user.group,&marginLevel) == RET_OK)
			if(m_manager->MarginLevelRequest(user.login,&marginLevel) == RET_OK)
			{
				Out(CmdOK,ipStr.c_str(),"'MarginLevelGet': login %d group: %s",user.login,user.group);
				margin = marginLevel.margin;
				freemargin = marginLevel.margin_free;
				equity = marginLevel.equity;
				if(force == FALSE)
				{
					float tmp = freemargin+deposit-usrinf.credit;
					if( tmp<-0.0000001 )
					{
						output = "ERROR\r\nfreemargin deposit failed\r\nend";
						return 0;
					}
				}
				else
				{
					/*float tmp = equity+deposit-usrinf.credit;
					if( tmp<-0.0000001 )
						return _snprintf(buffer,size-1,"ERROR\r\ndeposit failed\r\nend\r\n");*/
				}
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': receive command '%s'",outStr.c_str());
				output = "ERROR\r\nMarginLevelGet deposit failed\r\nend";
				return 0;
			}
		}

		GetIntParam(buffer,"CREDITWAY=",&creditWay);
		if(creditWay >= 3 && creditWay < 6 && deposit > 0.0000001)//BZB
		{
			GetIntParam(temp,"-",&dpTicket);
			map<int,BNSDetail> bnsMap;
			map<int,BNSDetail>::iterator bnsIt;
			lTime = m_manager->ServerTime();
			time_t fromTime;

			if(creditWay == 3)
				fromTime = 1262275200;//2010-01-01 00:00:00
			else
				fromTime = 1433088000;//2015-06-01 00:00:00

			TradeRecord * pTrades = NULL;
			int total = 0;
			pTrades = m_manager->TradesUserHistory(user.login,fromTime,lTime,&total);
			for(int i = 0; i < total; i++)
			{
				if(pTrades[i].cmd != OP_CREDIT) continue;
				if(pTrades[i].order > dpTicket && pTrades[i].profit > 0.0000001) continue;//不处理入金流水号之后的赠金

				int bciNo = 0;
				BNSDetail bnsDetail = {0};
				string comment = pTrades[i].comment;
				if(memcmp(pTrades[i].comment,"BCI",3) == 0 || memcmp(pTrades[i].comment,"Credit In",9) == 0)
				{
					bnsIt = bnsMap.find(pTrades[i].order);
					if(bnsIt == bnsMap.end())
					{
						if(memcmp(pTrades[i].comment,"BCI",3) == 0)
						{
							bnsDetail.rate = atoi(&pTrades[i].comment[3]);
							sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsDetail.bnsCode);
						}
						bnsDetail.bnsDay = pTrades[i].close_time;
						bnsDetail.value = pTrades[i].profit;
						bnsMap[pTrades[i].order] = bnsDetail;
					}
					else
					{
						if(memcmp(pTrades[i].comment,"BCI",3) == 0)
						{
							bnsIt->second.rate = atoi(&pTrades[i].comment[3]);
							sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsIt->second.bnsCode);
						}
						bnsIt->second.value += pTrades[i].profit;
						bnsIt->second.bnsDay = pTrades[i].close_time;
					}
				}
				else if(memcmp(pTrades[i].comment,"BCO",3) == 0 || memcmp(pTrades[i].comment,"Credit Out",10) == 0)
				{
					GetIntParam(pTrades[i].comment,"-",&bciNo);
					bnsIt = bnsMap.find(bciNo);
					if(bnsIt == bnsMap.end())
					{
						bnsDetail.value = pTrades[i].profit;
						bnsMap[bciNo] = bnsDetail;
					}
					else
					{
						bnsIt->second.value += pTrades[i].profit;
					}
				}
				else if(memcmp(pTrades[i].comment,"BCX",3) == 0)
				{
					GetIntParam(pTrades[i].comment,"#",&bciNo);
					bnsIt = bnsMap.find(bciNo);
					if(bnsIt == bnsMap.end())
					{
						bnsDetail.value = pTrades[i].profit;
						bnsMap[bciNo] = bnsDetail;
					}
					else
					{
						bnsIt->second.value += pTrades[i].profit;
					}
				}
			}
			if(pTrades != NULL) m_manager->MemFree(pTrades);

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
					
					if( wdCount < 2 && (bnsIt->second.bnsCode[0] > wdType || bnsIt->second.bnsCode[0] == 0) ) continue;//优先扣除等级为A的活动和没有等级的活动,然后扣除等级为B的活动,最后扣除老赠金

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

					userTotal = 1;
					pUsers = NULL;
					pUsers = m_manager->UserRecordsRequest(&user.login,&userTotal);
					if(pUsers != NULL)
					{
						memcpy(&usrinf,&pUsers[0],sizeof(UserRecord));
						m_manager->MemFree(pUsers);
					}

					if(usrinf.credit < 0.0000001)
					{
						credit = 0.0;
						break;
					}

					if((creditOut+usrinf.credit) < -0.0000001)
					{
						creditOut = (-1.0)*usrinf.credit;
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
						
					if( (m_manager->TradeTransaction(&info)) == RET_OK )
					{
						Out(CmdOK,ipStr.c_str(),"'web': BZB credit out %.2lf for '%d' - '%s'",creditOut,user.login,creditComm);
					}
					else
					{
						Out(CmdOK,ipStr.c_str(),"'web': failed to BZB credit out %.2lf for '%d' - '%s'",creditOut,user.login,creditComm);
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
				Out(CmdOK,ipStr.c_str(),"'web': BZB enter bzb map");

				sprintf(creditComm,"BZB@%s#%d-%d",bzbIt->first.c_str(),balSn,dpTicket);

				memset(&info,0,sizeof(TradeTransInfo));
				info.type = TT_BR_BALANCE;
				info.cmd = OP_BALANCE;
				info.orderby = user.login;
				info.price = -bzbIt->second;
				strcpy(info.comment,creditComm);

				if( (m_manager->TradeTransaction(&info)) == RET_OK )
				{
					Out(CmdOK,ipStr.c_str(),"'web': BZB balance %.2lf for '%d' - '%s'",-bzbIt->second,user.login,creditComm);
				}
				else
				{
					Out(CmdOK,ipStr.c_str(),"'web': failed to BZB balance %.2lf for '%d' - '%s'",-bzbIt->second,user.login,creditComm);
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

			if( (m_manager->TradeTransaction(&info)) != RET_OK )
			{
				output = "ERROR\r\nTradeTransaction deposit failed\r\nend";
				return 0;
			}

			if(force == TRUE)
			{
				Out(CmdOK,ipStr.c_str(),"'web': changed balance force %.2lf for '%d' - '%s'",deposit,user.login,temp);
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': changed balance %.2lf for '%d' - '%s'",deposit,user.login,temp);
			}

			if(deposit<-0.0000001)
			{
				creditWay = 1;//默认扣除赠金
				GetIntParam(buffer,"CREDITWAY=",&creditWay);

				if(bReject == true)
				{
					if(bAllReject == true)
					{
						lTime = m_manager->ServerTime();
						time_t fromTime = 1433088000;//2015-06-01 00:00:00
						TradeRecord * pTrades = NULL;
						int total = 0;
						double cxlCredit = 0.0;
						pTrades = m_manager->TradesUserHistory(user.login,fromTime,lTime,&total);
						for(int i = 0; i < total; i++)
						{
							if(usrinf.credit < 0.0000001) break;

							if(pTrades[i].cmd != OP_CREDIT) continue;
							if(memcmp(pTrades[i].comment,"BCI",3) != 0) continue;
							if(pTrades[i].profit < 0.0000001) continue;

							int creditSn = 0;
							GetIntParam(pTrades[i].comment,"#",&creditSn);
							int creditBalTick = 0;
							GetIntParam(pTrades[i].comment,"-",&creditBalTick);
							if(creditSn == cxlBalSn || creditBalTick == cxlBalTick)//自动bci和手动bci
							{
								if( (usrinf.credit - pTrades[i].profit) > -0.0000001 )
								{
									cxlCredit = -pTrades[i].profit;
								}
								else
								{
									cxlCredit = -usrinf.credit;
								}
								BNSDetail bnsDetail = {0};
								bnsDetail.rate = atoi(&pTrades[i].comment[3]);
								bnsDetail.bnsDay = pTrades[i].close_time;
								sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsDetail.bnsCode);

								sprintf(creditComm,"BCO%d@%s#%d-%d",bnsDetail.rate,bnsDetail.bnsCode,balSn,pTrades[i].order);
								info.type = TT_BR_BALANCE;
								info.cmd = OP_CREDIT;
								info.orderby = user.login;
								info.price = cxlCredit;
								strcpy(info.comment,creditComm);
								info.expiration = bnsDetail.bnsDay;
								if((m_manager->TradeTransaction(&info)) == RET_OK)
								{
									Out(CmdOK,ipStr.c_str(),"'web': BCO %.2lf for '%d' - '%s'",cxlCredit,user.login,creditComm);
								}
								else
								{
									Out(CmdOK,ipStr.c_str(),"'web': failed to BCO %.2lf for '%d' - '%s'",cxlCredit,user.login,creditComm);
								}

								break;
							}
						}
						if(pTrades != NULL) m_manager->MemFree(pTrades);
					}
				}
				else if(creditWay != 0)
				{
					map<int,BNSDetail> bnsMap;
					map<int,BNSDetail>::iterator bnsIt;
					lTime = m_manager->ServerTime();
					time_t fromTime = 1433088000;//2015-06-01 00:00:00
					TradeRecord * pTrades = NULL;
					int total = 0;
					pTrades = m_manager->TradesUserHistory(user.login,fromTime,lTime,&total);
					for(int i = 0; i < total; i++)
					{
						if(pTrades[i].cmd != OP_CREDIT) continue;

						int bciNo = 0;
						BNSDetail bnsDetail = {0};
						string comment = pTrades[i].comment;
						if(memcmp(pTrades[i].comment,"BCI",3) == 0)
						{
							bnsIt = bnsMap.find(pTrades[i].order);
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrades[i].profit;
								bnsDetail.rate = atoi(&pTrades[i].comment[3]);
								bnsDetail.bnsDay = pTrades[i].close_time;
								sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsDetail.bnsCode);
								bnsMap[pTrades[i].order] = bnsDetail;
							}
							else
							{
								bnsIt->second.rate = atoi(&pTrades[i].comment[3]);
								bnsIt->second.value += pTrades[i].profit;
								bnsIt->second.bnsDay = pTrades[i].close_time;
								sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsIt->second.bnsCode);
							}
						}
						else if(memcmp(pTrades[i].comment,"BCO",3) == 0)
						{
							GetIntParam(pTrades[i].comment,"-",&bciNo);
							bnsIt = bnsMap.find(bciNo);
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrades[i].profit;
								bnsMap[bciNo] = bnsDetail;
							}
							else
							{
								bnsIt->second.value += pTrades[i].profit;
							}
						}
						else if(memcmp(pTrades[i].comment,"BCX",3) == 0)
						{
							GetIntParam(pTrades[i].comment,"#",&bciNo);
							bnsIt = bnsMap.find(bciNo);
							if(bnsIt == bnsMap.end())
							{
								bnsDetail.value = pTrades[i].profit;
								bnsMap[bciNo] = bnsDetail;
							}
							else
							{
								bnsIt->second.value += pTrades[i].profit;
							}
						}
					}
					if(pTrades != NULL) m_manager->MemFree(pTrades);

					credit = deposit;
					char  wdType;
					for(int wdCount = 0; wdCount < 2; wdCount++)
					{
						if(wdCount == 0) wdType = 'A';
						if(wdCount == 1) wdType = 'B';

						for(bnsIt = bnsMap.begin(); bnsIt != bnsMap.end(); bnsIt++)
						{
							if( (credit + 0.01) > 0.0000001 ) break;

							if(bnsIt->second.rate == 0) continue; //不扣除比例为0的赠金
							if( (bnsIt->second.value - 0.01) < -0.0000001 ) continue;
							if( bnsIt->second.bnsCode[0] > wdType ) continue;//优先扣除等级为A的活动和没有等级的活动,然后扣除等级为B的活动

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

							userTotal = 1;
							pUsers = NULL;
							pUsers = m_manager->UserRecordsRequest(&user.login,&userTotal);
							if(pUsers != NULL)
							{
								memcpy(&usrinf,&pUsers[0],sizeof(UserRecord));
								m_manager->MemFree(pUsers);
							}
							if(usrinf.credit < 0.0000001)
							{
								credit = 0.0;
								break;
							}

							if((creditOut+usrinf.credit) < -0.0000001)
							{
								creditOut = (-1.0)*usrinf.credit;
								credit = 0.0;
							}

							bnsIt->second.value += creditOut;
							info.type = TT_BR_BALANCE;
							info.cmd = OP_CREDIT;
							info.orderby = user.login;
							info.price = creditOut;
							strcpy(info.comment,creditComm);
							info.expiration = bnsIt->second.bnsDay;
							if((m_manager->TradeTransaction(&info)) == RET_OK)
							{
								Out(CmdOK,ipStr.c_str(),"'web': BCO %.2lf for '%d' - '%s'",creditOut,user.login,creditComm);
								GetCreditNO(creditNo,user.login,creditComm);
								sprintf(creditDetail,"%d,%.2lf,%d,BCI%d@%s#,",creditNo,creditOut,bnsIt->second.bnsDay,bnsIt->second.rate,bnsIt->second.bnsCode);
								strCreditDetail = strCreditDetail + creditDetail;
							}
							else
							{
								Out(CmdOK,ipStr.c_str(),"'web': failed to BCO %.2lf for '%d' - '%s'",creditOut,user.login,creditComm);
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
				creditWay = 0;//默认不给赠金
				GetIntParam(buffer,"CREDITWAY=",&creditWay);
				if(bReject == true)
				{
					if(bAllReject == true)
					{
						lTime = m_manager->ServerTime();
						time_t fromTime = 1433088000;//2015-06-01 00:00:00
						TradeRecord * pTrades = NULL;
						int total = 0;
						double cxlCredit = 0.0;
						pTrades = m_manager->TradesUserHistory(user.login,fromTime,lTime,&total);
						for(int i = 0; i < total; i++)
						{
							if(pTrades[i].cmd != OP_CREDIT) continue;
							if(memcmp(pTrades[i].comment,"BCO",3) != 0) continue;
							if(pTrades[i].profit > -0.0000001) continue;

							int creditSn = 0;
							GetIntParam(pTrades[i].comment,"#",&creditSn);
							if(creditSn == cxlBalSn)
							{
								cxlCredit = -pTrades[i].profit;
								BNSDetail bnsDetail = {0};
								bnsDetail.rate = atoi(&pTrades[i].comment[3]);
								sscanf(pTrades[i].comment,"%*[^@]@%[^#]",bnsDetail.bnsCode);

								int srcCreditInNo = 0;
								GetIntParam(pTrades[i].comment,"-",&srcCreditInNo);
								TradeRecord cxlRecord = {0};
								GetCXRecord(cxlRecord,user.login,srcCreditInNo);
								if(cxlRecord.close_time != 0)
								{
									bnsDetail.bnsDay = cxlRecord.close_time;
								}
								else
								{
									bnsDetail.bnsDay = pTrades[i].close_time;
								}

								sprintf(creditComm,"BCI%d@%s#%d-%d",bnsDetail.rate,bnsDetail.bnsCode,balSn,pTrades[i].order);
								info.type = TT_BR_BALANCE;
								info.cmd = OP_CREDIT;
								info.orderby = user.login;
								info.price = cxlCredit;
								strcpy(info.comment,creditComm);
								info.expiration = bnsDetail.bnsDay;
								if((m_manager->TradeTransaction(&info)) == RET_OK)
								{
									Out(CmdOK,ipStr.c_str(),"'web': BCI %.2lf for '%d' - '%s'",creditNo,cxlCredit,user.login,creditComm);
								}
								else
								{
									Out(CmdOK,ipStr.c_str(),"'web': failed to BCI %.2lf for '%d' - '%s'",cxlCredit,user.login,creditComm);
								}
							}
						}
						if(pTrades != NULL) m_manager->MemFree(pTrades);
					}
				}
				else if(creditWay == 1)//bonus code
				{
					if(GetStrParam(buffer,"BNSCODE=",      bnsCode,             sizeof(bnsCode)-1)==FALSE)
					{
						Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,deposit);
						output = "OK\r\nmiss BNSCODE\r\nend";
						return 0;
					}

					GetIntParam(buffer,"BNSDAY=",&bnsDay);
					if(bnsDay<=0)
					{
						Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,deposit);
						output = "OK\r\ninvalid BNSDAY\r\nend";
						return 0;
					}
					GetIntParam(buffer,"BNSRATE=",&bnsRate);
					if(bnsRate<=0)
					{
						Out(CmdOK,ipStr.c_str(),"'web': failed to BCI for '%d#%.2lf'",user.login,deposit);
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
					if((m_manager->TradeTransaction(&info)) == RET_OK)
					{
						Out(CmdOK,ipStr.c_str(),"'web': BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
					}
					else
					{
						Out(CmdOK,ipStr.c_str(),"'web': failed to BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
					}
				}
				else if(creditWay == 2)//credit details
				{
					if(GetStrParam(buffer,"CREDITDETAIL=",      creditDetail,             sizeof(creditDetail)-1)==FALSE)
					{
						Out(CmdOK,ipStr.c_str(),"'web': no CREDITDETAIL to BCI for '%d#%.2lf'",user.login,deposit);
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
							  if((m_manager->TradeTransaction(&info)) == RET_OK)
							  {
								 Out(CmdOK,ipStr.c_str(),"'web': BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
							  }
							  else
							  {
								 Out(CmdOK,ipStr.c_str(),"'web': failed to BCI %.2lf for '%d' - '%s'",credit,user.login,creditComm);
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

		gcInfo.command = GROUP_DELETE;
		gcInfo.len = 1;

		if(m_manager->UsersGroupOp(&gcInfo,&user.login) != RET_OK)
		{
			Out(CmdOK,ipStr.c_str(),"'web': account '%d' deleted",user.login);
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

		//if(investor!=0)         investor =TRUE;				//xbc 改为  0.主密码，1.投资者密码，2.同时改
		if(drop_key!=0)			drop_key =TRUE;
	
		if(CheckPassword(user.password)==FALSE)
		{
			output = "ERROR\r\ninvalid PASSWORD\r\nend";
			return 0;
		}

		//如果INVESTOR设为2，那么同时改
		if(investor == 2)
		{
			int bUser = m_manager->UserPasswordSet(user.login,user.password,FALSE,drop_key);
			int bInves = m_manager->UserPasswordSet(user.login,user.password,TRUE,drop_key);
			if(bUser == RET_OK && bInves == RET_OK)
			{
				Out(CmdOK,ipStr.c_str(),"'web': set investor and master password for '%d' [successful]",user.login);
				output = "OK\r\nchange both password successful\r\nend";
				return 0;
			}
			else
			{
				if(bUser == RET_OK)
				{
					Out(CmdOK,ipStr.c_str(),"'web': set master password for '%d' [successful]",user.login);
					output = "ERROR\r\nchange master password successful but investor failed\r\nend";
					return 0;
				}
				else if(bInves == RET_OK)
				{
					Out(CmdOK,ipStr.c_str(),"'web': set investor password for '%d' [successful]",user.login);
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
		else
		{
			if(investor!=0) investor =TRUE;
			if(m_manager->UserPasswordSet(user.login,user.password,investor,drop_key) == RET_OK)
			{
				if(investor == TRUE)
					Out(CmdOK,ipStr.c_str(),"'web': set investor password for '%d' [successful]",user.login);
				else
					Out(CmdOK,ipStr.c_str(),"'web': set master password for '%d' [successful]",user.login);

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

		if(m_manager->UserPasswordCheck(user.login,user.password) == RET_OK)
		{
			Out(CmdOK,ipStr.c_str(),"'web': checking password of '%d' [successful]",user.login);
			output = "OK\r\npassword is right\r\nend";
			return 0;
		}
		else
		{
			Out(CmdOK,ipStr.c_str(),"'web': checking password of '%d' [failed]",user.login);
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

		userTotal = 1;
		pUsers = NULL;
		pUsers = m_manager->UserRecordsRequest(&user.login,&userTotal);
		if(pUsers != NULL)
		{
			memcpy(&usrinf,&pUsers[0],sizeof(UserRecord));
			m_manager->MemFree(pUsers);
		}
		else
		{
			output = "ERROR\r\ncredit failed\r\nend";
			return 0;
		}

		if( credit < -0.0000001 && (usrinf.credit+credit) < -0.0000001 )
		{
			output = "ERROR\r\ncredit failed\r\nend";
			return  0;
		}
		//start wangxp 2025-3-21
		if( credit < -0.00001)
		{
			MarginLevel marginLevel = {0};
			//if(m_pumping->MarginLevelGet(user.login,user.group,&marginLevel) == RET_OK)
			if(m_manager->MarginLevelRequest(user.login,&marginLevel) == RET_OK)
			{
				Out(CmdOK,ipStr.c_str(),"'MarginLevelGet': login %d group: %s",user.login,user.group);
				margin = marginLevel.margin;
				freemargin = marginLevel.margin_free;
				equity = marginLevel.equity;
				if(force == FALSE)
				{
					float tmp = freemargin+credit;
					Out(CmdOK,ipStr.c_str(),"'Available Free Margin':%f freemargin %f credit:%f",tmp,freemargin,credit);
					if( tmp<-0.0000001 )
					{
						output = "ERROR\r\nfreemargin credit failed\r\nend";
						return 0;
					}
				}
				else
				{
					/*float tmp = equity+deposit-usrinf.credit;
					if( tmp<-0.0000001 )
						return _snprintf(buffer,size-1,"ERROR\r\ndeposit failed\r\nend\r\n");*/
				}
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': receive command '%s'",outStr.c_str());
				output = "ERROR\r\nMarginLevelGet credit failed\r\nend";
				return 0;
			}
		}
		//   end wangxp 2025-3-21

		GetIntParam(buffer,"BNSDAY=",&bnsDay);
		if(bnsDay<=0)
		{
			lTime = m_manager->ServerTime()+180*24*3600;//默认到期日是180天后

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
        if((m_manager->TradeTransaction(&info)) == RET_OK)
		{
			Out(CmdOK,ipStr.c_str(),"'web': changed credit %.2lf for '%d' - '%s'",credit,user.login,temp);
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

		userTotal = 1;
		pUsers = NULL;
		pUsers = m_manager->UserRecordsRequest(&user.login,&userTotal);
		if(pUsers != NULL)
		{
			memcpy(&user,&pUsers[0],sizeof(UserRecord));
			m_manager->MemFree(pUsers);
		}
		else
		{
			output = "ERROR\r\ninvalid LOGIN\r\nend";
			return 0;
		}

		GetStrParam(buffer,"GROUP=",group_name,sizeof(group_name)-1);
	
		if(m_manager->GroupRecordGet(group_name,&group)==RET_OK)
		{
			COPY_STR(user.group,group_name);
		}

		GetIntParam(buffer,"ENABLE=",&user.enable);
		GetIntParam(buffer,"CHANGE_PASSWORD=",&user.enable_change_password);
		GetIntParam(buffer,"COLOR=",(int*)&user.user_color);
		
		GetStrParam(buffer,"NAME=",          user.name,             sizeof(user.name)-1);
		GetStrParam(buffer,"EMAIL=",         user.email,            sizeof(user.email)-1);
#ifndef VEXNB
		GetStrParam(buffer,"COUNTRY=",       user.country,          sizeof(user.country)-1);
#endif
		GetStrParam(buffer,"STATE=",         user.state,            sizeof(user.state)-1);
		GetStrParam(buffer,"CITY=",          user.city,             sizeof(user.city)-1);
		GetStrParam(buffer,"ADDRESS=",       user.address,          sizeof(user.address)-1);
		GetStrParam(buffer,"COMMENT=",       user.comment,          sizeof(user.comment)-1);
		GetStrParam(buffer,"PHONE=",         user.phone,            sizeof(user.phone)-1);
		GetStrParam(buffer,"PHONE_PASSWORD=",user.password_phone,   sizeof(user.password_phone)-1);
		GetStrParam(buffer,"STATUS=",        user.status,           sizeof(user.status)-1);
		GetStrParam(buffer,"ZIPCODE=",       user.zipcode,          sizeof(user.zipcode)-1);
		GetStrParam(buffer,"ID=",            user.id,               sizeof(user.id)-1);
		GetIntParam(buffer,"LEVERAGE=",     &user.leverage);
		GetIntParam(buffer,"AGENT=",        &user.agent_account);
		GetIntParam(buffer,"SEND_REPORTS=", &user.send_reports);
		GetIntParam(buffer,"READONLY=",		&user.enable_read_only);
		GetFltParam(buffer,"INTEREST_RATE=",			&user.interestrate);
		
		if(GetIntParam(buffer,"LIMIT=",         &eqLimit)==TRUE)
		{
			memcpy(user.api_data,&eqLimit,sizeof(eqLimit));
		}
	
		//---- checks
		if(user.send_reports!=0)         user.send_reports =TRUE;
		if(user.enable!=0)				 user.enable =TRUE;
		if(user.enable_change_password!=0)         user.enable_change_password =TRUE;
		if(user.enable_read_only!=0)     user.enable_read_only =TRUE;

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

		if(user.interestrate<-0.0000001 || user.interestrate>(100+0.0000001))
		{
			output = "ERROR\r\ninvalid INTEREST_RATE\r\nend";
			return 0;
		}

		if(m_manager->UserRecordUpdate(&user) == RET_OK)
		{
			Out(CmdOK,ipStr.c_str(),"'web': update information of '%d' [successful]",user.login);
			output = "OK\r\nuser update successful\r\nend";
			return 0;
		}
		else
		{
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

		userTotal = 1;
		pUsers = NULL;
		pUsers = m_manager->UserRecordsRequest(&user.login,&userTotal);
		if(pUsers != NULL)
		{
			memcpy(&usrinf,&pUsers[0],sizeof(UserRecord));
			m_manager->MemFree(pUsers);
		
			if(usrinf.enable == TRUE)
			{
				Out(CmdOK,ipStr.c_str(),"'web': check state of '%d' [enable]",user.login);
				output = "OK\r\nuser check successful\r\nend";
				return 0;
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': check state of '%d' [disable]",user.login);
				output = "Disable\r\nuser check successful\r\nend";
				return 0;
			}
		}
		else
		{
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

		if( (m_pumping->UserRecordGet(user.login,&user)) != RET_OK )
		{
			output = "ERROR\r\nfreemargin check failed\r\nend";
			return  0;
		}

		MarginLevel marginLevel = {0};
		if(m_pumping->MarginLevelGet(user.login,user.group,&marginLevel) == RET_OK)
		{
			margin = marginLevel.margin;
			freemargin = marginLevel.margin_free;
			equity = marginLevel.equity;
			if((freemargin-withdrawal-usrinf.credit)>-0.0000001)
			{
				Out(CmdOK,ipStr.c_str(),"'web': check free margin of '%d' [enable]",user.login);
				output = "OK\r\nfreemargin check successful\r\nend";
				return 0;
			}
			else
			{
				Out(CmdOK,ipStr.c_str(),"'web': check free margin of '%d' [disable]",user.login);
				output = "Disable\r\nfreemargin check successful\r\nend";
				return 0;
			}
		}
		else
		{
			output = "ERROR\r\nfreemargin check failed\r\nend";
			return 0;
		}
		break;
  }

   return 0;
}