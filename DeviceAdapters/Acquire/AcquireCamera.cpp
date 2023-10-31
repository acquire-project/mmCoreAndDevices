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
#include "acquire.h"
#include "device/hal/device.manager.h"
#include <sstream>
#include <chrono>
#include <thread>
#include "SequenceThread.h"
#include "boost/filesystem.hpp"


/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
#define SIZED(str) str, sizeof(str) - 1

using namespace std;

const char* cameraName = "AcquireCamera";
AcquireCamera* AcquireCamera::g_instance = nullptr;
const int DEMO_IMAGE_WIDTH = 320;
const int DEMO_IMAGE_HEIGHT = 240;
const int DEMO_IMAGE_DEPTH = 1;

const bool MULTI_CHANNEL = true;
// const std::string outStreamId("Zarr");
// const std::string outStreamId("tiff");
std::vector<std::string> streamFormats = { "Zarr", "tiff" };

const VideoFrame* next(VideoFrame* cur)
{
	return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
}

size_t ConsumedBytes (const VideoFrame* const cur, const VideoFrame* const end)
{
		return (uint8_t*)end - (uint8_t*)cur;
};

AcquireCamera::AcquireCamera() :
	initialized_(false),
	demo(true),
	saveToZarr(false),
	softwareTriggerId(-1),
	stopOnOverflow(false),
	currentCamera(0),
	multiChannel(MULTI_CHANNEL),
	liveThread(nullptr),
	streamId(streamFormats[0])
{

	// set error messages
	SetErrorText(ERR_SOFTWARE_TRIGGER_NOT_AVAILABLE, "Software trigger not available");
	SetErrorText(ERR_FAILED_CREATING_ACQ_DIR, "Failed to create acquisition directory");


	// instantiate cpx
	g_instance = this;
	runtime = acquire_init(AcquireCamera::reporter);
	auto dm = acquire_device_manager(runtime);
	if (!runtime || !dm)
	{
		g_instance = nullptr;
		LogMessage("CPX initialize failed");
		return;
	}

	vector<string> devices;
	devices.push_back(g_Camera_None);
	for (uint32_t i = 0; i < device_manager_count(dm); ++i) {
		struct DeviceIdentifier identifier = {};
		int ret = device_manager_get(&identifier, dm, i);
		if (ret != AcquireStatus_Ok) {
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

	if (isDual())
		multiChannel = true;
	else
		multiChannel = false;

	// cameras
	char val[MM::MaxStrLength];
	GetProperty(g_prop_Camera_1, val);
	camera1 = val;

	GetProperty(g_prop_Camera_2, val);
	camera2 = val;

	CPropertyAction* pAct = new CPropertyAction(this, &AcquireCamera::OnDevice);
	CreateProperty(g_prop_CurrentDevice, camera1.c_str(), MM::String, false, pAct);
	AddAllowedValue(g_prop_CurrentDevice, camera1.c_str(), 0);
	if (!isDual())
	{
		AddAllowedValue(g_prop_CurrentDevice, camera2.c_str(), 0);
	}

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

	// stream format
	pAct = new CPropertyAction(this, &AcquireCamera::OnStreamFormat);
	CreateProperty(g_prop_StreamFormat, streamId.c_str(), MM::String, false, pAct);
	SetAllowedValues(g_prop_StreamFormat, streamFormats);

	// test cpx loading
	g_instance = this;
	runtime = acquire_init(AcquireCamera::reporter);
	auto dm = acquire_device_manager(runtime);
	if (!runtime || !dm)
	{
		g_instance = nullptr;
		return ERR_ACQ_INIT;
	}

	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	ret = device_manager_select(dm, DeviceKind_Camera, camera1.c_str(), camera1.size(), &props.video[0].camera.identifier);
	if (ret != AcquireStatus_Ok)
		return ret;
	
	if (isDual())
	{
		ret = device_manager_select(dm, DeviceKind_Camera, camera2.c_str(), camera2.size(), &props.video[1].camera.identifier);
		if (ret != AcquireStatus_Ok)
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

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	// get camera properties again because we might have changed something during configure
	props = {};
	ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	// get metatadata
	AcquirePropertyMetadata meta = {};
	ret = acquire_get_configuration_metadata(runtime, &meta);
	if (ret != AcquireStatus_Ok)
		return ret;

	// software trigger
	softwareTriggerId = getSoftwareTrigger(meta, 0);
	if (isDual())
	{
		// second trigger has to be the same as first
		int trigId = getSoftwareTrigger(meta, 1);
		if (trigId != softwareTriggerId)
			return ERR_SOFTWARE_TRIGGER_NOT_AVAILABLE;
	}

	// we will support only cameras with software trigger capabilities
	if (softwareTriggerId == -1)
		return ERR_SOFTWARE_TRIGGER_NOT_AVAILABLE;
	
	props.video[0].camera.settings.input_triggers.frame_start.enable = 1;
	props.video[0].camera.settings.input_triggers.frame_start.line = softwareTriggerId;
	
	props.video[1].camera.settings.input_triggers.frame_start.enable = 1;
	props.video[1].camera.settings.input_triggers.frame_start.line = softwareTriggerId;

	props.video[0].camera.settings.binning = 1;
	if (demo)
		// for demo camera use hardcoded values
		props.video[0].camera.settings.shape = { DEMO_IMAGE_WIDTH, DEMO_IMAGE_HEIGHT };
	else
		// for actual cameras use the values from the metadata
		props.video[0].camera.settings.shape = { (unsigned)meta.video[0].camera.shape.x.high, (unsigned)meta.video[0].camera.shape.y.high };
	
	props.video[0].camera.settings.offset = { 0, 0 };
	props.video[0].max_frame_count = 1;
	props.video[0].camera.settings.exposure_time_us = 20000;
	props.video[0].camera.settings.binning = 1;

	props.video[1].camera.settings.shape = props.video[0].camera.settings.shape;
	props.video[1].camera.settings.offset = props.video[0].camera.settings.offset;
	props.video[1].max_frame_count = props.video[0].max_frame_count;
	props.video[1].camera.settings.binning = props.video[0].camera.settings.binning;
	props.video[1].camera.settings.exposure_time_us = props.video[0].camera.settings.exposure_time_us;

	if (demo)
	{
		fullFrame.xSize = DEMO_IMAGE_WIDTH;
      fullFrame.ySize = DEMO_IMAGE_HEIGHT;
	}
	else {
      fullFrame.xSize = (int)meta.video[0].camera.shape.x.high;
      fullFrame.ySize = (int)meta.video[0].camera.shape.y.high;
   }

	props.video[0].max_frame_count = MAXUINT64;
	props.video[1].max_frame_count = MAXUINT64;

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	// get camera properties again because we might have changed something during configure
	props = {};
	ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	VideoFrame* beg, * end;
	acquire_map_read(runtime, 0, &beg, &end);
	acquire_map_read(runtime, 1, &beg, &end);

	// START acquiring
	ret = acquire_start(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	// binning
	pAct = new CPropertyAction(this, &AcquireCamera::OnBinning);
	ret = CreateIntegerProperty(MM::g_Keyword_Binning, 1, false, pAct);
	if (ret != DEVICE_OK)
		return ret;

	vector<string> binValues;
	binValues.push_back("1");
	binValues.push_back("2");
	binValues.push_back("4");
	SetAllowedValues(MM::g_Keyword_Binning, binValues);

	// pixel type
	pAct = new CPropertyAction(this, &AcquireCamera::OnPixelType);
	ret = CreateStringProperty(MM::g_Keyword_PixelType, g_PixelType_8bit, false, pAct);
	if (ret != DEVICE_OK)
		return ret;

	vector<string> pixelTypeValues;
	// 
	if (meta.video[0].camera.supported_pixel_types == 0 || meta.video[0].camera.supported_pixel_types & 0x01)
      pixelTypeValues.push_back(g_PixelType_8bit);
	if (meta.video[0].camera.supported_pixel_types & 0x02)
		pixelTypeValues.push_back(g_PixelType_16bit);

	ret = SetAllowedValues(MM::g_Keyword_PixelType, pixelTypeValues);
	if (ret != DEVICE_OK)
		return ret;

	// zarr save
	pAct = new CPropertyAction(this, &AcquireCamera::OnSaveToZarr);
	ret = CreateProperty(g_prop_SaveToZarr, "0", MM::Integer, false, pAct);
	if (ret != DEVICE_OK)
		return ret;
	vector<string> zarrSaveValues = {"0", "1"};
	SetAllowedValues(g_prop_SaveToZarr, zarrSaveValues);

	// zarr save root
	pAct = new CPropertyAction(this, &AcquireCamera::OnSaveRoot);
	ret = CreateProperty(g_prop_SaveRoot, saveRoot.c_str(), MM::String, false, pAct);
	if (ret != DEVICE_OK)
		return ret;

	// zarr save prefix
	pAct = new CPropertyAction(this, &AcquireCamera::OnSavePrefix);
	ret = CreateProperty(g_prop_SavePrefix, savePrefix.c_str(), MM::String, false, pAct);
	if (ret != DEVICE_OK)
		return ret;

	// metadata 
	pAct = new CPropertyAction(this, &AcquireCamera::OnMetadata);
	ret = CreateProperty(g_prop_SetMetadata, zarrMetadata.c_str(), MM::String, false, pAct);
	if (ret != DEVICE_OK)
		return ret;


	setupBuffers(props.video[0].camera.settings.shape.x, props.video[0].camera.settings.shape.y, props.video[0].camera.settings.pixel_type + 1, isDual());

	initialized_ = true;
	return DEVICE_OK;
}

int AcquireCamera::Shutdown()
{
	liveThread->Stop();
	liveThread->wait();

	if (runtime)
	{
		auto ret = acquire_shutdown(runtime);
		if (ret != AcquireStatus_Ok)
			LogMessage("acquire_shutdown error: " + ret);
		runtime = nullptr;
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
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);

	auto dm = acquire_device_manager(runtime);

	ret = device_manager_select(dm, DeviceKind_Camera, camera1.c_str(), camera1.size(), &props.video[0].camera.identifier);
	if (ret != AcquireStatus_Ok)
	{
		LogMessage("CPX Select 1 failed");
	}

	if (isDual())
	{
		ret = device_manager_select(dm, DeviceKind_Camera, camera2.c_str(), camera2.size(), &props.video[1].camera.identifier);
		if (ret != AcquireStatus_Ok)
			LogMessage("CPX Select 2 failed");
	}

	props.video[0].camera.settings.exposure_time_us = (float)(exposure * 1000);
	if (isDual())
		props.video[1].camera.settings.exposure_time_us = props.video[0].camera.settings.exposure_time_us;

	ret = setAcquireProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error setting exposure: code=" + ret);

}

double AcquireCamera::GetExposure() const
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);
	return props.video[0].camera.settings.exposure_time_us / 1000.0;
}

int AcquireCamera::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	for (int i = 0; i < imgs.size(); i++)
	{
		props.video[i].camera.settings.shape.x = x;
		props.video[i].camera.settings.shape.y = y;
		props.video[i].camera.settings.offset.x = xSize;
		props.video[i].camera.settings.offset.y = ySize;
	}

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	return setupBuffers();
}

int AcquireCamera::GetROI(unsigned & x, unsigned & y, unsigned & xSize, unsigned & ySize)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);

	xSize = props.video[0].camera.settings.shape.x;
	ySize = props.video[0].camera.settings.shape.y;
	x = props.video[0].camera.settings.offset.x;
   y = props.video[0].camera.settings.offset.y;
	if (ret != AcquireStatus_Ok)
		return ret;

	return DEVICE_OK;
}

