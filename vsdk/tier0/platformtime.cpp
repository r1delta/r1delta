//========= Copyright Valve Corporation, All rights reserved. ============//
#include "../public/tier0/platformtime.h"

#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <time.h>

#ifdef _WIN32
	#include "winlite.h"
#else
	#include <sys/time.h>
	#include <unistd.h>
	#if IsOSX()
		#include <mach/mach.h>
		#include <mach/mach_time.h>
	#endif
#endif

#if IsNintendoSwitch()
	#include "platformtime_nswitch.h"
#endif

BEGIN_TIER0_NAMESPACE

#ifdef _WIN32
namespace
{
	struct NativeTimeFields
	{
		short Year;
		short Month;
		short Day;
		short Hour;
		short Minute;
		short Second;
		short Milliseconds;
		short Weekday;
	};

	struct NativeTimeZoneInformation
	{
		LONG Bias;
		WCHAR StandardName[32];
		NativeTimeFields StandardStart;
		LONG StandardBias;
		WCHAR DaylightName[32];
		NativeTimeFields DaylightStart;
		LONG DaylightBias;
	};
}

extern "C"
{
	__declspec( dllimport ) LONG NTAPI NtQueryPerformanceCounter( PLARGE_INTEGER PerformanceCounter, PLARGE_INTEGER PerformanceFrequency );
	__declspec( dllimport ) LONG NTAPI NtQuerySystemTime( PLARGE_INTEGER SystemTime );
	__declspec( dllimport ) LONG NTAPI RtlSystemTimeToLocalTime( PLARGE_INTEGER SystemTime, PLARGE_INTEGER LocalTime );
	__declspec( dllimport ) void NTAPI RtlTimeToTimeFields( PLARGE_INTEGER Time, NativeTimeFields *TimeFields );
	__declspec( dllimport ) BOOLEAN NTAPI RtlTimeFieldsToTime( NativeTimeFields *TimeFields, PLARGE_INTEGER Time );
	__declspec( dllimport ) LONG NTAPI RtlQueryTimeZoneInformation( NativeTimeZoneInformation *TimeZoneInformation );

	uint64 g_ClockSpeed = 1;
	unsigned long g_dwClockSpeed = 1;
	double g_ClockSpeedMicrosecondsMultiplier = 1000000.0;
	double g_ClockSpeedMillisecondsMultiplier = 1000.0;
	double g_ClockSpeedSecondsMultiplier = 1.0;
}
#endif

#if defined( _WIN32 ) || IsOSX() || IsNintendoSwitch()
	static uint64 g_TickFrequency;
	static double g_TickFrequencyDouble;
	static double g_TicksToUS;
#else
	static constexpr double g_TickFrequencyDouble = 1.0e9;
	static constexpr double g_TicksToUS = 1.0e6 / g_TickFrequencyDouble;
#endif

#ifdef _WIN32
namespace
{
	constexpr uint64 kNtTicksPerSecond = 10000000;
	constexpr uint64 kNtTicksPerMinute = 60 * kNtTicksPerSecond;
	constexpr uint64 kNtUnixEpoch = 11644473600ULL * kNtTicksPerSecond;
	constexpr double kBenchmarkFrameTime = 1.0 / 66.0;

	std::atomic<int> g_TickInitState{};
	std::atomic<uint64> g_TickLastReturned{};
	std::atomic<bool> g_BenchmarkMode{};
	std::atomic<uint64> g_BenchmarkTimeBits{};

	[[noreturn]] void FailNativeClock()
	{
		__fastfail( FAST_FAIL_FATAL_APP_EXIT );
	}

