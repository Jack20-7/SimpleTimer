#include<iostream>
#include<sys/epoll.h>
#include<set>
#include<functional>
#include<chrono>
#include<memory>

//为了方便以后字段的拓展，将稳定存在的字段抽象到一个公共的基类中去
//并且将expire和id这两个比较字段抽象出来的优点就是
//c++14中新增加了一个特性，就是对于set容器的find这个成员函数，在进行查找时，可以不用传入key，只需要传入排序相关的字段的ok了
//所以这样在查找时可以传入TimerNodeBase来进行查找，这样就避免了func函数对象多次拷贝的开销.

struct TimerNodeBase{
    time_t expire;    //超时时间
    int64_t id;       //对一个定时器进行唯一标识的id
};

struct TimerNode : public TimerNodeBase{
    using Callback = std::function<void (const TimerNode& node)>;
    Callback func;
    bool repeat;         //是否重复触发
    time_t interval;    //循环触发的间隔
};

bool operator< (const TimerNodeBase& lhv,const TimerNodeBase& rhv){
    if(lhv.expire < rhv.expire){
        return true;
    }else if(lhv.expire > rhv.expire){
        return false;
    }
    return lhv.id < rhv.id;
}

class Timer{
public:
    //获取当前时间(从系统启动开始计算，单位是ms)
    static time_t GetTick(){
        auto sc = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
        auto tmp = std::chrono::duration_cast<std::chrono::milliseconds>(sc.time_since_epoch());
        return tmp.count();
    }

    static int64_t GetID(){
        return ++gid_;
    }

    //添加定时器
    //返回的TimerNodeBase用来对TimerNode进行唯一标识
    TimerNodeBase addTimer(time_t msec,TimerNode::Callback func,time_t interval = 0){
        TimerNode node;
        node.expire = GetTick() + msec;
        node.func = func;
        node.id = GetID();
        node.repeat = interval > 0 ? true : false;
        node.interval = interval;

        timers_.insert(node);

        return static_cast<TimerNodeBase>(node);
    }

    bool delTimer(TimerNodeBase& node){
        auto it = timers_.find(node);
        if(it != timers_.end()){
            timers_.erase(it);
            return true;
        }
        return false;
    }

    //检查是否有定时任务超时，如果有的话，就将第一个超时的定时任务给处理掉
    //如果没有的话，那么返回false
    bool checkTimer(){
        auto it = timers_.begin();
        if(it != timers_.end() && it->expire <= GetTick()){
            //如果有任务并且已经超时
            it->func(*it);
            if(it->repeat){
                addTimer(it->interval,it->func,it->interval);
            }
            timers_.erase(it);
            return true;
        }
        return false;
    }

    //没有定时任务      -    return -1
    //有定时任务超时    -    return 0
    //没有定时任务超时  -    return 最早触发的定时任务距离当前的超时时间
    time_t timeToSleep(){
        auto it = timers_.begin();
        if(it == timers_.end()){
            return -1;
        }
        time_t dis = it->expire - GetTick();
        return dis > 0 ? dis : 0;
    }
private:
    std::set<TimerNode,std::less<>> timers_;
    static int64_t gid_;  //全局递增的id
};

int64_t Timer::gid_ = 0;


//测试定时任务

int main(void){
    int fd = epoll_create(100);

    std::unique_ptr<Timer> timer = std::make_unique<Timer>();

    //std::endl可能涉及到刷新输出缓冲区的作用，所以这里不加会到处没输出
    timer->addTimer(1000,[](const TimerNode& node){
        std::cout << "node id: " << node.id << std::endl;
    });
    timer->addTimer(1000,[](const TimerNode& node){
        std::cout << "node id: " << node.id << std::endl;
    });

    auto id = timer->addTimer(5000,[](const TimerNode& node){
        std::cout << "node id: " << node.id << std::endl;
    });
    timer->delTimer(id);

    epoll_event ev[64] = {0};

    timer->addTimer(10000,[](const TimerNode& node){
        std::cout << "node id: " << node.id << std::endl;
    },1000);

    while(true){
        //std::cout << timer->timeToSleep() << std::endl;

        int n = epoll_wait(fd,ev,64,timer->timeToSleep());

        //对网络事件进行处理
        for(int i = 0 ;i < n;++i){
        }

        //下面就可以对定时任务进行处理
        while(timer->checkTimer());
    }

    return 0;
}

