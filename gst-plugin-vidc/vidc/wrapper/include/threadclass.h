/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef THREADCLASS_H
#define THREADCLASS_H
#include <stdint.h>
#include <functional>
#include "MMThread.h"

class ThreadClass {
public:
    typedef std::function<void()> ThreadEntryType; // Thread entry function

    ThreadClass();

    virtual ~ThreadClass() {}

    void release(); // Release thread

    // If deriving from this class, invoke this start method
    void start(
        const char* namePtr,
        uint32_t stackSize = 0, // Stack size in bytes, 0 for default of 8K
        int32_t priority = MM_Thread_DefaultPriority);

    // If the thread is a data member of the class, invoke this start
    // method and supply the thread entry function.
    void start(
        const char* namePtr,
        ThreadEntryType threadFcn, // Thread processing function
        uint32_t stackSize = 0, // Stack size in bytes, 0 for default of 8K
        int32_t priority = MM_Thread_DefaultPriority);

    void wait();
    void detach();

protected:
    MM_HANDLE mThreadHandle;

    ThreadEntryType mThreadEntryFcn;

private:
    static int threadEntry(void* arg);
};

#endif // THREADCLASS_H
