#include <iostream>
#include <coroutine>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <memory>
#include <map>
#include <list>

using namespace std;
using namespace std::chrono;

class Awaitable;
class Server;

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
        // co_yield surport
        template<std::convertible_to<T> From>
        std::suspend_always yield_value(From&& from) {
            value_ = std::forward<From>(from); // caching the result in promise
            return {};
        }
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

struct ResumeData{
    string data;
};
typedef function< void(const int req_id, Awaitable *awaitable)> AwaitableHandler;
class Awaitable {
public:
    bool await_ready() { return false; }
    void await_suspend(coroutine_handle<> h)
    {
        cout << "Coroutine has suspend: " << h.address() << endl;
        cout << "Now call the handler\n";
        coroutine_handle_ = h;
        handler_.operator()(req_id, this);
        data_ = new ResumeData();
    }
    ResumeData* await_resume() { return data_; }
    ResumeData *data_ = nullptr;
    int req_id = 0;
    AwaitableHandler handler_;
    coroutine_handle<> coroutine_handle_;
};

class IModule {
public:
    virtual void Init(Server* s) {};
    virtual void Start() {};
    virtual void Update() {};
    virtual void Destroy() {};
};

class INetClient : public IModule {};
class NetClient : public INetClient {};

typedef int socket_t;
typedef function< Coroutine<int> (const socket_t sock, const int msg_id, const char* msg, const uint32_t len)> NET_RECEIVE_FUNCTOR;
typedef shared_ptr<NET_RECEIVE_FUNCTOR> NET_RECEIVE_FUNCTOR_PTR;
class INet : IModule {
public:
    template <typename BaseType>
    bool Register(const int msg_id, BaseType* pBase, Coroutine<int> (BaseType::* handleReceiver)(const socket_t, const int, const char*, const uint32_t)) {
        NET_RECEIVE_FUNCTOR functor =
            bind(handleReceiver, pBase, placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
        NET_RECEIVE_FUNCTOR_PTR functorPtr(new NET_RECEIVE_FUNCTOR(functor));
        return AddReceiveCallBack(msg_id, functorPtr);
    }
private:
    virtual bool AddReceiveCallBack(const int msg_id, const NET_RECEIVE_FUNCTOR_PTR& cb) = 0;
};

class Net : public INet {
public:
    bool AddReceiveCallBack(const int msg_id, const NET_RECEIVE_FUNCTOR_PTR& cb) {
        callbacks[msg_id] = cb;
        return true;
    }
    void Init(Server* s);
    void Start() override;
    void Update() override;
    void Destroy() override {}
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
    Awaitable DoSomeAsync(int req_id);
    void BindCoroutineHandler(const int req_id, Awaitable* awaitable);
private:
    Server* s_;
    INet* net_;
};

class Server {
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
    list<Coroutine<int>> cos_;
    map<int, Awaitable*> cos_data_;
};

void Work::Init(Server *s) {
    cout << "Work Module init\n";
    this->s_ = s;
    // register handler
    net_ = (INet*)s_->modules_["net"];
}

void Work::Start() {
    this->net_->Register(1, this, &Work::HandleReq);
}

Coroutine<int> Work::HandleReq(const socket_t sock, const int msg_id, const char* data, const uint32_t data_len) {
    cout << "A new corotine stated, Handle Req Recived: " << data << endl;
    ResumeData* resume_data = co_await DoSomeAsync(1);
    if (resume_data == nullptr) {
        cout << " Resume data is nullptr\n";
        co_return;
    }
    cout << "Coroutine resumed: Data: " << resume_data->data << endl;
    co_return;
}

Awaitable Work::DoSomeAsync(int req_id) {
    cout << "\nSimulate this server request another server\n";
    cout << "Bind corotine to req_id, " << " : " << req_id << endl;
    Awaitable awaitable;
    awaitable.req_id = req_id;
    awaitable.handler_ = bind(&Work::BindCoroutineHandler, this, placeholders::_1, placeholders::_2);
    return awaitable;
}

void Work::BindCoroutineHandler(const int req_id, Awaitable* awaitable) {
    cout << "Now can bind req_id with coroutine, req_id: " << req_id << " coroutine: " << awaitable->coroutine_handle_.address() << endl;
    s_->cos_data_[req_id] = awaitable;
}

void Net::Init(Server* s) {
    cout << "Net Module init\n";
    this->s_ = s;
}

void Net::Start() {}

void Net::Update()
{
    // Simulate network requests
    static time_t expire_time = 0;
    time_t nowtime = time(nullptr);
    if (expire_time > nowtime) {
        return;
    }
    
    // Crate one coroutine per request
    cout << "\nSimulate the client requested this server\n";
    for (auto& c : callbacks) {
        auto func = c.second.get();
        string data = "test request";
        auto co = func->operator()(0, c.first, data.data(), data.size());
        cout << "Net module: created a coroutine: " << co.get_handle().address() << endl;
        s_->cos_.push_back(co);
        // start run
        cout << "Net module: run coroutine...\n";
        co.get_handle().resume();
        cout << "Net module: recover from coroutine... for created\n";
    }
    
    cout << "\nSimulate this server request has received a reply\n";
    // 
    for (auto iter : s_->cos_data_) {
        auto awaitable = iter.second;
        // The reply data transfor to await return value.
        awaitable->data_->data = "network response";
        cout << "Net module: resume corotine: " << awaitable->coroutine_handle_.address() << endl;
        awaitable->coroutine_handle_.resume();
        cout << "Net module: recover from coroutine... for replyed\n";
    }

    // Check coroutines is timeout

    // Free the memory
    cout << "\nFree the coroutines\n";
    for (auto iter = s_->cos_.begin(); iter != s_->cos_.end();) {
        std::list<Coroutine<int>>::iterator citer = iter;
        iter++;
        if (citer->get_handle().done())
        {
            cout << "Destroyed coroutine: " << citer->get_handle().address() << endl;
            citer->get_handle().destroy();
            s_->cos_.erase(citer);
        }
    }
    cout << "Next round ...\n\n";

    expire_time = nowtime + 5;
}

int main() {
    cout << "The example about c++20 coroutine's usage in none blocked io server\n\n";
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

/*
The example about c++20 coroutine's usage in none blocked io server

Net Module init
Work Module init

Simulate the client requested this server
Net module: created a coroutine: 0000022F98893170
Net module: run coroutine...
A new corotine stated, Handle Req Recived: test request

Simulate this server request another server
Bind corotine to req_id,  : 1
Coroutine has suspend: 0000022F98893170
Now call the handler
Now can bind req_id with coroutine, req_id: 1 coroutine: 0000022F98893170
Net module: recover from coroutine... for created

Simulate this server request has received a reply
Net module: resume corotine: 0000022F98893170
Coroutine resumed: Data: network response
Net module: recover from coroutine... for replyed

Free the coroutines
Destroyed coroutine: 0000022F98893170
Next round ...
*/