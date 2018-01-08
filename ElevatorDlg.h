#pragma once
#include "resource.h"
#include "Elevator.h"

class CElevatorDlg
{
public:
	CElevatorDlg();
	~CElevatorDlg();
	
	void DoModal(HINSTANCE hInstance);

	operator HWND()
	{
		return m_hWnd;
	}
protected:
	void OnInitDialog();
	bool OnCommand(WPARAM wParam, LPARAM lParam);
	template<bool Cabin> void PressElevatorButton(int ctrl_id, size_t floor);
	
	template <typename... TT>
	void ShowMessage(UINT Type, const char *Caption, TT&&... args)
	{
		auto s = TS::FormatStr(std::forward<TT>(args)...).str();
		::MessageBoxA(*this, s.c_str(), Caption, Type);
	}

	void ResetElevator(bool init = false);
	void UpdateLevels();
	void UpdateControls(CElevator::TEvent ev = CElevator::TEvent(-1));

	static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

	HWND m_hWnd = nullptr;

	int m_height = 0;
	std::unique_ptr<CElevator> m_spElevator;
};

