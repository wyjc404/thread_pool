#pragma once

#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <thread>
#include <future>

//TODO: 任务窃取， 动态扩容

class join_threads {
    std::vector<std::thread>& threads;
public: 
    explicit join_threads(std::vector<std::thread>& threads_) : threads(threads_) {}

    ~join_threads() {

        std::cout << "Joining threads..." << std::endl;
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        std::cout << "All threads joined." << std::endl;
    }
};

class function_wrapper {
    struct impl_base {
        impl_base() = default;
        impl_base(const impl_base&) = delete;
        impl_base& operator=(const impl_base&) = delete;
        virtual ~impl_base() = default;
        virtual void call() = 0;
    };

    template <typename FunctionType>
    struct impl_type : impl_base {
        FunctionType func;
        impl_type(FunctionType&& f) : func(std::move(f)) {}
        void call() override { func(); }
    };

    impl_base* impl = nullptr;
public:

    template<typename FunctionType>
    function_wrapper(FunctionType&& func)
        : impl(new impl_type<FunctionType>(std::forward<FunctionType>(func))) {
        if(!impl) {
            throw std::bad_alloc();
        }
    }

    function_wrapper() = default;

    function_wrapper(function_wrapper&& other) noexcept : impl(other.impl) {
        other.impl = nullptr;
    }

    function_wrapper& operator=(function_wrapper&& other) noexcept {
        if(this != &other) {
            impl = other.impl;
            other.impl = nullptr;
        }
        return *this;
    }

    ~function_wrapper() {
        if(impl) {
            delete impl;
            impl = nullptr;
        }
    }

    void operator()() {
        if(impl) {
            impl->call();
        }
    }
};

struct pipe_line {
    pipe_line() 
        : mtx(std::make_unique<std::mutex>()), 
          cond_var(std::make_unique<std::condition_variable>()) {}
    std::deque<function_wrapper> tasks;
    std::unique_ptr<std::mutex> mtx;
    std::unique_ptr<std::condition_variable> cond_var;
};

class thread_pool final {
private:

    void push_task(function_wrapper&& task) {
        global_queue.front()->tasks.push_back(std::move(task));
        global_queue.front()->cond_var->notify_one();
        std::make_heap(global_queue.begin(), global_queue.end(), [&](pipe_line* left, pipe_line* right){
            return left->tasks.size() > right->tasks.size();
        });
    }

private:
    std::atomic<bool> done;
    std::atomic<size_t> thread_count;

    std::vector<pipe_line> local_queues;
    std::vector<pipe_line*> global_queue;

    std::unordered_map<std::thread::id, size_t> local_queue_index;
    
    static thread_local size_t index;

    bool active_threads[20];

    std::vector<std::thread> threads;
    join_threads joiner;

    void worker_thread() {
        index = local_queue_index[std::this_thread::get_id()];
        std::unique_lock<std::mutex> lock(*(local_queues[index].mtx));
        function_wrapper task;
       
        while(!done) {
            ((local_queues[index].cond_var))->wait(lock, [this] {
                return !local_queues[index].tasks.empty() || done;
            });

            if(local_queues[index].tasks.empty() || done) {
                continue;
            }

            active_threads[index] = true;

            task = std::move(local_queues[index].tasks.front());
            
            try {
                task();
                local_queues[index].tasks.pop_front();
            }
            catch(const std::exception& e) {
                std::cerr << "Exception in thread " << std::this_thread::get_id() << ": " << e.what() << std::endl;
            }
            // if(local_queues[index].tasks.empty()) {
            //     active_threads[index] = false;
            //     steal(index);
            // }
        }
    }

    void steal(size_t index) {
        for(size_t i = 0; i < thread_count; ++i) {
            if(i == index || !active_threads[i]) continue;

            if(!local_queues[i].tasks.empty()) {
                function_wrapper task = std::move(local_queues[i].tasks.back());
                local_queues[i].tasks.pop_back();
                local_queues[index].tasks.push_front(std::move(task));
                break;
            }
        }
    }
public:
    thread_pool(size_t size = std::thread::hardware_concurrency()) 
        : done(false), thread_count(size), joiner(threads){
        try{
            threads.reserve(size);
            local_queues.resize(size);
            global_queue.reserve(size);
            for(size_t i = 0; i < size; ++i) {
                threads.emplace_back(&thread_pool::worker_thread, this);
                local_queue_index.insert(std::make_pair(threads[threads.size() - 1].get_id(), i));
                global_queue.push_back(&local_queues[i]);
            }
            for(size_t i = 0; i < size; ++i) {
               active_threads[i] = false;
            }
        }
        catch(const std::exception& e) {
            done = true;
            throw std::runtime_error("Failed to create thread pool: " + std::string(e.what()));
        }
    }
    ~thread_pool() {
        wait();
        shutdown();
        std::cout << "Thread pool destroyed." << std::endl;
    }

    template <typename FunctionType>
    std::future<std::invoke_result_t<FunctionType>> submit(FunctionType&& func) {

        using return_type = std::invoke_result_t<FunctionType>;
        std::packaged_task<return_type()> task(std::forward<FunctionType>(func));
        std::future<return_type> res = task.get_future();

        if(done) {
            throw std::runtime_error("Thread pool is already stopped");
        }

        push_task(std::move(task));
        
        return res;
    }

    void print_task_count_in_thread() {
        for(size_t i = 0; i < thread_count; i++) {
            std::cout << "Thread " << i << " has " << local_queues[i].tasks.size() << " tasks." << std::endl;
        }
    }

    void shutdown() {
        done = true;
        for(auto& queue : local_queues) {
             queue.cond_var->notify_all();
        }
    }

    void wait() {
        bool all_done = false;
        while (!all_done) {
            all_done = true;
            for(auto& queue : local_queues) {
                all_done &= queue.tasks.empty();
            }
        }
    }
};


thread_local size_t thread_pool::index = 0;