#pragma once
#include "SyncObjs.h"
#include "Format.h"

#include <chrono>
#include <map>
#include <optional>

using namespace std::literals;
using namespace std::placeholders;

class CMotor;
class CElevator;


#define ELEVATOR_EVENTS \
	TS_ITEM(stop) \
	TS_ITEM(wait_doors_close) \
	TS_ITEM(start) \
	TS_ITEM(floor) \
	TS_ITEM(doors_opening) \
	TS_ITEM(doors_open) \
	TS_ITEM(doors_closing) \
	TS_ITEM(doors_close) \

struct CElevatorParams
{
	size_t m_floors = 17;
	double m_floor_height = 3.0; //m

	double m_speed = 1.5; //m/s

	std::chrono::milliseconds m_close_tm = 5s;

	double m_doors_speed = 0.8; //m/s
	size_t m_doors_width = 800; //mm
};

enum TDirection : int
{
	dir_down = -1,
	dir_stop = 0,
	dir_up = 1,
};

class CMotor
{
public:
	typedef int TValue;

	CMotor(const TValue &step = 100)
	: m_step(step)
	{
	}

	~CMotor()
	{
		{
			SYS_LOCK(m_mx);
			m_stop = true;
			m_cv.notify_all();
		}

		if (m_thread.joinable())
			try {m_thread.join();} catch(...) {;}
	}

	void ResetLimits(const TValue &low, const TValue &high)
	{
		SYS_LOCK(m_mx);
		m_limits = std::minmax(low, high);
	}

	template <typename TFunc, typename... TT>
	void RegisterSensor(int val, TFunc &&func, TT&&... args)
	{
		std::function<void(CMotor &, int, int)> fn = std::bind(std::forward<TFunc>(func), std::forward<TT>(args)..., _1, _2, _3);
		m_sensors.emplace(val, std::move(fn));
	}

	void ClearSensors() noexcept
	{
		m_sensors.clear();
	}

	void Start(double speed) //mm/sec
	{
		SYS_LOCK(m_mx);
		if (speed == 0)
			m_dx = 0;
		else
		{
			m_dx = speed < 0? -m_step: m_step;

			typedef decltype(m_speed) T;
			m_speed = T(int(double(T::period::den) / (std::abs(speed) * T::period::num) * m_step));
			m_tm = std::chrono::steady_clock::now() + m_speed;
		}

		m_cv.notify_all();
	}

	void Stop() noexcept
	{
		SYS_LOCK(m_mx);
		m_dx = 0;
		m_cv.notify_all();
	}

	bool IsStop() const
	{
		SYS_LOCK(m_mx);
		return m_dx == 0;
	}

	auto GetLevel() const
	{
		return m_val;
	}

	auto GetDirection() const
	{
		return m_dx;
	}

protected:
	void CheckSensors(TValue prev)
	{
		if (m_val == prev)
			return;

		const auto dir = m_val < prev? -1: 1;
		auto val = m_val;
		if (dir < 0)
			prev = std::exchange(val, prev);

		for (auto it = m_sensors.lower_bound(prev), end = m_sensors.end(); it != end && it->first <= val; ++it)
			it->second(*this, it->first, dir);
	}

	bool CheckLimits(const TValue &val) const
	{
		return m_limits->first < val && val < m_limits->second;
	}

	TValue ProcessRun()
	{
		auto lock = Sys::UniqueLock(m_mx);
		bool reset = false;
		while (!m_stop && m_dx == 0)
		{
			reset = true;
			m_cv.wait(lock);
		}

		if (m_stop)
			return 0;

		if (reset)	
			m_tm = std::chrono::steady_clock::now() + m_speed;

		while (!m_stop && m_dx != 0)
		{
			if (m_limits && !CheckLimits(m_val) && !CheckLimits(m_val + m_dx))
			{
				m_dx = 0;
				break;
			}

			if (m_cv.wait_until(lock, m_tm) == std::cv_status::timeout)
			{
				m_tm += m_speed;
				return m_dx;
			}
		}

		return 0;
	}

