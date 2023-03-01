///////////////////////////////////////////////////////////////////////////////
// FILE:          DualCamera.cpp
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

#include "DualCamera.h"
#include "cpx.h"
#include "device/device.manager.h"
#include <sstream>
#include <chrono>
#include <thread>


/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
#define SIZED(str) str, sizeof(str) - 1


using namespace std;


const char* cameraName = "DualCamera";
DualCamera* DualCamera::g_instance = nullptr;

DualCamera::DualCamera() : initialized_(false)
{
	CreateProperty(MM::g_Keyword_Name, cameraName, MM::String, true);

	// Description
	CreateProperty(MM::g_Keyword_Description, "Loads images from disk according to position of focusing stage", MM::String, true);

	// CameraName
	CreateProperty(MM::g_Keyword_CameraName, "DualCamera", MM::String, true);

	// CameraID
	CreateProperty(MM::g_Keyword_CameraID, "V1.0", MM::String, true);

}

DualCamera::~DualCamera()
{
	Shutdown();
}

int DualCamera::Initialize()
{
	if (initialized_)
		return DEVICE_OK;

	// binning
	CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false);

	std::vector<std::string> binningValues;
	binningValues.push_back("1");
	SetAllowedValues(MM::g_Keyword_Binning, binningValues);

	// test cpx loading
	g_instance = this;
	cpx = cpx_init(DualCamera::reporter);
	auto dm = cpx_device_manager(cpx);
	if (!cpx || !dm)
	{
		g_instance = nullptr;
		return ERR_CPX_INIT;
	}

	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != DEVICE_OK)
		return ret;

	// set up simulated cameras
	ret = device_manager_select(dm, DeviceKind_Camera, SIZED("simulated.*random.*"), &props.video[0].camera.identifier);
	if (ret != DEVICE_OK)
		return ret;

	ret = device_manager_select(dm, DeviceKind_Camera, SIZED("simulated.*sin.*"), &props.video[1].camera.identifier);
	if (ret != DEVICE_OK)
		return ret;


	// we are assuming that cameras are identical
	CreateProperty("LineIntervalUs", to_string(props.video[0].camera.settings.line_interval_us).c_str(), MM::Float, false);

	auto width = props.video[0].camera.settings.shape.x;
	auto height = props.video[0].camera.settings.shape.y;
	imgs.resize(2); // two 8-bit images
	imgs[0].Resize(width, height, 1);
	imgs[1].Resize(width, height, 1);

	initialized_ = true;
	return DEVICE_OK;
}

int DualCamera::Shutdown()
{
	if (cpx)
	{
		cpx_stop(cpx);
		cpx_shutdown(cpx);
		g_instance = nullptr;
	}

	cpx = nullptr;

	initialized_ = false;

	return DEVICE_OK;
}

void DualCamera::GetName(char * name) const
{
	CDeviceUtils::CopyLimitedString(name, cameraName);
}

long DualCamera::GetImageBufferSize() const
{
	return imgs[0].Width() * imgs[0].Height() * imgs[0].Depth();
}

unsigned DualCamera::GetBitDepth() const
{
	return 8;
}

int DualCamera::GetBinning() const
{
	return 1;
}

int DualCamera::SetBinning(int)
{
	return DEVICE_OK;
}

void DualCamera::SetExposure(double exposure)
{
	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);
	props.video[0].camera.settings.exposure_time_us = (float)(exposure * 1000);
	props.video[1].camera.settings.exposure_time_us = props.video[0].camera.settings.exposure_time_us;
	ret = setCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error setting exposure: code=" + ret);

}

double DualCamera::GetExposure() const
{
	CpxProperties props = {};
	int ret = getCpxProperties(props);
	if (ret != DEVICE_OK)
		LogMessage("Error obtaining properties: code=" + ret);
	return props.video[0].camera.settings.exposure_time_us / 1000.0;
}

int DualCamera::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{
	return DEVICE_OK;
}

int DualCamera::GetROI(unsigned & x, unsigned & y, unsigned & xSize, unsigned & ySize)
{

	return DEVICE_OK;
}

int DualCamera::ClearROI()
{
	return DEVICE_OK;
}

int DualCamera::IsExposureSequenceable(bool & isSequenceable) const
{
	isSequenceable = false;

	return DEVICE_OK;
}

const unsigned char * DualCamera::GetImageBuffer()
{
	return imgs[0].GetPixels();
}

const unsigned char* DualCamera::GetImageBuffer(unsigned channel)
{
	if (channel > imgs.size() - 1)
		return nullptr;

	return imgs[channel].GetPixels();
}

