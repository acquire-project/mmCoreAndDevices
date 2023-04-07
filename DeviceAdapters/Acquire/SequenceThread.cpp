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
#include "SequenceThread.h"
#include "AcquireCamera.h"

SequenceThread::SequenceThread(AcquireCamera* pCam)
   : camera(pCam), stop(false), imageCounter(0), numImages(0), intervalMs(0.0)
{
}

int SequenceThread::svc()
{
   running = true;
   imageCounter = 0;
   try
   {
      while (!stop)
      {
         int framesRead(0);
         int ret = camera->readLiveFrames(framesRead);
         if (ret != DEVICE_OK)
         {
            camera->LogMessage("Reading live frames failed: " + ret);
            stop = true;
         }
         imageCounter += framesRead;
         if (numImages != 0 && imageCounter >= numImages)
            break;
      }
   }
   catch (...)
   {
      return ERR_UNKNOWN_LIVE;
   }
   running = false;
   return DEVICE_OK;
}

void SequenceThread::Stop()
{
   stop = true;
}

void SequenceThread::Start(long numImages, double intervalMs)
{
   numImages = numImages;
   intervalMs = intervalMs;
   imageCounter = 0;
   stop = false;
   activate();
}
