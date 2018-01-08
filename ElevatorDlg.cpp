#include "stdafx.h"
#include "ElevatorDlg.h"
#include "Format.h"

static const int _buttons_id = 1000;
static const int _calls_id = 2000;
static const int _min_floor = 5;
static const int _floors = 20;
static const int _level_step = 100; //mm

static const UINT_PTR _levels_timer = 1;
enum TMessageIDs: UINT
{
	msg_elevator_event = WM_USER + 1, //lParam = CElevator::TEvent
};

CElevatorDlg::CElevatorDlg()
{
}

CElevatorDlg::~CElevatorDlg()
{
}

void CElevatorDlg::DoModal(HINSTANCE hInstance)
{
	DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_ELEVATOR), nullptr, DlgProc, LPARAM(this));
}


CElevatorDlg *GetDialog(HWND hWnd)
{
	return reinterpret_cast<CElevatorDlg *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
}

INT_PTR CElevatorDlg::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	try
	{
		if (message == WM_INITDIALOG)
		{
			if (!lParam)
				return FALSE;

			auto &dlg = *reinterpret_cast<CElevatorDlg *>(lParam);
			dlg.m_hWnd = hDlg;
			::SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
			dlg.OnInitDialog();

			return TRUE;
		}

		auto *pDlg = GetDialog(hDlg);
		if (!pDlg)
			return FALSE;

		auto &dlg = *pDlg;
		switch (message)
		{
		case WM_TIMER:
			if (wParam == _levels_timer)
				dlg.UpdateLevels();
			break;
		case msg_elevator_event:
			dlg.UpdateControls(CElevator::TEvent(lParam));
			break;
		case WM_COMMAND:
			return dlg.OnCommand(wParam, lParam);
		case WM_DESTROY:
			dlg.m_spElevator.reset();
			break;
		default:
			return FALSE;
		}
	}
	catch (...)
	{
	}
	return TRUE;
}

template<bool Cabin> 
void CElevatorDlg::PressElevatorButton(int ctrl_id, size_t floor)
{
	if (!m_spElevator)
		return;

	const auto check = ::SendDlgItemMessage(*this, ctrl_id, BM_GETCHECK, 0, 0);
	if (check != BST_CHECKED)
		return;

	//auto msg = TS::FormatStr("Button pressed", floor).str();
	//::MessageBoxA(m_hWnd, msg.c_str(), Inner?"Cabin": "Call", MB_OK);
	if constexpr (Cabin)
		m_spElevator->PressButton(floor);
	else
		m_spElevator->CallLift(floor);

	UpdateControls();
}

bool CElevatorDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	const auto ctrl_id = LOWORD(wParam);

	[[maybe_unused]] const auto notify_code = HIWORD(wParam);
	[[maybe_unused]] auto hCtrl = reinterpret_cast<HWND>(lParam);

	switch (ctrl_id)
	{
	case IDCANCEL:
		EndDialog(m_hWnd, ctrl_id);
		::SetWindowLongPtr(m_hWnd, 0, 0);
		m_hWnd = nullptr;
		break;
	case IDC_CLOSE_DOORS:
		if (m_spElevator)
			m_spElevator->ForceCloseDoors();
		break;
	case IDC_OPEN_DOORS:
		if (m_spElevator)
			m_spElevator->ForceOpenDoors();
		break;
	case IDC_RESET_PARAMS:
		ResetElevator(false);
		break;

	default:
		if (ctrl_id >= _buttons_id && ctrl_id <= (_buttons_id + _floors))
			PressElevatorButton<true>(ctrl_id, ctrl_id - _buttons_id);
		else if (ctrl_id >= _calls_id && ctrl_id <= (_calls_id + _floors))
			PressElevatorButton<false>(ctrl_id, ctrl_id - _calls_id);
		else 
			return false;
	}

	return true;
}

void CElevatorDlg::OnInitDialog()
{
	for (size_t i = _min_floor; i <= _floors; ++i)
		::SendDlgItemMessage(*this, IDC_FLOORS, CB_ADDSTRING, 0, LPARAM(std::to_wstring(i).c_str()));


	//CElevatorParams params;
	//::SendDlgItemMessage(*this, IDC_FLOORS, CB_SETCURSEL, params.m_floors - _min_floor, 0);

	//::SetDlgItemText(*this, IDC_ELEVATOR_SPEED, std::to_wstring(params.m_speed).c_str());
	//::SetDlgItemText(*this, IDC_FLOOR_HEIGHT, std::to_wstring(params.m_floor_height).c_str());
	//::SetDlgItemText(*this, IDC_CLOSE_TIME, std::to_wstring(params.m_close_tm.count() / 1000.0).c_str());

	ResetElevator(true);
}


inline
void ResetButton(HWND hDlg, int id, bool enable)
{
	auto hCtrl = ::GetDlgItem(hDlg, id);
	::EnableWindow(hCtrl, enable);
	::SendMessage(hCtrl, BM_SETCHECK, BST_UNCHECKED, 0);
}

inline 
double GetDlgItemDouble(HWND hDlg, int id)
{
	static const size_t _sz = 127;
	wchar_t buf[_sz + 1];
	const auto n = ::GetDlgItemText(hDlg, id, buf, _sz);
	return n? ::_wtof(buf): 0;
}

