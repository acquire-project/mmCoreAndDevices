///////////////////////////////////////////////////////////////////////////////
// FILE:          SequenceThread.h
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   Camera/CPX streaming thread
//
// AUTHOR:        Nenad Amodaj nenad@amodaj.com
//
// COPYRIGHT:     2023 Chan Zuckerberg Initiative (CZI)
// LICENSE:       Licensed under the Apache License, Version 2.0 (the "License");
//                you may not use this file except in compliance with the License.
//                You may obtain a copy of the License at
//                
//                http://www.apache.org/licenses/LICENSE-2.0
//                
//                Unless required by applicable law or agreed to in writing, software
//                distributed under the License is distributed on an "AS IS" BASIS,
//                WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//                See the License for the specific language governing permissions and
//                limitations under the License.
#pragma once
#include "DeviceThreads.h"
#include <atomic>

class AcquireCamera;

class SequenceThread : public MMDeviceThreadBase
{
public:
   SequenceThread(AcquireCamera* pCam);
   ~SequenceThread() {}
   void Stop();
   void Start(long numImages, double intervalMs);
   bool IsActive() { return running; }

private:
   int svc(void);
   AcquireCamera* camera;
   long numImages;
   long imageCounter;
   double intervalMs;
   std::atomic<bool> stop;
   std::atomic<bool> running;
};
