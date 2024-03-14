#include <iostream>
#include <coroutine>
#include <vector>
#include <memory>
#include <map>
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

    explicit Coroutine(handle_type handle) : coro_handle(handle) {}

    ~Coroutine() {}

    handle_type get_handle() const {
        return coro_handle;
    }

private:
    handle_type coro_handle;
};

Coroutine<int> doasync(int i) {
    cout << "doasync started " << i << endl;
    co_await std::suspend_always{};
    std::cout << "Coroutine resumed after suspend_always co: " << i << "\n";

    co_await std::suspend_always{};
    std::cout << "Coroutine resumed after another suspend_always\n";
    
    co_return;
}

void MakeCos(map<int, void*>& ptr_co_manager)
{
    auto co = doasync(1);
    auto co2 = doasync(2);
    ptr_co_manager[0] = co.get_handle().address();
    ptr_co_manager[1] = co2.get_handle().address();
    std::cout << "create co1 and co2\n";
}

int main() {

    cout << "Courinte example:" << endl;

    map<int, void*> co_addrs;
    MakeCos(co_addrs);

    
    auto co1 = Coroutine<int>::handle_type::from_address(co_addrs[1]);
    cout << "\nStart coroutines \n";
    for (auto& iter : co_addrs) {
        auto co = Coroutine<int>::handle_type::from_address(iter.second);
        cout << "Start to run " << iter.first << endl;
        co.resume();
        cout << "Return from " << iter.first << endl;
    }

    cout << "\nResum coroutines again\n";
    for (auto& iter : co_addrs) {
        auto co = Coroutine<int>::handle_type::from_address(iter.second);
        cout << "Start to run " << iter.first << endl;
        co.resume();
        cout << "Return from " << iter.first << endl;
    }

    cout << "\nResum coroutines again\n";
    for (auto& iter : co_addrs) {
        auto co = Coroutine<int>::handle_type::from_address(iter.second);
        cout << "Start to run " << iter.first << endl;
        co.resume();
        cout << "Return from " << iter.first << endl;
    }

    // free memory
    cout << "\nFree coroutines objects\n";
    for (auto& iter : co_addrs)
    {
        auto co = Coroutine<int>::handle_type::from_address(iter.second);
        if (co.done())
        {
            cout << "destroy: " << iter.first << " address: " << co.address() << endl;
            co.destroy();
        }
    }

    return 0;
}

// Output:
/*
Courinte example:
create co1 and co2

Start coroutines
Start to run 0
doasync started 1
Return from 0
Start to run 1
doasync started 2
Return from 1

Resum coroutines again
Start to run 0
Coroutine resumed after suspend_always co: 1
Return from 0
Start to run 1
Coroutine resumed after suspend_always co: 2
Return from 1

Resum coroutines again
Start to run 0
Coroutine resumed after another suspend_always
Return from 0
Start to run 1
Coroutine resumed after another suspend_always
Return from 1

Free coroutines objects
destroy: 0 address: 000002E5EEC9DB90
destroy: 1 address: 000002E5EECA32E0
*/