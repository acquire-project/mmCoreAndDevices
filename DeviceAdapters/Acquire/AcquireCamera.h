///////////////////////////////////////////////////////////////////////////////
// FILE:          AcquireCamera.h
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

#define ERR_INVALID_DEVICE_NAME 90000
#define ERR_CPX_INIT 90001
#define ERR_CPX_CONFIURE_FAILED 90002
#define ERR_UNSUPPORTED_PIXEL_TYPE 90003
#define ERR_INVALID_CAMERA_SELECTION 90004
#define ERR_UNKNOWN_LIVE 90005
#define ERR_TIMEOUT 90006
#define ERR_CPX_MISSED_FRAME 90007
#define ERR_CPX_TIMEOUT 90008

static const char* g_prop_CurrentDevice = "Device";
static const char* g_prop_Camera_1 = "Camera_1";
static const char* g_prop_Camera_2 = "Camera_2";
static const char* g_Camera_None = "None";

extern const char* cameraName;
struct CpxRuntime;
struct CpxProperties;
class SequenceThread;

class AcquireCamera : public CCameraBase<AcquireCamera>
{
public:
	AcquireCamera();
	~AcquireCamera();

	friend class SequenceThread;

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
	bool IsCapturing();
	unsigned GetNumberOfComponents() const;
	unsigned GetNumberOfChannels() const;
	int GetChannelName(unsigned channel, char* name);

	// property handlers
	int OnDevice(MM::PropertyBase* pProp, MM::ActionType eAct);

private:
	bool initialized_;
	static AcquireCamera* g_instance;
	CpxRuntime* cpx;
	std::vector<ImgBuffer> imgs;
	bool demo;
	std::string camera1;
	std::string camera2;
	SequenceThread* liveThread;
	bool stopOnOverflow;
	int currentCamera;
	bool multiChannel;

	int getCpxProperties(CpxProperties& props) const;
	int setCpxProperties(CpxProperties& props);
	static void reporter(int is_error, const char* file, int line, const char* function, const char* msg);
	int readSnapImageFrames();
	int readLiveFrames(int& framesRead);
	void setupBuffers(unsigned width, unsigned height, unsigned depth, bool dual);
	bool isDual() { return camera2.compare(g_Camera_None) != 0; }
	int abortCpx();
	void generateSyntheticImage(int channel, uint8_t value);
};
