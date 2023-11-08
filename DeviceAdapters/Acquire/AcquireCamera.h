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
#define ERR_ACQ_INIT 90001
#define ERR_ACQ_CONFIURE_FAILED 90002
#define ERR_UNSUPPORTED_PIXEL_TYPE 90003
#define ERR_INVALID_CAMERA_SELECTION 90004
#define ERR_UNKNOWN_LIVE 90005
#define ERR_TIMEOUT 90006
#define ERR_ACQ_MISSED_FRAME 90007
#define ERR_ACQ_TIMEOUT 90008
#define ERR_UNKNOWN_PIXEL_TYPE 90009
#define ERR_SOFTWARE_TRIGGER_NOT_AVAILABLE 90010
#define ERR_FAILED_CREATING_ACQ_DIR 90011

static const char* g_prop_CurrentDevice = "Device";
static const char* g_prop_SaveToZarr = "SaveToZarr";
static const char* g_prop_SaveRoot = "SaveRoot";
static const char* g_prop_SavePrefix = "SavePrefix";
static const char* g_prop_Camera_1 = "Camera_1";
static const char* g_prop_Camera_2 = "Camera_2";
static const char* g_prop_StreamFormat = "StreamFormat";
static const char* g_prop_ZarrChannels = "ZarrChannels";
static const char* g_prop_ZarrSlices = "ZarrSlices";
static const char* g_prop_ZarrFrames = "ZarrFrames";
static const char* g_prop_ZarrTimepoints = "ZarrTimepoints";
static const char* g_prop_ZarrOrder = "ZarrOrder";
static const char* g_prop_ZarrPositions = "ZarrPositions";
static const char* g_Camera_None = "None";

// constants for naming pixel types (allowed values of the "PixelType" property)
static const char* g_PixelType_8bit = "8bit";
static const char* g_PixelType_16bit = "16bit";


extern const char* cameraName;
struct AcquireRuntime;
struct AcquireProperties;
class SequenceThread;
struct AcquirePropertyMetadata;

class AcquireROI {
public:
	int x;
	int y;
	int xSize;
	int ySize;

	AcquireROI() : x(0), y(0), xSize(0), ySize(0) {}
	~AcquireROI() {}
};

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
	int OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnSaveToZarr(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnSaveRoot(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnSavePrefix(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnStreamFormat(MM::PropertyBase* pProp, MM::ActionType eAct);

	int OnZarrChannels(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnZarrSlices(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnZarrFrames(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnZarrPositions(MM::PropertyBase* pProp, MM::ActionType eAct);
	int OnZarrOrder(MM::PropertyBase* pProp, MM::ActionType eAct);

private:
	bool initialized_;
	static AcquireCamera* g_instance;
	AcquireRuntime* runtime;
	std::vector<ImgBuffer> imgs;
	bool demo;
	bool saveToZarr;
	std::string saveRoot;
	std::string savePrefix;
	std::string currentDirName;
	std::string camera1;
	std::string camera2;
	SequenceThread* liveThread;
	bool stopOnOverflow;
	int currentCamera;
	bool multiChannel;
	AcquireROI fullFrame;
	int softwareTriggerId;
	std::string streamId;

	// zarr metadata
	long zarrChannels;
	long zarrSlices;
	long zarrFrames;
	long zarrPositions;
	long zarrOrder;

	int getAcquireProperties(AcquireProperties& props) const;
	int setAcquireProperties(AcquireProperties& props);
	static void reporter(int is_error, const char* file, int line, const char* function, const char* msg);
	int readSnapImageFrames();
	int readLiveFrames(int& framesRead);
	void setupBuffers(unsigned width, unsigned height, unsigned depth, bool dual);
	bool isDual() { return camera2.compare(g_Camera_None) != 0; }
	int abortCpx();
	void generateSyntheticImage(int channel, uint8_t value);
	int setPixelType(const char* pixType);
	int getPixelType(std::string& pixType);
	int setBinning(int bin);
	int getBinning(int& bin);
	int setupBuffers();
	int enterZarrSave();
	int exitZarrSave();
	int getSoftwareTrigger(AcquirePropertyMetadata& meta, int stream);
	static void setFileName(AcquireProperties& props, int stream, const std::string& fileName);
};