int AcquireCamera::ClearROI()
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	for (int i = 0; i < imgs.size(); i++)
	{
		props.video[i].camera.settings.shape.x = fullFrame.xSize;
		props.video[i].camera.settings.shape.y = fullFrame.ySize;
		props.video[i].camera.settings.offset.x = fullFrame.x;
		props.video[i].camera.settings.offset.y = fullFrame.y;
	}

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	return setupBuffers();
}

int AcquireCamera::IsExposureSequenceable(bool & isSequenceable) const
{
	isSequenceable = false;

	return DEVICE_OK;
}

const unsigned char * AcquireCamera::GetImageBuffer()
{
	return imgs[currentCamera].GetPixels();
}

const unsigned char* AcquireCamera::GetImageBuffer(unsigned channel)
{
	if (channel > imgs.size() - 1)
		return nullptr;
	if (multiChannel)
		return imgs[channel].GetPixels();
	else
		return imgs[currentCamera].GetPixels();
}

unsigned AcquireCamera::GetNumberOfComponents() const
{
	return 1;
}

unsigned AcquireCamera::GetNumberOfChannels() const
{
	if (multiChannel)
		return (unsigned) imgs.size();
	else
		return 1;
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
	AcquireStatusCode aret = acquire_execute_trigger(runtime, 0);
	if (aret != AcquireStatusCode::AcquireStatus_Ok)
		return aret;

	aret = acquire_execute_trigger(runtime, 1);
	if (aret != AcquireStatusCode::AcquireStatus_Ok)
		return aret;

	int ret = readSnapImageFrames();
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

	ret = acquire_abort(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	// switch to hardware trigger
	AcquireProperties props = {};
	getAcquireProperties(props);

	props.video[0].max_frame_count = numImages == 0 ? MAXUINT64 : numImages;
	props.video[1].max_frame_count = numImages == 0 ? MAXUINT64 : numImages;

	props.video[0].camera.settings.input_triggers.frame_start.enable = 0;
	props.video[1].camera.settings.input_triggers.frame_start.enable = 0;

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ERR_ACQ_CONFIURE_FAILED;

	ret = acquire_start(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	LogMessage("Started sequence acquisition.");

	this->stopOnOverflow = stopOnOverflow;
	liveThread->Start(numImages, interval_ms);
	return DEVICE_OK;
}

int AcquireCamera::StopSequenceAcquisition()
{
	LogMessage("Stopped sequence acquisition.");

	liveThread->Stop();
	liveThread->wait();

	int ret = acquire_abort(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	// switch back to s/w trigger
	AcquireProperties props = {};
	getAcquireProperties(props);

	props.video[0].max_frame_count = MAXUINT64;
	props.video[1].max_frame_count = MAXUINT64;

	props.video[0].camera.settings.input_triggers.frame_start.enable = 1;
	props.video[0].camera.settings.input_triggers.frame_start.line = softwareTriggerId;

	props.video[1].camera.settings.input_triggers.frame_start.enable = 1;
	props.video[1].camera.settings.input_triggers.frame_start.line = softwareTriggerId;

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ERR_ACQ_CONFIURE_FAILED;

	ret = acquire_start(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	LogMessage("Ended sequence acquisition.");


	return DEVICE_OK;
}

bool AcquireCamera::IsCapturing()
{
	return liveThread->IsActive();
}

int AcquireCamera::getAcquireProperties(AcquireProperties& props) const
{
	props = {};
	return acquire_get_configuration(runtime, &props);
}

int AcquireCamera::setAcquireProperties(AcquireProperties& props)
{
	return acquire_configure(runtime, &props);
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
	auto scode = acquire_map_read(runtime, 0, &beg, &end);
	if (scode != AcquireStatus_Ok)
		return scode;
	// TODO: check other instances

	int retries = 0;
	const int maxRetries = 1000;
	while (beg == end && retries < maxRetries)
	{
		retries++;
		acquire_map_read(runtime, 0, &beg, &end);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (retries >= maxRetries)
		return ERR_TIMEOUT;

	memcpy(imgs[0].GetPixelsRW(), beg->data, beg->bytes_of_frame - sizeof(VideoFrame));
	uint32_t n = (uint32_t)ConsumedBytes(beg, end);
	acquire_unmap_read(runtime, 0, n);

	// read second frame
	if (imgs.size() > 1) {
		acquire_map_read(runtime, 1, &beg, &end);
		retries = 0;
		while (beg == end && retries < maxRetries)
		{
			retries++;
			acquire_map_read(runtime, 1, &beg, &end);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (retries >= maxRetries)
			return ERR_TIMEOUT;

		memcpy(imgs[1].GetPixelsRW(), beg->data, beg->bytes_of_frame - sizeof(VideoFrame));
		n = (uint32_t)ConsumedBytes(beg, end);
		acquire_unmap_read(runtime, 1, n);
	}

	return 0;
}

/**
 * @brief Read available number of frames from both streams and push to circular buffer
 * This function is intended to called during image streaming
 * @param framesRead Number of frames read
 * @return Error code
 */
int AcquireCamera::readLiveFrames(int& framesRead)
{
	// check how many available frames from camera 1
	framesRead = 0;
	VideoFrame* beg1, * end1;
	int retries = 0;
	acquire_map_read(runtime, 0, &beg1, &end1);
	const int maxRetries = 1000;
	while (beg1 == end1 && retries < maxRetries)
	{
		retries++;
		acquire_map_read(runtime, 0, &beg1, &end1);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (retries >= maxRetries)
		return ERR_TIMEOUT;

	size_t numFrames1 = ConsumedBytes(beg1, end1) / beg1->bytes_of_frame;
	size_t numFrames2 = 0;
	uint64_t startFrameId = beg1->frame_id;

	// check frames from camera 2
	VideoFrame* beg2(0), * end2(0);
	if (isDual())
	{
		retries = 0;
		acquire_map_read(runtime, 1, &beg2, &end2);
		while ((beg2 == end2) && retries < maxRetries)
		{
			retries++;
			acquire_map_read(runtime, 1, &beg2, &end2);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (retries >= maxRetries)
			return ERR_TIMEOUT;
		numFrames2 = ConsumedBytes(beg2, end2) / beg2->bytes_of_frame;
	}

	size_t numFrames = isDual() ? min(numFrames1, numFrames2) : numFrames1;
	auto ptr1 = beg1;
	auto ptr2 = beg2;

	// insert frames into circular buffer
	for (size_t i = 0; i < numFrames; i++)
	{
		// check frame id
		if (ptr1->frame_id != startFrameId + i)
		{
			LogMessage(">>>> Camera1 missed frame: expected " + std::to_string(startFrameId + 1) + ", got " + std::to_string(ptr1->frame_id));
			//return ERR_CPX_MISSED_FRAME;
		}

		memcpy(imgs[0].GetPixelsRW(), ptr1->data, imgs[0].Width() * imgs[0].Height() * imgs[0].Depth());
		Metadata md;
		md.PutImageTag("CpxFrameId", ptr1->frame_id);
		md.PutImageTag("CpxTimeStamp", ptr1->timestamps.hardware);
		auto currentFrameId = ptr1->frame_id;


		if (isDual())
		{
			if (ptr2->frame_id != startFrameId + i)
			{
				LogMessage("Camera2 missed frame: expected " + std::to_string(startFrameId + 1) + ", got " + std::to_string(ptr2->frame_id));
				// return ERR_CPX_MISSED_FRAME;
			}
			memcpy(imgs[1].GetPixelsRW(), ptr2->data, imgs[1].Width() * imgs[1].Height() * imgs[1].Depth());

		}

		if (multiChannel)
		{
			for (int bufNum = 0; bufNum < imgs.size(); bufNum++) {
				int ret = GetCoreCallback()->InsertImage(this, imgs[bufNum].GetPixels(), imgs[bufNum].Width(), imgs[bufNum].Height(), imgs[bufNum].Depth(), 1, md.Serialize().c_str());
				LogMessage(">>> Camera " + std::to_string(bufNum + 1) + " frame " + std::to_string(currentFrameId) + " inserted");
				if (!stopOnOverflow && ret == DEVICE_BUFFER_OVERFLOW)
				{
					GetCoreCallback()->ClearImageBuffer(this);
					LogMessage("Camera buffer overflow " + std::to_string(bufNum + 1) + " frame " + std::to_string(currentFrameId));
					break;
				}
			}
		}
		else
		{
			int ret = GetCoreCallback()->InsertImage(this, imgs[currentCamera].GetPixels(), imgs[currentCamera].Width(), imgs[currentCamera].Height(), imgs[currentCamera].Depth(), 1, md.Serialize().c_str());
			LogMessage(">>> Camera " + std::to_string(currentCamera + 1) + " frame " + std::to_string(currentFrameId) + " inserted");
			if (!stopOnOverflow && ret == DEVICE_BUFFER_OVERFLOW)
			{
				GetCoreCallback()->ClearImageBuffer(this);
				GetCoreCallback()->InsertImage(this, imgs[currentCamera].GetPixels(), imgs[currentCamera].Width(), imgs[currentCamera].Height(), imgs[currentCamera].Depth(), 1, md.Serialize().c_str());
				LogMessage("Camera buffer overflow " + std::to_string(currentCamera + 1) + " frame " + std::to_string(currentFrameId));
			}
		}
		// advance to the next frame
		ptr1 += beg1->bytes_of_frame;
		if (isDual())
         ptr2 += beg2->bytes_of_frame;
	}
	acquire_unmap_read(runtime, 0, numFrames * beg1->bytes_of_frame);
	if (isDual() && beg2 != nullptr)
		acquire_unmap_read(runtime, 1, numFrames * beg2->bytes_of_frame);

	framesRead = (int)numFrames;

	return 0;
}

/**
 * @brief AcquireCamera::setupBuffers - setup image buffers for the micromanager adapter, buffer size and depth determine the image size
 * @param width image width in pixels
 * @param height image height in pixels
 * @param depth pixel depth in bytes
 * @param dual true if dual camera mode, false if single camera mode
 */
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

int AcquireCamera::abortCpx()
{
	return acquire_abort(runtime);
}

void AcquireCamera::generateSyntheticImage(int channel, uint8_t value)
{
	memset(imgs[channel].GetPixelsRW(), value, imgs[0].Width() * imgs[0].Height() * imgs[0].Depth());
	LogMessage(">>> Synthetic image generated in channel " + std::to_string(channel) + ", level: " + std::to_string(value));
}

int AcquireCamera::setPixelType(const char* pixType)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	int depth = 0;

	if (strcmp(pixType, g_PixelType_8bit) == 0)
	{
		props.video[0].camera.settings.pixel_type = SampleType_u8;
		props.video[1].camera.settings.pixel_type = SampleType_u8;
		depth = 1;
   }
	else if (strcmp(pixType, g_PixelType_16bit) == 0)
	{
		props.video[0].camera.settings.pixel_type = SampleType_u16;
		props.video[1].camera.settings.pixel_type = SampleType_u16;
		depth = 2;
	}
	else
	{
      return ERR_UNKNOWN_PIXEL_TYPE;
   }
	// apply new settings
	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

   return setupBuffers();
}

int AcquireCamera::getPixelType(std::string& pixType)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;
	if (props.video[0].camera.settings.pixel_type == SampleType_u8)
		pixType = g_PixelType_8bit;
	else if (props.video[0].camera.settings.pixel_type == SampleType_u16)
		pixType = g_PixelType_16bit;
	else
		return ERR_UNKNOWN_PIXEL_TYPE;
	return DEVICE_OK;
}

int AcquireCamera::setBinning(int bin)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	for (int i = 0; i < imgs.size(); i++)
	{
		// reset the ROI to full frame to avoid confusion
		props.video[i].camera.settings.offset.x = (uint8_t)fullFrame.x;
		props.video[i].camera.settings.offset.y = (uint8_t)fullFrame.y;
		props.video[i].camera.settings.shape.x = (uint32_t)fullFrame.xSize;
		props.video[i].camera.settings.shape.y = (uint32_t)fullFrame.ySize;
	}

	// apply frame
	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	ret = setupBuffers();
	if (ret != DEVICE_OK)
		return ret;

	ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	// now do the binning
	for (int i = 0; i < imgs.size(); i++)
	{
		props.video[i].camera.settings.binning = (uint8_t)bin;
		props.video[i].camera.settings.shape.x = (uint32_t)(fullFrame.xSize / bin);
		props.video[i].camera.settings.shape.y = (uint32_t)(fullFrame.ySize / bin);
	}
	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	return setupBuffers();
}

int AcquireCamera::getBinning(int& bin)
{
	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;
	bin = props.video[0].camera.settings.binning;
	return DEVICE_OK;
}

// Setup buffers based on the current state of the camera
int AcquireCamera::setupBuffers()
{
	AcquireProperties props = {};

	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	setupBuffers(props.video[0].camera.settings.shape.x, props.video[0].camera.settings.shape.y, props.video[0].camera.settings.pixel_type + 1, isDual());
	return DEVICE_OK;
}

int AcquireCamera::enterZarrSave()
{
	if (IsCapturing())
		return DEVICE_CAMERA_BUSY_ACQUIRING;

	// stop current acquisition
	acquire_abort(runtime);

	// create file name
	auto savePrefixTmp(savePrefix);
	string dirName = saveRoot + "/" + savePrefixTmp;
	int counter(1);
	while (boost::filesystem::exists(dirName))
	{
		savePrefixTmp = savePrefix + "_" + to_string(counter++);
		dirName = saveRoot + "/" + savePrefixTmp;
	}
	currentDirName = dirName;
	boost::system::error_code errCode;
	if (!boost::filesystem::create_directory(currentDirName, errCode))
		return ERR_FAILED_CREATING_ACQ_DIR;

	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	auto dm = acquire_device_manager(runtime);
	if (!runtime || !dm)
	{
		g_instance = nullptr;
		return ERR_ACQ_INIT;
	}
	
	device_manager_select(dm,
		DeviceKind_Storage,
		streamId.c_str(), streamId.size(),
		&props.video[0].storage.identifier);

	device_manager_select(dm,
		DeviceKind_Storage,
		streamId.c_str(), streamId.size(),
		&props.video[1].storage.identifier);

	setFileName(props, 0, currentDirName + "/" + "stream1." + streamId);
	setFileName(props, 1, currentDirName + "/" + "stream2." + streamId);

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	ret = acquire_start(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	return DEVICE_OK;
}

int AcquireCamera::exitZarrSave()
{
	if (IsCapturing())
		return DEVICE_CAMERA_BUSY_ACQUIRING;

	// stop current acquisition
	acquire_abort(runtime);

	AcquireProperties props = {};
	int ret = getAcquireProperties(props);
	if (ret != AcquireStatus_Ok)
		return ret;

	auto dm = acquire_device_manager(runtime);
	if (!runtime || !dm)
	{
		g_instance = nullptr;
		return ERR_ACQ_INIT;
	}

	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[0].storage.identifier);

	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[1].storage.identifier);

	ret = acquire_configure(runtime, &props);
	if (ret != AcquireStatus_Ok)
		return ret;

	ret = acquire_start(runtime);
	if (ret != AcquireStatus_Ok)
		return ret;

	return DEVICE_OK;
}

int AcquireCamera::getSoftwareTrigger(AcquirePropertyMetadata& meta, int stream)
{
	int line = -1;
	for (int i = 0; i < meta.video[stream].camera.digital_lines.line_count; ++i)
	{
		if (strcmp(meta.video[stream].camera.digital_lines.names[i], "software") == 0) {
			line = i;
			break;
		}
	}

	return line;
}

/**
 * Assigns file name for the output stream 
 */
void AcquireCamera::setFileName(AcquireProperties& props, int stream, const std::string& fileName)
{
	// Metadata is null because we don't have access to Summary metdata from the acq engine
	// that includes pixel size
	storage_properties_init(&props.video[stream].storage.settings, 0, fileName.c_str(), fileName.size() + 1, nullptr, 0, { 1.0, 1.0 });
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Property Handlers
//////////////////////////////////////////////////////////////////////////////////////////////

int AcquireCamera::OnDevice(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(currentCamera == 0 ? camera1.c_str() : camera2.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		string dev;
		pProp->Get(dev);
		if (dev.compare(camera1) == 0)
		{
			currentCamera=0;
		}
		else
		{
			currentCamera=1;
		}
	}

	return DEVICE_OK;
}

int AcquireCamera::OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		string pixType;
		int ret = getPixelType(pixType);
		if (ret != DEVICE_OK)
			return ret;
		pProp->Set(pixType.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		string pixType;
		pProp->Get(pixType);
		int ret = setPixelType(pixType.c_str());
		if (ret != DEVICE_OK)
			return ret;
	}
	return DEVICE_OK;
}

int AcquireCamera::OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		int bin;
		int ret = getBinning(bin);
		if (ret != DEVICE_OK)
			return ret;
		pProp->Set((long)bin);

	}
	else if (eAct == MM::AfterSet)
	{
		long bin;
		pProp->Get(bin);
		int ret = setBinning((int)bin);
	}

	return DEVICE_OK;
}

int AcquireCamera::OnSaveToZarr(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(saveToZarr ? 1L : 0L);
	}
	else if (eAct == MM::AfterSet)
	{
		long val;
		pProp->Get(val);
		if (val && !saveToZarr) {
			int ret = enterZarrSave();
			if (ret != DEVICE_OK)
				return ret;
			saveToZarr = true;
		}
		else if (!val && saveToZarr)
		{
			int ret = exitZarrSave();
			if (ret != DEVICE_OK)
				return ret;
			saveToZarr = false;
		}

		return DEVICE_OK;
	}

	return DEVICE_OK;
}

int AcquireCamera::OnSaveRoot(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(saveRoot.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		pProp->Get(saveRoot);
	}

	return DEVICE_OK;
}

int AcquireCamera::OnSavePrefix(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(savePrefix.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		pProp->Get(savePrefix);
	}

	return DEVICE_OK;
}

int AcquireCamera::OnStreamFormat(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(streamId.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		pProp->Get(streamId);
	}

	return DEVICE_OK;
}

int AcquireCamera::OnMetadata(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		pProp->Set(zarrMetadata.c_str());
	}
	else if (eAct == MM::AfterSet)
	{
		pProp->Get(zarrMetadata);
	}

	return DEVICE_OK;
}
