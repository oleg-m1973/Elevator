#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>

#define SYS_LOCK(mx) std::lock_guard<std::decay_t<decltype(mx)>> __lock##__COUNTER__(mx);

namespace Sys
{
template <typename TMutex, typename... TT>
auto UniqueLock(TMutex &mx, TT&&... args)
{
	return std::unique_lock<TMutex>(mx, std::forward<TT>(args)...);
}

class CTimer
{
public:
	~CTimer()
	{
		Cancel();
	}

	template <typename TDuration, typename TFunc, typename... TT>
	void Start(TDuration &&timeout, TFunc &&func, TT&&... args)
	{
		Cancel();
		SYS_LOCK(m_mx);
		m_cancel = false;
		const auto tm = std::chrono::steady_clock::now() + timeout;
		auto fn = std::bind(std::forward<TFunc>(func), std::forward<TT>(args)...);
		m_future = std::async([this, tm, fn = std::move(fn)]()
		{
			auto lock = UniqueLock(m_mx);
			while (!m_cancel)
				if (m_cv.wait_until(lock, tm) == std::cv_status::timeout)
				{
					fn();
					break;
				}
		});
	}

	void Cancel() noexcept
	{
		if (!m_future.valid())
			return;

		{
			SYS_LOCK(m_mx);
			m_cancel = true;
			m_cv.notify_all();
		}
				
		try {m_future.wait();} catch(...) {;}
	}
protected:
	std::future<void> m_future;

	std::mutex m_mx;
	std::condition_variable m_cv;
	volatile bool m_cancel = false;
};

}