	bool NativeQueryPerformanceCounter( uint64 &counter, uint64 *frequency = nullptr )
	{
		LARGE_INTEGER nativeCounter{};
		LARGE_INTEGER nativeFrequency{};
		const LONG status = NtQueryPerformanceCounter( &nativeCounter, frequency ? &nativeFrequency : nullptr );
		if ( status < 0 || nativeCounter.QuadPart < 0 || ( frequency && nativeFrequency.QuadPart <= 0 ) )
			return false;

		counter = static_cast<uint64>( nativeCounter.QuadPart );
		if ( frequency )
			*frequency = static_cast<uint64>( nativeFrequency.QuadPart );
		return true;
	}

	uint64 DetectTimestampFrequency( uint64 performanceFrequency )
	{
		int registers[4]{};
		__cpuid( registers, 0 );
		const int maxBasicLeaf = registers[0];

		if ( maxBasicLeaf >= 0x15 )
		{
			__cpuidex( registers, 0x15, 0 );
			const uint32 denominator = static_cast<uint32>( registers[0] );
			const uint32 numerator = static_cast<uint32>( registers[1] );
			const uint32 crystalFrequency = static_cast<uint32>( registers[2] );
			if ( denominator && numerator && crystalFrequency )
				return ( static_cast<uint64>( crystalFrequency ) * numerator ) / denominator;
		}

		if ( maxBasicLeaf >= 0x16 )
		{
			__cpuidex( registers, 0x16, 0 );
			const uint32 baseMHz = static_cast<uint32>( registers[0] ) & 0xFFFF;
			if ( baseMHz )
				return static_cast<uint64>( baseMHz ) * 1000000ULL;
		}

		uint64 counterStart;
		if ( !NativeQueryPerformanceCounter( counterStart ) )
			FailNativeClock();

		_mm_lfence();
		const uint64 timestampStart = __rdtsc();
		const uint64 calibrationTicks = performanceFrequency / 100;
		uint64 counterEnd = counterStart;
		do
		{
			_mm_pause();
			if ( !NativeQueryPerformanceCounter( counterEnd ) )
				FailNativeClock();
		} while ( counterEnd - counterStart < calibrationTicks );

		_mm_lfence();
		const uint64 timestampEnd = __rdtsc();
		const uint64 counterDelta = counterEnd - counterStart;
		if ( !counterDelta || timestampEnd <= timestampStart )
			FailNativeClock();

		return static_cast<uint64>(
			static_cast<long double>( timestampEnd - timestampStart ) * performanceFrequency / counterDelta + 0.5L );
	}

	void InitializeFastTimerGlobals( uint64 performanceFrequency )
	{
		g_ClockSpeed = DetectTimestampFrequency( performanceFrequency );
		if ( !g_ClockSpeed )
			FailNativeClock();

		g_dwClockSpeed = static_cast<unsigned long>( g_ClockSpeed );
		g_ClockSpeedMicrosecondsMultiplier = 1000000.0 / static_cast<double>( g_ClockSpeed );
		g_ClockSpeedMillisecondsMultiplier = 1000.0 / static_cast<double>( g_ClockSpeed );
		g_ClockSpeedSecondsMultiplier = 1.0 / static_cast<double>( g_ClockSpeed );
	}

	uint64 ScaleTicks( uint64 ticks, uint64 unitsPerSecond )
	{
		return ( ticks / g_TickFrequency ) * unitsPerSecond +
			( ticks % g_TickFrequency ) * unitsPerSecond / g_TickFrequency;
	}

	double AdvanceBenchmarkTime()
	{
		uint64 oldBits = g_BenchmarkTimeBits.load( std::memory_order_relaxed );
		for ( ;; )
		{
			const double next = std::bit_cast<double>( oldBits ) + kBenchmarkFrameTime;
			const uint64 nextBits = std::bit_cast<uint64>( next );
			if ( g_BenchmarkTimeBits.compare_exchange_weak(
				oldBits, nextBits, std::memory_order_relaxed, std::memory_order_relaxed ) )
				return next;
		}
	}

	bool IsLeapYear( int year )
	{
		return ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
	}

