#include "stdafx.h"
#include "Elevator.h"
#include "ElevatorDlg.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE , _In_ LPWSTR , _In_ int )
{
	CElevatorDlg dlg;
	dlg.DoModal(hInstance);
    return 0;
}
