#pragma once

#include <thread>
#include "core.h"

using AllocateThreadIDFn = void(*)();
using FreeThreadIDFn = void(*)();

inline AllocateThreadIDFn AllocateThreadID = (AllocateThreadIDFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "AllocateThreadID");
inline FreeThreadIDFn FreeThreadID = (FreeThreadIDFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "FreeThreadID");

class CThread
{
	std::thread m_thread;

public:
    static int m_total_threads;

    template <typename Callable, typename... Args>
    CThread(Callable&& func, Args&&... args) {
        AllocateThreadID(); 
        m_total_threads++;
        m_thread = std::thread([=]() {
            func(args...); 
            });
    }

    ~CThread() {
        if (m_thread.joinable()) {
            m_thread.join(); 
        }
        m_total_threads--;
        FreeThreadID(); 
    }

    CThread(CThread&& other) noexcept : m_thread(std::move(other.m_thread)) {}
    CThread& operator=(CThread&& other) noexcept {
        if (this != &other) {
            if (m_thread.joinable()) {
                m_thread.join();
            }
            m_thread = std::move(other.m_thread);
        }
        return *this;
    }

    CThread(const CThread&) = delete;
    CThread& operator=(const CThread&) = delete;

    void join() {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void detach() {
        m_thread.detach();
    }
};