// InterfaceGW.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "InterfaceGW.h"
#include "MessageCenter.h"
#include "MT4Manager.h"
#include "MT5Manager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CWinApp theApp;
MessageCenter mCenter;
MT4Manager g_mt4manager;
MT5Manager g_mt5manager;
ManagerIF * g_pManagerIF = NULL;
CString					g_currentPath=".";
SERVICE_STATUS          ServiceStatus; 
SERVICE_STATUS_HANDLE   hStatus;
CString                 g_svrName;
int                     g_version;

void Log(CString & record)
{
	ofstream oFile;
	CString fileName = g_currentPath + "\\MT4InterfaceGW.log";
	oFile.open(fileName,ios_base::app);

	if(oFile)
	{
		oFile<<CTime::GetCurrentTime().Format("%Y-%m-%d %H:%M:%S")<<" "<<record<<endl;
		oFile.flush();
		oFile.close();
	}
}

void CALLBACK TimerPing( 
    HWND hwnd,       
    UINT message,     
    UINT idTimer,     
    DWORD dwTime)   
{
	AfxBeginThread(MessageCenter::PingHandler,NULL);
}


// Control Handler
void ControlHandler(DWORD request) 
{ 
   switch(request) 
   { 
      case SERVICE_CONTROL_STOP: 
         ServiceStatus.dwWin32ExitCode = 0; 
         ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
         break; 
 
      case SERVICE_CONTROL_SHUTDOWN: 

         ServiceStatus.dwWin32ExitCode = 0; 
         ServiceStatus.dwCurrentState = SERVICE_STOPPED;
         break; 
        
      default:
         break;
    } 
 
    // Report current status
    SetServiceStatus (hStatus, &ServiceStatus);
 
    return; 
}

bool InitService() 
{ 
	ServiceStatus.dwServiceType = 
      SERVICE_WIN32; 
	ServiceStatus.dwCurrentState = 
      SERVICE_START_PENDING; 
	ServiceStatus.dwControlsAccepted   =  
      SERVICE_ACCEPT_STOP | 
      SERVICE_ACCEPT_SHUTDOWN;
	ServiceStatus.dwWin32ExitCode = 0; 
	ServiceStatus.dwServiceSpecificExitCode = 0; 
	ServiceStatus.dwCheckPoint = 0; 
	ServiceStatus.dwWaitHint = 0;

	hStatus = RegisterServiceCtrlHandler(
	g_svrName.GetBuffer(), 
      (LPHANDLER_FUNCTION)ControlHandler); 
	if (hStatus == (SERVICE_STATUS_HANDLE)0) 
	{ 
		// Registering Control Handler failed
		return false; 
	}
	return true; 
}

void ServiceMain(int argc, char** argv) 
{ 
	if (InitService()==false) 
	{
		// Initialization failed
		ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
		ServiceStatus.dwWin32ExitCode = -1; 
		SetServiceStatus(hStatus, &ServiceStatus); 
		return; 
	} 
	// We report the running status to SCM. 
	ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
	SetServiceStatus (hStatus, &ServiceStatus);

#ifdef NDEBUG
	int nRetCode = 0;

	HKEY hKey; 
    char szPath[512]; 
    DWORD dwBufLen = sizeof(szPath)-1,dwType;
	string svrKey = "SYSTEM\\CurrentControlSet\\Services\\";
	svrKey += g_svrName.GetBuffer();
	RegOpenKeyEx( HKEY_LOCAL_MACHINE, svrKey.c_str(),  0, KEY_QUERY_VALUE, &hKey );
	RegQueryValueEx( hKey, "ImagePath", NULL, &dwType,(LPBYTE) szPath, &dwBufLen);
	RegCloseKey( hKey ); 
	CString tmp = szPath;
	g_currentPath = tmp.Left(tmp.ReverseFind('\\'));
	if(g_currentPath=="") g_currentPath = ".";

	CString fileName = g_currentPath + "\\MTInterfaceGW.ini";
	g_version = GetPrivateProfileInt("SYSTEM","VERSION",0,fileName.GetBuffer());

	if(g_version == 4)
		g_pManagerIF = &g_mt4manager;
	else
		g_pManagerIF = &g_mt5manager;

	HMODULE hModule = ::GetModuleHandle(NULL);

	if (hModule != NULL)
	{
		// initialize MFC and print and error on failure
		if (!AfxWinInit(hModule, NULL, ::GetCommandLine(), 0))
		{
			nRetCode = 1;
		}
		else
		{
			if(g_pManagerIF->Init()==false)
			{
				nRetCode = 1;
			}
			else  if(mCenter.Init() == true)
			{
				cout<<"Program startup success!"<<endl;
				SetTimer(NULL,1,30*1000,TimerPing);//30秒ping一次
				MSG   msg;   
				while(GetMessage(&msg,NULL,0,0))   
				{   
					if(msg.message==WM_TIMER)   
					{   
						DispatchMessage(&msg);   
					}   
				}
				KillTimer(NULL,1);
			}
		}
	}
	else
	{
		nRetCode = 1;
	}
#endif

}


int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	int nRetCode = 0;

#ifdef NDEBUG
	g_svrName = argv[1];
	SERVICE_TABLE_ENTRY ServiceTable[2];
	ServiceTable[0].lpServiceName = g_svrName.GetBuffer();
	ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;

	ServiceTable[1].lpServiceName = NULL;
	ServiceTable[1].lpServiceProc = NULL;
	// Start the control dispatcher thread for our service
	StartServiceCtrlDispatcher(ServiceTable);
#endif

#ifdef _DEBUG

	g_currentPath = ".";
	
	CString fileName = g_currentPath + "\\MTInterfaceGW.ini";
	g_version = GetPrivateProfileInt("SYSTEM","VERSION",0,fileName.GetBuffer());

	if(g_version == 4)
		g_pManagerIF = &g_mt4manager;
	else
		g_pManagerIF = &g_mt5manager;

	HMODULE hModule = ::GetModuleHandle(NULL);

	if (hModule != NULL)
	{
		// initialize MFC and print and error on failure
		if (!AfxWinInit(hModule, NULL, ::GetCommandLine(), 0))
		{
			nRetCode = 1;
		}
		else
		{
			if(g_pManagerIF->Init()==false)
			{
				nRetCode = 1;
			}
			else  if(mCenter.Init() == true)
			{
				cout<<"Program startup success!"<<endl;
				SetTimer(NULL,1,60*1000,TimerPing);//60秒ping一次
				MSG   msg;   
				while(GetMessage(&msg,NULL,0,0))   
				{   
					if(msg.message==WM_TIMER)   
					{   
						DispatchMessage(&msg);   
					}   
				}
				KillTimer(NULL,1);
			}
		}
	}
	else
	{
		nRetCode = 1;
	}
#endif

	return nRetCode;
}