	void ThreadProc()
	{
		auto prev = m_val;
		while (!m_stop)
		{
			const auto dx = ProcessRun();
			if (dx)
			{
				m_val += dx;
				CheckSensors(prev);
				prev = m_val;
			}
		}
	}


	const TValue m_step = 100; //mm
	std::optional<std::pair<TValue, TValue>> m_limits;

	mutable std::mutex m_mx;
	std::condition_variable m_cv;

	std::chrono::steady_clock::duration m_speed; //time per step
	volatile TValue m_dx = 0;
	TValue m_val = 0; //mm
	std::chrono::steady_clock::time_point m_tm;

	volatile bool m_stop = false;

	std::multimap<int, std::function<void(CMotor &, int, int)>> m_sensors;

	std::thread m_thread{&CMotor::ThreadProc, this};
};

class CElevator
{
public:
	typedef uintmax_t TFloorMask;

	enum TEvent : size_t
	{
#define TS_ITEM(name) ev_##name,
		ELEVATOR_EVENTS
#undef TS_ITEM
		ev_count,
	};

	static const char *GetEventName(TEvent ev)
	{
#define TS_ITEM(name) #name,
		static const char *_names[ev_count] = {ELEVATOR_EVENTS};
#undef TS_ITEM
		return ev < ev_count? _names[ev]: "???";
	}

	template <typename TFunc, typename... TT>
	CElevator(const CElevatorParams &params, TFunc &&func, TT&&... args)
	: m_params(params)
	, m_fn(std::bind(std::forward<TFunc>(func), std::forward<TT>(args)..., _1, _2))
	{
		static const size_t _max_floor = sizeof(TFloorMask) * 8;
		if (m_params.m_floors > _max_floor)
			throw std::logic_error("Invalid floors count");

		const int h = int(params.m_floor_height * 1000);

		m_motor.ResetLimits(0, int(m_params.m_floors - 1) * h);
		
		CMotor::TValue level = 0;
		for (size_t i = 0; i < m_params.m_floors; ++i)
		{
			m_motor.RegisterSensor(level, &CElevator::FloorSensor, this, i);
			level += h;
		}

		m_doors_motor.ResetLimits(0, int(m_params.m_doors_width));
		m_doors_motor.RegisterSensor(0, &CElevator::DoorsSensor<0>, this);
		m_doors_motor.RegisterSensor(int(m_params.m_doors_width), &CElevator::DoorsSensor<1>, this);

		OpenDoors(false);
	}
	
	void PressButton(size_t floor)
	{
		_PressButton(floor, m_buttons);
	}

	void CallLift(size_t floor)
	{
		_PressButton(floor, m_calls);
	}

	void ForceOpenDoors()
	{
		SYS_LOCK(m_mx);
		if (m_motor.IsStop())
			OpenDoors(true);
		else
			m_force_open = true;
	}

	void ForceCloseDoors()
	{
		SYS_LOCK(m_mx);
		if (!m_doors.second)
		{
			m_doors_timer.Cancel();
			OpenDoors(false);
		}
	}

	auto GetButtons() const
	{
		return m_buttons;
	}

	auto GetCalls() const
	{
		return m_calls;
	}

	auto GetDirection() const
	{
		return m_dir;
	}

	auto GetFloor() const
	{
		return m_floor;
	}

	auto GetDoorsOpened() const
	{
		return !m_doors.first;
	}

	const auto &GetParams() const
	{
		return m_params;
	}

	const CMotor &GetMotor() const
	{
		return m_motor;
	}

	const CMotor &GetDoorsMotor() const
	{
		return m_doors_motor;
	}

	auto GetDoors() const
	{
		return m_doors;
	}

protected:
	void _PressButton(size_t floor, TFloorMask &buttons)
	{
		SYS_LOCK(m_mx);
		buttons |= TFloorMask(1) << floor;
		if (m_dir == dir_stop && m_doors.second)
			DoorsClosed();
	}

	void RaiseEvent(TEvent ev)
	{
		if (m_fn)
			m_fn(ev, *this);
	}

