///////////////////////////////////////////////////////////////////////////////
// FILE:          AcquireCamera.cpp
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

#include "AcquireCamera.h"
#include "cpx.h"
#include "device/device.manager.h"
#include <sstream>
#include <chrono>
#include <thread>
#include "SequenceThread.h"


/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
#define SIZED(str) str, sizeof(str) - 1

using namespace std;

const char* cameraName = "AcquireCamera";
AcquireCamera* AcquireCamera::g_instance = nullptr;
const int DEMO_IMAGE_WIDTH = 640;
const int DEMO_IMAGE_HEIGHT = 480;
const int DEMO_IMAGE_DEPTH = 1;

const VideoFrame* next(VideoFrame* cur)
{
	return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
}

size_t ConsumedBytes (const VideoFrame* const cur, const VideoFrame* const end)
{
		return (uint8_t*)end - (uint8_t*)cur;
};


AcquireCamera::AcquireCamera() : initialized_(false), demo(true), stopOnOverflow(false)
{
	// instantiate cpx
	g_instance = this;
	cpx = cpx_init(AcquireCamera::reporter);
	auto dm = cpx_device_manager(cpx);
	if (!cpx || !dm)
	{
		g_instance = nullptr;
		LogMessage("CPX inistialize failed");
		return;
	}

	vector<string> devices;
	devices.push_back(g_Camera_None);
	for (uint32_t i = 0; i < device_manager_count(dm); ++i) {
		struct DeviceIdentifier identifier = {};
		int ret = device_manager_get(&identifier, dm, i);
		if (ret != CpxStatus_Ok) {
			LogMessage("cpx failed getting device identifier");
		}
		if (identifier.kind == DeviceKind_Camera)
			devices.push_back(identifier.name);
	}

	CreateProperty(MM::g_Keyword_Name, cameraName, MM::String, true);

	// Description
	CreateProperty(MM::g_Keyword_Description, "Records simultaneously from two Hammamatsu cameras", MM::String, true);

	// CameraName
	CreateProperty(MM::g_Keyword_CameraName, "AcquireCamera", MM::String, true);

	// CameraID
	CreateProperty(MM::g_Keyword_CameraID, "V1.0", MM::String, true);

	// device
	CreateProperty(g_prop_Camera_1, devices.size() ? devices[0].c_str() : g_Camera_None, MM::String, false, nullptr, true);
	SetAllowedValues(g_prop_Camera_1, devices);
	CreateProperty(g_prop_Camera_2, devices.size() ? devices[0].c_str() : g_Camera_None, MM::String, false, nullptr, true);
	SetAllowedValues(g_prop_Camera_2, devices);

	liveThread = new SequenceThread(this);
}

AcquireCamera::~AcquireCamera()
{
	Shutdown();
	delete liveThread;
}

int AcquireCamera::Initialize()
{
	if (initialized_)
		return DEVICE_OK;

	// cameras
	char val[MM::MaxStrLength];
	GetProperty(g_prop_Camera_1, val);
	camera1 = val;

	GetProperty(g_prop_Camera_2, val);
	camera2 = val;

	// if we are using simulated cameras, then we are in demo mode
	// TODO: make sure "demo" flag is necessary
	if (camera1.compare(camera2) == 0)
		return ERR_INVALID_CAMERA_SELECTION;

	if (camera1.compare(g_Camera_None) == 0)
		return ERR_INVALID_CAMERA_SELECTION;

	if (camera1.rfind("simulated", 0) == 0) {
		if (camera2.compare(g_Camera_None) != 0 && camera2.rfind("simulated", 0) != 0)
			return ERR_INVALID_CAMERA_SELECTION; // both cameras must be simulated

		demo = true;
	}
	else
		demo = false;

	// binning
	CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false);

	std::vector<std::string> binningValues;
	binningValues.push_back("1");
	SetAllowedValues(MM::g_Keyword_Binning, binningValues);

	// test cpx loading
	g_instance = this;
	cpx = cpx_init(AcquireCamera::reporter);
	auto dm = cpx_device_manager(cpx);
	if (!cpx || !dm)
	{
		g_instance = nullptr;
		return ERR_CPX_INIT;
	}

	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != CpxStatus_Ok)
		return ret;

	ret = device_manager_select(dm, DeviceKind_Camera, camera1.c_str(), camera1.size(), &props.video[0].camera.identifier);
	if (ret != CpxStatus_Ok)
		return ret;
	
	if (isDual())
	{
		ret = device_manager_select(dm, DeviceKind_Camera, camera2.c_str(), camera2.size(), &props.video[1].camera.identifier);
		if (ret != CpxStatus_Ok)
			return ret;
	}

	// disable storage
	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[0].storage.identifier);

	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[1].storage.identifier);

	if (demo)
	{
		props.video[0].camera.settings.binning = 1;
		props.video[0].camera.settings.pixel_type = DEMO_IMAGE_DEPTH == 2 ? SampleType_u16 : SampleType_u8;
		props.video[0].camera.settings.shape = { DEMO_IMAGE_WIDTH, DEMO_IMAGE_HEIGHT };
		props.video[0].max_frame_count = 1;

		if (isDual())
		{
			props.video[1].camera.settings.binning = 1;
			props.video[1].camera.settings.pixel_type = DEMO_IMAGE_DEPTH == 2 ? SampleType_u16 : SampleType_u8;
			props.video[1].camera.settings.shape = { DEMO_IMAGE_WIDTH, DEMO_IMAGE_HEIGHT };
			props.video[1].max_frame_count = 1;
		}
	}
	// TODO: what if not demo???

	ret = cpx_configure(cpx, &props);
	if (ret != CpxStatus_Ok)
		return ret;

	if (props.video[0].camera.settings.pixel_type > 1)
	{
		LogMessage("Acquire MM adapter does not support device pixel type");
		return ERR_UNSUPPORTED_PIXEL_TYPE;
	}

	setupBuffers(props.video[0].camera.settings.shape.x, props.video[0].camera.settings.shape.y, props.video[0].camera.settings.pixel_type + 1, isDual());

	// we are assuming that cameras are identical
	CreateProperty("LineIntervalUs", to_string(props.video[0].camera.settings.line_interval_us).c_str(), MM::Float, false);

	initialized_ = true;
	return DEVICE_OK;
}

