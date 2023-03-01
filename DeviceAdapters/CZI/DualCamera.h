///////////////////////////////////////////////////////////////////////////////
// FILE:          DualCamera.h
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   Micro-manager device adapter wrapper for CZI Acquire module
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

#include <string>
#include <vector>
#include "DeviceBase.h"
#include "ImgBuffer.h"

#define ERR_WRONG
#define ERR_INVALID_DEVICE_NAME 90000
#define ERR_CPX_INIT 90001
#define ERR_CPX_CONFIURE_FAILED 90002

extern const char* cameraName;
struct CpxRuntime;
struct CpxProperties;

class DualCamera : public CCameraBase<DualCamera>
{
public:
	DualCamera();
	~DualCamera();

	// Inherited via CCameraBase
	int Initialize();
	int Shutdown();
	void GetName(char * name) const;
	long GetImageBufferSize() const;
	unsigned GetBitDepth() const;
	int GetBinning() const;
	int SetBinning(int binSize);
	void SetExposure(double exp_ms);
	double GetExposure() const;
	int SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize);
	int GetROI(unsigned & x, unsigned & y, unsigned & xSize, unsigned & ySize);
	int ClearROI();
	int IsExposureSequenceable(bool & isSequenceable) const;
	const unsigned char * GetImageBuffer();
	const unsigned char* GetImageBuffer(unsigned channel);
	unsigned GetImageWidth() const;
	unsigned GetImageHeight() const;
	unsigned GetImageBytesPerPixel() const;
	int SnapImage();
	int StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow);
	int StopSequenceAcquisition();
	unsigned GetNumberOfComponents() const;
	unsigned GetNumberOfChannels() const;
	int GetChannelName(unsigned channel, char* name);

private:
	bool initialized_;
	static DualCamera* g_instance;
	CpxRuntime* cpx;
	std::vector<ImgBuffer> imgs;

	int getCpxProperties(CpxProperties& props) const;
	int setCpxProperties(CpxProperties& props);
	static void reporter(int is_error, const char* file, int line, const char* function, const char* msg);
	int readFrame(int stream, CpxProperties& props);
};