	template <bool Start>
	void StartMotor()
	{
		if constexpr (!Start)
			m_motor.Stop();
		else
		{
			if (m_dir == dir_stop)
				return;

			RaiseEvent(ev_start);
			m_motor.Start(m_params.m_speed * (1000.0 * int(m_dir)));
		}
	}

	void OpenDoors(bool Open)
	{
		RaiseEvent(Open? ev_doors_opening: ev_doors_closing);
		m_motor.Stop();
		m_doors_motor.Start(m_params.m_doors_speed * (Open? -1000.0: 1000.0));
	}

	void FloorSensor(size_t floor, CMotor &motor, int val, int dir)
	{
		motor; val; dir;
		SYS_LOCK(m_mx);
		if (m_floor == floor)
			return;

		m_floor = floor;
		bool stop = 
			!m_floor || 
			(m_floor == (m_params.m_floors - 1)) || 
			((m_buttons | m_calls) == 0) ||
			m_force_open ||
			false;

		if (stop)
			m_dir = dir_stop;
		else 
		{
			const auto floor_mask = TFloorMask(1) << m_floor;
			const auto floors_up = ~(floor_mask - 1) & ~floor_mask;

			stop = 
				m_buttons & floor_mask || 
				(m_calls & floor_mask && (m_dir != dir_up || ((m_calls & floors_up) == 0))) || 
				false;
		}

		RaiseEvent(ev_floor);
		if (stop)
		{
			StartMotor<false>();	
			LiftStopped();
		}
	}

	template <size_t N>
	void DoorsSensor(CMotor &motor, int val, int dir)
	{
		val;
		SYS_LOCK(m_mx);
		const bool raised = dir > 0; 

		std::get<N>(m_doors) = raised;
		if constexpr (N == 0)
		{
			if (!raised)
			{
				motor.Stop();
				DoorsOpened();
			}
		}
		else
		{
			if (raised)
			{
				motor.Stop();
				DoorsClosed();
			}
		}
	}

	void LiftStopped()
	{
		SYS_LOCK(m_mx);
		RaiseEvent(ev_stop);

		const auto floor = TFloorMask(1) << m_floor;
		const bool open_doors = m_force_open || (m_buttons | m_calls) & floor;
		if (open_doors)
		{
			m_buttons &= ~floor;
			m_calls &= ~floor;
			OpenDoors(true);
			m_force_open = false;
		}
	}

	void DoorsOpened()
	{
		SYS_LOCK(m_mx);
				
		RaiseEvent(ev_doors_open);
		m_doors_timer.Start(m_params.m_close_tm, &CElevator::OpenDoors, this, false);
	}

	void DoorsClosed()
	{
		SYS_LOCK(m_mx);

		RaiseEvent(ev_doors_close);
		const auto buttons = m_buttons | m_calls;
		if (buttons == 0)
		{
			m_dir = dir_stop;
			return;
		}

		const auto floor = TFloorMask(1) << m_floor;
		if (buttons & floor)
		{
			CElevator::LiftStopped();
			return;
		}
		
		const auto floors_down = floor - 1;
		const auto floors_up = ~floors_down & ~floor;

		//Keep direction if possible, internal buttons has priority, up direction has priority
		if (m_dir == dir_down)
			m_dir = 
				(m_buttons & floors_down)? dir_down: 
				(m_buttons & floors_up)? dir_up:
				(buttons & floors_down)? dir_down: 
				dir_up;
		else
			m_dir = 
				(m_buttons & floors_up)? dir_up:	
				(m_buttons & floors_down)? dir_down:
				(buttons & floors_up)? dir_up: 
				dir_down;

		StartMotor<true>();
	}

	const CElevatorParams m_params;
	const std::function<void(TEvent, CElevator &)> m_fn;

	CMotor m_motor;
	CMotor m_doors_motor;

	Sys::CTimer m_doors_timer;

	mutable std::recursive_mutex m_mx;
	TFloorMask m_buttons = {0};
	TFloorMask m_calls = {0};
	
	size_t m_floor = 0;
	std::pair<bool, bool> m_doors{false, false};
	bool m_force_open = false;


	TDirection m_dir = dir_stop; 
};