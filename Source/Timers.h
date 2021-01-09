#pragma once

#include <chrono>

class Timer
{
public:
	void Start()
	{
		m_Start = std::chrono::steady_clock::now();
	}

	std::chrono::microseconds GetElapsedMicroseconds()
	{
		auto stop = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::microseconds>( stop - m_Start );
	}

	std::chrono::milliseconds GetElapsedMilliseconds()
	{
		auto stop = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>( stop - m_Start );
	}

	std::chrono::duration<float> GetElapsedSecondsFloat()
	{
		auto stop = std::chrono::steady_clock::now();
		return stop - m_Start;
	}

private:
	std::chrono::steady_clock::time_point m_Start;
};


class FrameTimer : private Timer
{
public:
	FrameTimer()
		: m_IsFirstFrame( true )
		, m_CurrentFrameDeltaTime( 0.0f )
	{
	}

	float GetCurrentFrameDeltaTime()
	{
		return m_CurrentFrameDeltaTime;
	}

	void BeginFrame()
	{
		CalculateFrameDeltaTime();
		Start();
		m_IsFirstFrame = false;
	}

private:
	void CalculateFrameDeltaTime()
	{
		if ( !m_IsFirstFrame )
		{
			m_CurrentFrameDeltaTime = GetElapsedSecondsFloat().count();
		}
	}

	bool  m_IsFirstFrame;
	float m_CurrentFrameDeltaTime;
};