int AcquireCamera::Shutdown()
{
	liveThread->Stop();
	liveThread->wait();

	if (cpx)
	{
		auto ret = cpx_shutdown(cpx);
		if (ret != CpxStatus_Ok)
			LogMessage("cpx_shutdown error: " + ret);
		cpx = nullptr;
		g_instance = nullptr;
	}

	initialized_ = false;

	return DEVICE_OK;
}

void AcquireCamera::GetName(char * name) const
{
	CDeviceUtils::CopyLimitedString(name, cameraName);
}

long AcquireCamera::GetImageBufferSize() const
{
	return imgs[0].Width() * imgs[0].Height() * imgs[0].Depth();
}

unsigned AcquireCamera::GetBitDepth() const
{
	return imgs[0].Depth() * 8;
}

int AcquireCamera::GetBinning() const
{
	return 1;
}

int AcquireCamera::SetBinning(int)
{
	return DEVICE_OK;
}

void AcquireCamera::SetExposure(double exposure)
{
	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);

	auto dm = cpx_device_manager(cpx);

	ret = device_manager_select(dm, DeviceKind_Camera, camera1.c_str(), camera1.size(), &props.video[0].camera.identifier);
	if (ret != CpxStatus_Ok)
	{
		LogMessage("CPX Select 1 failed");
	}

	if (isDual())
	{
		ret = device_manager_select(dm, DeviceKind_Camera, camera2.c_str(), camera2.size(), &props.video[1].camera.identifier);
		if (ret != CpxStatus_Ok)
			LogMessage("CPX Select 2 failed");
	}

	props.video[0].camera.settings.exposure_time_us = (float)(exposure * 1000);
	if (isDual())
		props.video[1].camera.settings.exposure_time_us = props.video[0].camera.settings.exposure_time_us;

	ret = setCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error setting exposure: code=" + ret);

}

double AcquireCamera::GetExposure() const
{
	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);
	return props.video[0].camera.settings.exposure_time_us / 1000.0;
}

int AcquireCamera::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{
	return DEVICE_OK;
}

int AcquireCamera::GetROI(unsigned & x, unsigned & y, unsigned & xSize, unsigned & ySize)
{

	return DEVICE_OK;
}

int AcquireCamera::ClearROI()
{
	return DEVICE_OK;
}

int AcquireCamera::IsExposureSequenceable(bool & isSequenceable) const
{
	isSequenceable = false;

	return DEVICE_OK;
}

const unsigned char * AcquireCamera::GetImageBuffer()
{
	return imgs[0].GetPixels();
}

const unsigned char* AcquireCamera::GetImageBuffer(unsigned channel)
{
	if (channel > imgs.size() - 1)
		return nullptr;

	return imgs[channel].GetPixels();
}

unsigned AcquireCamera::GetNumberOfComponents() const
{
	return 1;
}

