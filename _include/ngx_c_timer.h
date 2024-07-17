#ifndef NGX_C_TIMER_H
#define NGX_C_TIMER_H

#include <set>
#include <functional>

using callback = std::function<void(void*)>;

struct TimerInfo{
    TimerInfo(time_t msec, time_t data, callback func, int id, int count, void* arg) : t_msec(msec), t_data(data), t_func(func), t_id(id), t_count(count), t_arg(arg)
    {}
    time_t t_msec;
    time_t t_data;
    callback t_func;
    int t_id;
    int t_count;
    void* t_arg;
};

bool operator<(const TimerInfo& t1, const TimerInfo& t2);
bool operator==(const TimerInfo& t1, const TimerInfo& t2);

class Timer{
private:
    Timer();
    ~Timer();

private:
    static Timer* m_instance;
    class Destroy_Timer{
    public:
        ~Destroy_Timer();
    };
    static void Create(void);
    time_t GetTime(void);
    void AddTimer(TimerInfo info);
public:
    static Timer* GetInstance(void);
    void AddTimer(time_t msec, int count, callback&& f, void* arg); //添加定时器事件
    void DelTimer(const TimerInfo& timer);    //删除定时器事件
    void CheckTimer(void);      //处理定时事件
    time_t SetTime(void);    //设置epoll等待时间
private:
    static int id;
    std::multiset<TimerInfo, std::less<>> Timer_buf;
};


#endif