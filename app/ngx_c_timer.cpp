#include <mutex>
#include <chrono>
#include <unistd.h>

#include "ngx_c_timer.h"

Timer* Timer::m_instance = nullptr;
int Timer::id = 0;

bool operator<(const TimerInfo& t1, const TimerInfo& t2)
{
    if(t1.t_data < t2.t_data)
        return true;
    else if(t1.t_data > t2.t_data)
        return false;
    else
    {
        return t1.t_id < t2.t_id ? true : false;
    }
}

bool operator==(const TimerInfo& t1, const TimerInfo& t2)
{
    if(t1.t_id == t2.t_id)
    {
        return true;
    }
    return false;
}

Timer::Timer()
{
    
}

Timer::~Timer()
{ 
    Timer_buf.clear();
}

Timer::Destroy_Timer::~Destroy_Timer()
{
    if(Timer::m_instance != nullptr)
    {
        delete Timer::m_instance;
        Timer::m_instance = nullptr;
    }
}

void Timer::Create(void)
{
    if(Timer::m_instance == nullptr)
    {
        Timer::m_instance = new Timer;
        static Destroy_Timer DT;
    }
}

Timer* Timer::GetInstance(void)
{
    static std::once_flag create_flag;

    std::call_once(create_flag, Create);

    return Timer::m_instance;
}

time_t Timer::GetTime(void)
{
    auto data = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    auto tm = std::chrono::duration_cast<std::chrono::milliseconds>(data.time_since_epoch());

    return tm.count();
}

void Timer::AddTimer(time_t msec, int count, callback&& f, void* arg)
{
    char tt = 'z';
    time_t tm = GetTime();
    Timer_buf.emplace(msec, tm  + msec, f, id, count, arg);
    id++;
    //write(pipefd[1], &tt, 1);
}

void Timer::AddTimer(TimerInfo info)
{
    Timer_buf.insert(info);
}

void Timer::DelTimer(const TimerInfo& timer)
{
    Timer_buf.erase(timer);
}

time_t Timer::Timer::SetTime(void)
{
    auto it = Timer_buf.begin();
    if(it == Timer_buf.end())
    {
        return -1;
    }

    auto diss = it ->t_data - GetTime();
    return diss > 0 ? diss : 0;
}

void Timer::CheckTimer(void)
{
    auto it = Timer_buf.begin();

    if(it->t_count == 1)    //触发一次
    {
        (it->t_func)(it->t_arg);
        DelTimer(*it);
    }
    else if(it->t_count > 1)//触发多次
    {
        (it->t_func)(it->t_arg);
        TimerInfo t = *it;
        t.t_data = GetTime() + t.t_msec;
        t.t_count--;
        DelTimer(*it);
        AddTimer(t);
    }
    else if(it->t_count == -1)//一直触发
    {
        (it->t_func)(it->t_arg);
        TimerInfo t = *it;
        t.t_data = GetTime() + t.t_msec;
        DelTimer(*it);
        AddTimer(t);       
    }
}