void CElevatorDlg::ResetElevator(bool init)
{
	CElevatorParams params;
	if (!init)
	{
		params.m_floors = ::SendDlgItemMessage(*this, IDC_FLOORS, CB_GETCURSEL, 0, 0) + _min_floor;
		if (params.m_floors > _floors)
		{
			ShowMessage(MB_ICONERROR | MB_OK, "Error", "Invalid floors count", params.m_floors);
			return;
		}

		params.m_speed = GetDlgItemDouble(*this, IDC_ELEVATOR_SPEED);
		if (params.m_speed < 0)
		{
			ShowMessage(MB_ICONERROR | MB_OK, "Error", "Invalid elevator speed", params.m_speed);
			return;
		}

		params.m_floor_height = GetDlgItemDouble(*this, IDC_FLOOR_HEIGHT);
		if (params.m_floor_height <= 0)
		{
			ShowMessage(MB_ICONERROR | MB_OK, "Error", "Invalid floor height", params.m_floor_height);
			return;
		}

		params.m_close_tm = std::chrono::milliseconds(int(1000 * GetDlgItemDouble(*this, IDC_CLOSE_TIME)));
	}

	m_spElevator = std::make_unique<CElevator>(params, [this](CElevator::TEvent ev, CElevator &)
	{		
		::PostMessage(*this, msg_elevator_event, 0, LPARAM(ev));
	});

	for (size_t i = 0; i < _floors; ++i)
	{
		const bool enable = i < params.m_floors;
		ResetButton(*this, _buttons_id + i, enable);
		ResetButton(*this, _calls_id + i, enable);
	}
	
	const int h = int(params.m_floor_height * 1000);
	::SendDlgItemMessage(*this, IDC_LEVEL, TBM_SETTICFREQ, h, 0);

	::SendDlgItemMessage(*this, IDC_LEVEL, TBM_SETRANGEMIN, FALSE, 0);

	m_height = int(params.m_floors - 1) * h;
	::SendDlgItemMessage(*this, IDC_LEVEL, TBM_SETRANGEMAX, FALSE, m_height);
	::SendDlgItemMessage(*this, IDC_LEVEL, TBM_SETPOS, TRUE, m_height);

	::SendDlgItemMessage(*this, IDC_DOORS, PBM_SETRANGE32, 0, params.m_doors_width);
	
	::SendDlgItemMessage(*this, IDC_FLOORS, CB_SETCURSEL, params.m_floors - _min_floor, 0);

	::SetDlgItemText(*this, IDC_ELEVATOR_SPEED, std::to_wstring(params.m_speed).c_str());
	::SetDlgItemText(*this, IDC_FLOOR_HEIGHT, std::to_wstring(params.m_floor_height).c_str());
	::SetDlgItemText(*this, IDC_CLOSE_TIME, std::to_wstring(params.m_close_tm.count() / 1000.0).c_str());


	UpdateControls();
	SetTimer(*this, _levels_timer, 100, 0);
}

void CElevatorDlg::UpdateLevels()
{
	if (!m_spElevator)
		return;

	auto &lift = *m_spElevator;
	auto &motor = lift.GetMotor();
	auto &doors = lift.GetDoorsMotor();

	::SendDlgItemMessage(*this, IDC_LEVEL, TBM_SETPOS, TRUE, m_height - motor.GetLevel());
	::SendDlgItemMessage(*this, IDC_DOORS, PBM_SETPOS, doors.GetLevel(), 0);
}

void CElevatorDlg::UpdateControls(CElevator::TEvent ev)
{
	if (!m_spElevator)
		return;

	auto &lift = *m_spElevator;
	auto &params = lift.GetParams();

	const auto dir = lift.GetDirection();
	const auto floor = lift.GetFloor();
	{
		std::wstring s = std::to_wstring(floor + 1); 

		if (dir != dir_stop)
			s += dir < 0? wchar_t(0x2193): wchar_t(0x2191);

		::SetDlgItemText(*this, IDC_FLOOR, s.c_str());
	}

	//::SetDlgItemInt(*this, IDC_FLOOR, floor + 1, FALSE);

	auto mask = 0x01;
		
	const auto buttons = lift.GetButtons();
	const auto calls = lift.GetCalls();

	for (size_t i = 0; i < params.m_floors; ++i, mask <<= 1)
	{
		::SendDlgItemMessage(*this, _buttons_id + i, BM_SETCHECK, buttons & mask? BST_CHECKED: BST_UNCHECKED, 0);		
		::SendDlgItemMessage(*this, _calls_id + i, BM_SETCHECK, calls & mask? BST_CHECKED: BST_UNCHECKED, 0);		
	}
	
	//auto &motor = lift.GetMotor();
	const auto doors = lift.GetDoors();
	::SetDlgItemText(*this, IDC_DOORS_STATUS, doors.second? L"Close": L"Open");


	if (ev != CElevator::TEvent(-1))
	{
		std::string s = 
			CElevator::GetEventName(ev);
			//TS::FormatStr(ev).str();
		::SetDlgItemTextA(*this, IDC_ELEVATOR_STATUS, s.c_str());
	}
}