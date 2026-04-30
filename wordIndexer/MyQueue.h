#pragma once
#include <Windows.h>
#include "MyBuf.h"
#include "Config.h"

// Fixed-capacity ring buffer of MyBuf* pointers.
// Thread-safe via two semaphores + critical section.
class MyQueue {
    MyBuf*          slots[N_BUFFERS];
    int             head;
    int             tail;
    CRITICAL_SECTION cs;
    HANDLE          semFull;   // counts filled slots
    HANDLE          semEmpty;  // counts empty slots

public:
    MyQueue() {
        head = tail = 0;
        InitializeCriticalSection(&cs);
        semFull  = CreateSemaphore(NULL, 0,          N_BUFFERS, NULL);
        semEmpty = CreateSemaphore(NULL, N_BUFFERS,  N_BUFFERS, NULL);
    }

    ~MyQueue() {
        DeleteCriticalSection(&cs);
        CloseHandle(semFull);
        CloseHandle(semEmpty);
    }

    void Enqueue(MyBuf* mb) {
        WaitForSingleObject(semEmpty, INFINITE);
        EnterCriticalSection(&cs);
        slots[tail] = mb;
        tail = (tail + 1) % N_BUFFERS;
        LeaveCriticalSection(&cs);
        ReleaseSemaphore(semFull, 1, NULL);
    }

    MyBuf* Dequeue() {
        WaitForSingleObject(semFull, INFINITE);
        EnterCriticalSection(&cs);
        MyBuf* mb = slots[head];
        head = (head + 1) % N_BUFFERS;
        LeaveCriticalSection(&cs);
        ReleaseSemaphore(semEmpty, 1, NULL);
        return mb;
    }
};