	int DayOfYear( int year, int month, int day )
	{
		static constexpr int daysBeforeMonth[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
		if ( month < 1 || month > 12 || day < 1 )
			return 0;
		return daysBeforeMonth[month - 1] + day - 1 + ( month > 2 && IsLeapYear( year ) ? 1 : 0 );
	}

	int IsDaylightTime( const LARGE_INTEGER &systemTime, const LARGE_INTEGER &localTime )
	{
		NativeTimeZoneInformation timeZone{};
		if ( RtlQueryTimeZoneInformation( &timeZone ) < 0 || !timeZone.DaylightStart.Month )
			return 0;

		const LONGLONG biasTicks = systemTime.QuadPart - localTime.QuadPart;
		const LONGLONG daylightBiasTicks =
			static_cast<LONGLONG>( timeZone.Bias + timeZone.DaylightBias ) * kNtTicksPerMinute;
		return biasTicks == daylightBiasTicks ? 1 : 0;
	}

	bool FillCalendarTime( const LARGE_INTEGER &time, const LARGE_INTEGER *systemTime, tm *output )
	{
		if ( !output || time.QuadPart < 0 )
			return false;

		NativeTimeFields fields{};
		LARGE_INTEGER mutableTime = time;
		RtlTimeToTimeFields( &mutableTime, &fields );
		if ( fields.Year < 1601 || fields.Month < 1 || fields.Month > 12 )
			return false;

		output->tm_sec = fields.Second;
		output->tm_min = fields.Minute;
		output->tm_hour = fields.Hour;
		output->tm_mday = fields.Day;
		output->tm_mon = fields.Month - 1;
		output->tm_year = fields.Year - 1900;
		output->tm_wday = fields.Weekday;
		output->tm_yday = DayOfYear( fields.Year, fields.Month, fields.Day );
		output->tm_isdst = systemTime ? IsDaylightTime( *systemTime, time ) : 0;
		return true;
	}

	bool UnixSecondsToNativeTime( uint64 unixSeconds, LARGE_INTEGER &nativeTime )
	{
		constexpr uint64 maxSeconds =
			( static_cast<uint64>( std::numeric_limits<LONGLONG>::max() ) - kNtUnixEpoch ) / kNtTicksPerSecond;
		if ( unixSeconds > maxSeconds )
			return false;

		nativeTime.QuadPart = static_cast<LONGLONG>( kNtUnixEpoch + unixSeconds * kNtTicksPerSecond );
		return true;
	}

	bool QueryLocalCalendarTime( tm *output )
	{
		LARGE_INTEGER systemTime{};
		LARGE_INTEGER localTime{};
		if ( NtQuerySystemTime( &systemTime ) < 0 || RtlSystemTimeToLocalTime( &systemTime, &localTime ) < 0 )
			return false;
		return FillCalendarTime( localTime, &systemTime, output );
	}
}
#endif

static uint64 InitTicks();
static uint64 g_TickBase = InitTicks();

static uint64 InitTicks()
{
#if defined( _WIN32 )
	if ( g_TickInitState.load( std::memory_order_acquire ) == 2 )
		return g_TickBase;

	int expected = 0;
	if ( g_TickInitState.compare_exchange_strong( expected, 1, std::memory_order_acq_rel ) )
	{
		uint64 counter;
		if ( !NativeQueryPerformanceCounter( counter, &g_TickFrequency ) )
			FailNativeClock();

		g_TickFrequencyDouble = static_cast<double>( g_TickFrequency );
		g_TicksToUS = 1.0e6 / g_TickFrequencyDouble;
		g_TickBase = counter;
		g_TickLastReturned.store( counter, std::memory_order_relaxed );
		InitializeFastTimerGlobals( g_TickFrequency );
		g_TickInitState.store( 2, std::memory_order_release );
		return counter;
	}

	while ( g_TickInitState.load( std::memory_order_acquire ) != 2 )
		_mm_pause();
	return g_TickBase;
#elif IsOSX()
	mach_timebase_info_data_t TimebaseInfo;
	mach_timebase_info( &TimebaseInfo );
	g_TickFrequencyDouble = static_cast<double>( TimebaseInfo.denom ) / TimebaseInfo.numer * 1.0e9;
	g_TickFrequency = static_cast<uint64>( g_TickFrequencyDouble + 0.5 );
	g_TickBase = mach_absolute_time();
	g_TicksToUS = 1.0e6 / g_TickFrequencyDouble;
	return g_TickBase;
#elif IsNintendoSwitch()
	g_TickBase = PlatformTime_GetRawTickCounter();
	g_TickFrequency = PlatformTime_GetRawTickFrequency();
	g_TickFrequencyDouble = g_TickFrequency;
	g_TicksToUS = 1.0e6 / g_TickFrequencyDouble;
	return g_TickBase;
#elif IsPosix()
	timespec TimeSpec;
	clock_gettime( CLOCK_MONOTONIC, &TimeSpec );
	g_TickBase = static_cast<uint64>( TimeSpec.tv_sec ) * 1000000000 + TimeSpec.tv_nsec;
	return g_TickBase;
#else
#error Unknown platform
#endif
}

uint64 Plat_RelativeTicks()
{
	if ( g_TickBase == 0 )
		InitTicks();

	uint64 ticks;
#if defined( _WIN32 )
	if ( !NativeQueryPerformanceCounter( ticks ) )
		FailNativeClock();

	uint64 last = g_TickLastReturned.load( std::memory_order_relaxed );
	while ( ticks > last && !g_TickLastReturned.compare_exchange_weak(
		last, ticks, std::memory_order_relaxed, std::memory_order_relaxed ) )
	{
	}
	if ( ticks < last )
		ticks = last;
#elif IsOSX()
	ticks = mach_absolute_time();
#elif IsNintendoSwitch()
	ticks = PlatformTime_GetRawTickCounter();
#elif IsPosix()
	timespec TimeSpec;
	clock_gettime( CLOCK_MONOTONIC, &TimeSpec );
	ticks = static_cast<uint64>( TimeSpec.tv_sec ) * 1000000000 + TimeSpec.tv_nsec;
#else
#error Unknown platform
#endif
	return ticks;
}

double Plat_FloatTime()
{
#ifdef _WIN32
	if ( g_BenchmarkMode.load( std::memory_order_relaxed ) )
		return AdvanceBenchmarkTime();
#endif
	const uint64 ticks = Plat_RelativeTicks();
	return static_cast<double>( static_cast<int64>( ticks - g_TickBase ) ) / g_TickFrequencyDouble;
}

extern "C" uint32 Plat_MSTime()
{
#ifdef _WIN32
	if ( g_BenchmarkMode.load( std::memory_order_relaxed ) )
		return static_cast<uint32>( AdvanceBenchmarkTime() * 1000.0 );
#endif
	const uint64 ticks = Plat_RelativeTicks();
#ifdef _WIN32
	return static_cast<uint32>( ScaleTicks( ticks - g_TickBase, 1000 ) );
#else
	return static_cast<uint32>( static_cast<double>( ticks - g_TickBase ) * 1000.0 / g_TickFrequencyDouble );
#endif
}

uint64 Plat_USTime()
{
	const uint64 ticks = Plat_RelativeTicks();
	return static_cast<uint64>( ( ticks - g_TickBase ) * g_TicksToUS );
}

extern "C" uint64 Timer_GetTimeUS()
{
	return static_cast<uint64>( Plat_MSTime() ) * 1000;
}

extern "C" uint64 Plat_GetClockStart()
{
	if ( g_TickBase == 0 )
		InitTicks();
	return g_TickBase;
}

#ifdef _WIN32
extern "C" bool Plat_IsInBenchmarkMode()
{
	return g_BenchmarkMode.load( std::memory_order_relaxed );
}

extern "C" void Plat_SetBenchmarkMode( bool enabled )
{
	g_BenchmarkMode.store( enabled, std::memory_order_relaxed );
}

extern "C" void Plat_ConvertToLocalTime( uint64 unixSeconds, tm *output )
{
	if ( !output )
		return;

	LARGE_INTEGER systemTime{};
	LARGE_INTEGER localTime{};
	if ( !UnixSecondsToNativeTime( unixSeconds, systemTime ) ||
		RtlSystemTimeToLocalTime( &systemTime, &localTime ) < 0 ||
		!FillCalendarTime( localTime, &systemTime, output ) )
	{
		std::memset( output, 0, sizeof( *output ) );
	}
}

extern "C" void Plat_GetLocalTime( tm *output )
{
	if ( output && !QueryLocalCalendarTime( output ) )
		std::memset( output, 0, sizeof( *output ) );
}

extern "C" void Plat_GetTimeString( tm *timeValue, char *output, int maxBytes )
{
	if ( !output || maxBytes <= 0 )
		return;

	static constexpr const char *weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static constexpr const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	if ( !timeValue || timeValue->tm_wday < 0 || timeValue->tm_wday >= 7 ||
		timeValue->tm_mon < 0 || timeValue->tm_mon >= 12 )
	{
		output[0] = '\0';
		return;
	}

	std::snprintf(
		output, static_cast<size_t>( maxBytes ), "%s %s %2d %02d:%02d:%02d %d\n",
		weekdays[timeValue->tm_wday], months[timeValue->tm_mon], timeValue->tm_mday,
		timeValue->tm_hour, timeValue->tm_min, timeValue->tm_sec, timeValue->tm_year + 1900 );
	output[maxBytes - 1] = '\0';
}

extern "C" int Plat_gmtime( uint64 unixSeconds, tm *output )
{
	if ( !output )
		return EINVAL;

	LARGE_INTEGER nativeTime{};
	if ( !UnixSecondsToNativeTime( unixSeconds, nativeTime ) || !FillCalendarTime( nativeTime, nullptr, output ) )
	{
		std::memset( output, 0, sizeof( *output ) );
		return EINVAL;
	}
	return 0;
}

extern "C" int64 Plat_timegm( tm *timeValue )
{
	if ( !timeValue )
		return -1;

	NativeTimeFields fields{};
	fields.Year = static_cast<short>( timeValue->tm_year + 1900 );
	fields.Month = static_cast<short>( timeValue->tm_mon + 1 );
	fields.Day = static_cast<short>( timeValue->tm_mday );
	fields.Hour = static_cast<short>( timeValue->tm_hour );
	fields.Minute = static_cast<short>( timeValue->tm_min );
	fields.Second = static_cast<short>( timeValue->tm_sec );

	LARGE_INTEGER nativeTime{};
	if ( !RtlTimeFieldsToTime( &fields, &nativeTime ) || nativeTime.QuadPart < static_cast<LONGLONG>( kNtUnixEpoch ) )
		return -1;
	return ( nativeTime.QuadPart - static_cast<LONGLONG>( kNtUnixEpoch ) ) / kNtTicksPerSecond;
}

extern "C" void GetCurrentDate( int *day, int *month, int *year )
{
	tm now{};
	QueryLocalCalendarTime( &now );
	if ( day )
		*day = now.tm_mday;
	if ( month )
		*month = now.tm_mon + 1;
	if ( year )
		*year = now.tm_year + 1900;
}

extern "C" void GetCurrentDayOfTheWeek( int *day )
{
	tm now{};
	QueryLocalCalendarTime( &now );
	if ( day )
		*day = now.tm_wday;
}

extern "C" void GetCurrentDayOfTheYear( int *day )
{
	tm now{};
	QueryLocalCalendarTime( &now );
	if ( day )
		*day = now.tm_yday;
}
#endif

END_TIER0_NAMESPACE