unsigned AcquireCamera::GetNumberOfChannels() const
{
	return (unsigned)imgs.size();
}

int AcquireCamera::GetChannelName(unsigned channel, char* name)
{
	if (channel > imgs.size()-1)
		return DEVICE_NONEXISTENT_CHANNEL;

	string chName = (channel == 0 ? "Camera-1" : "Camera-2");
	CDeviceUtils::CopyLimitedString(name, chName.c_str());
	return DEVICE_OK;
}

unsigned AcquireCamera::GetImageWidth() const
{
	return imgs[0].Width();
}

unsigned AcquireCamera::GetImageHeight() const
{
	return imgs[0].Height();
}

unsigned AcquireCamera::GetImageBytesPerPixel() const
{
	return imgs[0].Depth();
}

int AcquireCamera::SnapImage()
{
	CpxProperties props = {};
	getCpxProperties(props);
	auto dm = cpx_device_manager(cpx);

	// make sure we are acquiring only one frame
	props.video[0].max_frame_count = 1;
	if (isDual())
	{
		props.video[1].max_frame_count = 1;
	}

	int ret = cpx_configure(cpx, &props);
	if (ret != CpxStatus_Ok)
	{
		LogMessage("cpx_configure failed");
		return ERR_CPX_CONFIURE_FAILED;
	}

	// start single frame
	ret = cpx_start(cpx);
	if (ret != CpxStatus_Ok)
		throw std::exception("cpx_start failed");

	ret = readSnapImageFrames();
	cpx_stop(cpx);
	
	if (ret != DEVICE_OK)
		return ret;

	return DEVICE_OK;
}

int AcquireCamera::StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow)
{
	if (IsCapturing())
		return DEVICE_CAMERA_BUSY_ACQUIRING;
	
	int ret = GetCoreCallback()->PrepareForAcq(this);
	if (ret != DEVICE_OK)
		return ret;


	CpxProperties props = {};
	getCpxProperties(props);
	auto dm = cpx_device_manager(cpx);

	props.video[0].max_frame_count = numImages == 0 ? MAXUINT64 : numImages;
	if (isDual())
	{
		props.video[1].max_frame_count = numImages == 0 ? MAXUINT64 : numImages;
	}

	ret = cpx_configure(cpx, &props);
	if (ret != CpxStatus_Ok)
	{
		LogMessage("cpx_configure failed");
		return ERR_CPX_CONFIURE_FAILED;
	}

	ret = cpx_start(cpx);
	if (ret != CpxStatus_Ok)
		return ret;

	this->stopOnOverflow = stopOnOverflow;
	liveThread->Start(numImages, interval_ms);
	return DEVICE_OK;
}

int AcquireCamera::StopSequenceAcquisition()
{
	liveThread->Stop();
	liveThread->wait();
	int ret = cpx_abort(cpx);
	if (ret != CpxStatus_Ok)
		return ret;

	return DEVICE_OK;
}

bool AcquireCamera::IsCapturing()
{
	return liveThread->IsActive();
}

int AcquireCamera::getCpxProperties(CpxProperties& props) const
{
	props = {};
	return cpx_get_configuration(cpx, &props);
}

int AcquireCamera::setCpxProperties(CpxProperties& props)
{
	return cpx_configure(cpx, &props);
}

// Send message to micro-manager log
void AcquireCamera::reporter(int is_error, const char* file, int line, const char* function, const char* msg)
{
	const int maxLength(6000);
	char buffer[maxLength];
	snprintf(buffer, maxLength,
		"%s%s(%d) - %s: %s",
		is_error ? "ERROR " : "",
		file,
		line,
		function,
		msg);
	if (g_instance)
	{
		g_instance->LogMessage(buffer);
	}
}

