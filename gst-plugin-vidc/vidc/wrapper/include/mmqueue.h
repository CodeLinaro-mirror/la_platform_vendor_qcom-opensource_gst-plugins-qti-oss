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

#ifndef MMQUEUE_H
#define MMQUEUE_H

extern "C" {
#include "MMCriticalSection.h"
#include "MMEvent.h"
#include "types.h"
}
#include <list>

template <typename ItemType>
class MMQueue {
public:
    MMQueue()
    {
        initialize();
    }

    void clear(); // Clear the queue

    bool isEmpty(); // Return true if queue is empty

    ItemType pop(); // Return item at head of queue, blocks until queue is not empty

    // non-blocking call that returns the head of the
    // queue if it's not empty.  Valid flag indicates
    // if the returned item is valid.
    ItemType popNoWait(bool& valid); // Return item at head of queue if any. non-blocking

    void push(ItemType item); // Add item to the end of the queue

    int size();

private:
    MM_HANDLE mevent;
    MM_HANDLE mMutex;
    // std::queue was crashing after a few hundred
    // push/pop cycles so using a list instead.
    std::list<ItemType> mList;
    int mChainId = -1;

    bool initialize();
};

template <typename ItemType>
void MMQueue<ItemType>::clear()
{
    MM_CriticalSection_Enter(mMutex);
    mList.clear();
    MM_CriticalSection_Leave(mMutex);
}

template <typename ItemType>
bool MMQueue<ItemType>::isEmpty()
{
    return mList.empty();
}

template <typename ItemType>
int MMQueue<ItemType>::size()
{
    return mList.size();
}

template <typename ItemType>
ItemType MMQueue<ItemType>::pop() // Return item at head of queue, blocks until
{ //   an item is available.
    ItemType item;
    int rc;

    MM_CriticalSection_Enter(mMutex);
    while (mList.empty()) // Loop until something is on the queue
    {
        MM_CriticalSection_Leave(mMutex); // Can't stay locked while waiting on signal
        rc = MM_Event_Wait(mevent, 0XFFFFFFFF); // Wait for something to be put on the queue
        MM_CriticalSection_Enter(mMutex);
        if (rc != 0) // If failure waiting for signal
        {
            MM_ERROR_MSG("MMQueue::pop Error waiting on signal %d", rc);
            MM_Event_Reset(mevent);
        }
        if (mList.empty()) // If we were signaled but the queue is empty
        {
            // Spurious signals have been seen during testing.
            // This check protects against that.
            // MM_ERROR_MSG("MMQueue::pop Signaled with empty queue");
            MM_Event_Reset(mevent);
        }
    }

    item = mList.front();
    mList.pop_front();
    MM_CriticalSection_Leave(mMutex);

    return item;
}

template <typename ItemType>
ItemType MMQueue<ItemType>::popNoWait(bool& valid)
{
    ItemType item = ItemType();

    MM_CriticalSection_Enter(mMutex);
    if (mList.empty() == true) {
        valid = false;
    } else {
        item = mList.front();
        mList.pop_front();
        valid = true;
    }
    MM_CriticalSection_Leave(mMutex);

    return item;
}

template <typename ItemType>
void MMQueue<ItemType>::push(ItemType item) // Add item to the end of the queue
{
    MM_CriticalSection_Enter(mMutex);
    mList.emplace_back(item);
    MM_CriticalSection_Leave(mMutex);
    MM_Event_Set(mevent);
}

template <typename ItemType>
bool MMQueue<ItemType>::initialize()
{
    int rc;

    mList.resize(16); // Resize and clear to pre-allocate elements
    mList.clear();
    MM_DBG_MSG("MMQueue::initialize");
    rc = MM_Event_Create(&mevent);
    if (rc != 0) {
        MM_ERROR_MSG("MMQueue::initialize Event create failed %d", rc);
    }

    rc = MM_CriticalSection_Create(&mMutex);
    if (rc != 0) {
        MM_ERROR_MSG("MMQueue::initialize CriticalSection create failed %d", rc);
    }

    return true;
}

#endif // MMQUEUE_H
