#include <iostream>
#include <coroutine>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <memory>
#include <map>
using namespace std;
using namespace std::chrono;

template<typename T>
class Coroutine {
public:
    struct promise_type {
        T value_;
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        void unhandled_exception() noexcept {}
        Coroutine get_return_object() { return Coroutine{ handle_type::from_promise(*this) }; }
        void return_void() {}
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Coroutine(handle_type handle) : coro_handle(handle) { started_time_ = time(nullptr); }

    ~Coroutine() {}
    
    handle_type get_handle() const {
        return coro_handle;
    }
    time_t started_time_ = 0;
private:
    handle_type coro_handle;
};

class Server;
class IModule
{
public:
    virtual void Init(Server* s) {};
    virtual void Start() {};
    virtual void Update() {};
    virtual void Destroy() {};
};


typedef int socket_t;
typedef std::function< Coroutine<int> (const socket_t sock, const int msg_id, const char* msg, const uint32_t len)> NET_RECEIVE_FUNCTOR;
typedef std::shared_ptr<NET_RECEIVE_FUNCTOR> NET_RECEIVE_FUNCTOR_PTR;
class INet : IModule
{
public:

    template <typename BaseType>
    bool Register(const int msg_id, BaseType* pBase, Coroutine<int> (BaseType::* handleReceiver)(const socket_t, const int, const char*, const uint32_t)) {
        NET_RECEIVE_FUNCTOR functor =
            std::bind(handleReceiver, pBase, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        NET_RECEIVE_FUNCTOR_PTR functorPtr(new NET_RECEIVE_FUNCTOR(functor));
        return AddReceiveCallBack(msg_id, functorPtr);
    }
private:
    virtual bool AddReceiveCallBack(const int msg_id, const NET_RECEIVE_FUNCTOR_PTR& cb) = 0;
};

class Net : public INet
{
public:
    bool AddReceiveCallBack(const int msg_id, const NET_RECEIVE_FUNCTOR_PTR& cb) {
        callbacks[msg_id] = cb;
        return true;
    }
    void Init(Server* s);
    void Start() override;
    void Update() override;
    void Destroy() override {

    }

private:
    std::map<int, NET_RECEIVE_FUNCTOR_PTR> callbacks;
    Server* s_;
};

class Work : public IModule {
    void Init(Server* s) override;
    void Start() override;
    void Update() override {};
    void Destroy() override {};
    Coroutine<int> HandleReq(const socket_t sock, const int msg_id, const char* data, const uint32_t data_len);
private:
    Server* s_;
    INet* net_;
};

class Server
{
public:
    void Init() {
        modules_["net"] = (IModule*)new Net();
        modules_["work"] = (IModule*)new Work();
        for (auto m : modules_) { m.second->Init(this); }
    }

    void Start() {
        for (auto m : modules_) { m.second->Start(); }
    }
    void Update() {
        for (auto m : modules_) { m.second->Update(); }
    }
    void Destroy() {
        for (auto m : modules_) { m.second->Destroy(); delete m.second; }
    }

    map<string, IModule*> modules_;
    bool is_quit_ = false;
    vector<Coroutine<int>> cos_;
};

void Work::Init(Server *s)
{
    cout << "work init\n";
    this->s_ = s;

    // register handler
    net_ = (INet*)s_->modules_["net"];
}

void Work::Start() {
    this->net_->Register(1, this, &Work::HandleReq);
}

Coroutine<int> Work::HandleReq(const socket_t sock, const int msg_id, const char* data, const uint32_t data_len) {
    cout << "A new corotine stated, Handle Req Recived: " << data << endl;
    co_await suspend_always{};
    std::cout << "Coroutine resumed after another suspend_always\n";
    co_return;
}


void Net::Init(Server* s) {
    cout << "net init\n";
    this->s_ = s;
}

void Net::Start()
{

}

void Net::Update()
{
    // 模拟网络请求
    static time_t expire_time = 0;
    time_t nowtime = time(nullptr);
    if (expire_time > nowtime) {
        return;
    }
    
    for (auto& c : callbacks) {
        auto func = c.second.get();
        string data = "test request";
        auto co = func->operator()(0, c.first, data.data(), data.size());
        s_->cos_.push_back(co);
        // start run
        co.get_handle().resume();
    }
    
    expire_time = nowtime + 3;
}

int main() {
    Server s;
    s.Init();
    s.Start();
    while (!s.is_quit_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        s.Update();
    }
    s.Destroy();
    return 0;
}