// read one frame from each camera and place it in the image buffer
// for use with snapImage()
int AcquireCamera::readSnapImageFrames()
{
	VideoFrame* beg, * end;
	// read first frame and place it in the first image buffer
	cpx_map_read(cpx, 0, &beg, &end);
	int retries = 0;
	const int maxRetries = 1000;
	while (beg == end && retries < maxRetries)
	{
		retries++;
		cpx_map_read(cpx, 0, &beg, &end);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (retries >= maxRetries)
		return ERR_TIMEOUT;

	memcpy(imgs[0].GetPixelsRW(), beg->data, beg->bytes_of_frame - sizeof(VideoFrame));
	uint32_t n = (uint32_t)ConsumedBytes(beg, end);
	cpx_unmap_read(cpx, 0, n);

	// read second frame
	if (imgs.size() > 1) {
		cpx_map_read(cpx, 1, &beg, &end);
		retries = 0;
		while (beg == end && retries < maxRetries)
		{
			retries++;
			cpx_map_read(cpx, 1, &beg, &end);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (retries >= maxRetries)
			return ERR_TIMEOUT;

		memcpy(imgs[1].GetPixelsRW(), beg->data, beg->bytes_of_frame - sizeof(VideoFrame));
		n = (uint32_t)ConsumedBytes(beg, end);
		cpx_unmap_read(cpx, 1, n);
	}

	return 0;
}


// read available number of frames from both streams
// and push to circular buffer
int AcquireCamera::readLiveFrames(int& framesRead)
{
	// grab available frames from camera 1
	framesRead = 0;
	VideoFrame* beg1, * end1;
	int retries = 0;
	cpx_map_read(cpx, 0, &beg1, &end1);
	const int maxRetries = 1000;
	while (beg1 == end1 && retries < maxRetries)
	{
		retries++;
		cpx_map_read(cpx, 0, &beg1, &end1);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (retries >= maxRetries)
		return ERR_TIMEOUT;

	size_t numFrames = ConsumedBytes(beg1, end1) / beg1->bytes_of_frame;
	uint64_t startFrameId = beg1->frame_id;

	// grab the same number of frames from camera 2
	VideoFrame* beg2(0), * end2(0);
	if (isDual())
	{
		retries = 0;
		cpx_map_read(cpx, 1, &beg2, &end2);
		while (((beg2 == end2) || ((ConsumedBytes(beg2, end2) / beg2->bytes_of_frame)) < numFrames) && retries < maxRetries)
		{
			retries++;
			cpx_map_read(cpx, 1, &beg2, &end2);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (retries >= maxRetries)
			return ERR_TIMEOUT;
	}

	auto ptr1 = beg1;
	for (size_t i = 0; i < numFrames; i++)
	{
		// check frame id
		if (ptr1->frame_id != startFrameId + i)
		{
			LogMessage("Camera1 missed frame: expected " + std::to_string(startFrameId + 1) + ", got " + std::to_string(ptr1->frame_id));
			return ERR_CPX_MISSED_FRAME;
		}

		int ret = GetCoreCallback()->InsertImage(this, ptr1->data, imgs[0].Width(), imgs[0].Height(), imgs[0].Depth());
		bool overflow = false;
		if (!stopOnOverflow && ret == DEVICE_BUFFER_OVERFLOW)
		{
			GetCoreCallback()->ClearImageBuffer(this);
			overflow = true;
		}
		else
			return ret;

		// check sequence
		ptr1 += beg1->bytes_of_frame;

		// continue with the second frame only if the first one did not overflow
		if (!overflow && isDual())
		{
			auto ptr2 = beg2;
			if (ptr2->frame_id != startFrameId + i)
			{
				LogMessage("Camera2 missed frame: expected " + std::to_string(startFrameId + 1) + ", got " + std::to_string(ptr2->frame_id));
				return ERR_CPX_MISSED_FRAME;
			}

			ret = GetCoreCallback()->InsertImage(this, ptr2->data, imgs[0].Width(), imgs[0].Height(), imgs[0].Depth());
			if (!stopOnOverflow && ret == DEVICE_BUFFER_OVERFLOW)
			{
				GetCoreCallback()->ClearImageBuffer(this);
			}
			else
				return ret;

			ptr2 += beg2->bytes_of_frame;
		}
	}
	cpx_unmap_read(cpx, 0, numFrames*beg1->bytes_of_frame);
	if (isDual() && beg2 != nullptr)
		cpx_unmap_read(cpx, 1, numFrames*beg2->bytes_of_frame);

	framesRead = (int)numFrames;

	return 0;
}


void AcquireCamera::setupBuffers(unsigned width, unsigned height, unsigned depth, bool dual)
{
	imgs.clear();
	if (dual)
	{
		imgs.resize(2); // two images
		imgs[0].Resize(width, height, depth);
		imgs[1].Resize(width, height, depth);
	}
	else
	{
		imgs.resize(1); // single image
		imgs[0].Resize(width, height, depth);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Property Handlers
//////////////////////////////////////////////////////////////////////////////////////////////

int AcquireCamera::OnHardware(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(demo ? g_prop_Hardware_Demo : g_prop_Hardware_Hamamatsu);
	}
	else if (eAct == MM::AfterSet)
	{
		string hw;
		pProp->Get(hw);
		if (hw.compare(g_prop_Hardware_Demo) == 0)
		{
			demo = true;
		}
		else
		{
			demo = false;
		}
	}

	return DEVICE_OK;
}