unsigned DualCamera::GetNumberOfComponents() const
{
	return 1;
}

unsigned DualCamera::GetNumberOfChannels() const
{
	return (unsigned)imgs.size();
}

int DualCamera::GetChannelName(unsigned channel, char* name)
{
	if (channel > imgs.size()-1)
		return DEVICE_NONEXISTENT_CHANNEL;

	string chName = (channel == 0 ? "Camera-1" : "Camera-2");
	CDeviceUtils::CopyLimitedString(name, chName.c_str());
	return DEVICE_OK;
}

unsigned DualCamera::GetImageWidth() const
{
	return imgs[0].Width();
}

unsigned DualCamera::GetImageHeight() const
{
	return imgs[0].Height();
}

unsigned DualCamera::GetImageBytesPerPixel() const
{
	return 1;
}

int DualCamera::SnapImage()
{
	CpxProperties props = {};
	getCpxProperties(props);
	auto dm = cpx_device_manager(cpx);

	device_manager_select(dm,
		DeviceKind_Camera,
		SIZED("simulated.*random.*"),
		&props.video[0].camera.identifier);

	device_manager_select(dm,
		DeviceKind_Camera,
		SIZED("simulated.*sin.*"),
		&props.video[1].camera.identifier);

	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[0].storage.identifier);

	device_manager_select(dm,
		DeviceKind_Storage,
		SIZED("Trash"),
		&props.video[1].storage.identifier);

	cpx_configure(cpx, &props);

	props.video[0].camera.settings.binning = 1;
	props.video[0].camera.settings.pixel_type = SampleType_u8;
	props.video[0].camera.settings.shape = {64, 48};
	props.video[0].max_frame_count = 1;

	props.video[1].camera.settings.binning = 1;
	props.video[1].camera.settings.pixel_type = SampleType_u8;
	props.video[1].camera.settings.shape = {64, 48};
	props.video[1].max_frame_count = 1;

	int ret = cpx_configure(cpx, &props);
	if (ret != CpxStatus_Ok)
	{
		LogMessage("cpx_configure failed");
		return ERR_CPX_CONFIURE_FAILED;
	}

	imgs[0].Resize(props.video[0].camera.settings.shape.x, props.video[0].camera.settings.shape.y, 1);
	imgs[1].Resize(imgs[0].Width(), imgs[0].Height(), imgs[0].Depth());

	// start single frame
	ret = cpx_start(cpx);
	if (ret != CpxStatus_Ok)
		throw std::exception("cpx_start failed");

	readFrame(0, props);
	readFrame(1, props);

	cpx_stop(cpx);

	return DEVICE_OK;
}

int DualCamera::StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow)
{
	return CCameraBase::StartSequenceAcquisition(numImages, interval_ms, stopOnOverflow);
}

int DualCamera::StopSequenceAcquisition()
{
	return CCameraBase::StopSequenceAcquisition();
}

int DualCamera::getCpxProperties(CpxProperties& props) const
{
	props = {};
	return cpx_get_configuration(cpx, &props);
}

int DualCamera::setCpxProperties(CpxProperties& props)
{
	return cpx_configure(cpx, &props);
}

// Send message to micro-manager log
void DualCamera::reporter(int is_error, const char* file, int line, const char* function, const char* msg)
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

int DualCamera::readFrame(int stream, CpxProperties& props)
{
	const auto next = [](VideoFrame* cur) -> VideoFrame* {
		return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
	};

	const auto consumed_bytes = [](const VideoFrame* const cur,
		const VideoFrame* const end) -> size_t {
			return (uint8_t*)end - (uint8_t*)cur;
	};

	// resize buffer
	const int pixelSizeBytes = 1; // TODO: this has to be variable depending on the pixel type
	const int frameSize = props.video[0].camera.settings.shape.x * props.video[0].camera.settings.shape.y * pixelSizeBytes;

	VideoFrame* beg, * end;
	cpx_map_read(cpx, stream, &beg, &end);
	while (beg == end)
	{
		cpx_map_read(cpx, stream, &beg, &end);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		// TODO: timeout
	}
	assert(beg->shape.dims.width == props.video[0].camera.settings.shape.x);
	assert(beg->shape.dims.height == props.video[0].camera.settings.shape.y);
	memcpy(imgs[stream].GetPixelsRW(), beg->data, beg->bytes_of_frame - sizeof(VideoFrame));
	uint32_t n = (uint32_t) consumed_bytes(beg, end);
	cpx_unmap_read(cpx, stream, n);

	return 0;
}
