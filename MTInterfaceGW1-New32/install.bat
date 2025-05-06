sc create MT4InterfaceGW binpath= "%~dp0MTInterfaceGW.exe MT4InterfaceGW" start= auto
sc failure MT4InterfaceGW reset= 30 actions= restart/5000
sc start MT4InterfaceGW