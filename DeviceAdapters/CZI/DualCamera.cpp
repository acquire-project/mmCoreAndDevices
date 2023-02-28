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

const char* cameraName = "DualCamera";

DualCamera::DualCamera() : initialized_(false)
{
	CreateProperty(MM::g_Keyword_Name, cameraName, MM::String, true);

	// Description
	CreateProperty(MM::g_Keyword_Description, "Loads images from disk according to position of focusing stage", MM::String, true);

	// CameraName
	CreateProperty(MM::g_Keyword_CameraName, "DualCamera", MM::String, true);

	// CameraID
	CreateProperty(MM::g_Keyword_CameraID, "V1.0", MM::String, true);

	// binning
	CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false);

	std::vector<std::string> binningValues;
	binningValues.push_back("1");
	SetAllowedValues(MM::g_Keyword_Binning, binningValues);
}

DualCamera::~DualCamera()
{
}

int DualCamera::Initialize()
{
	if (initialized_)
		return DEVICE_OK;

	initialized_ = true;

	return DEVICE_OK;
}

int DualCamera::Shutdown()
{
	initialized_ = false;

	return DEVICE_OK;
}

void DualCamera::GetName(char * name) const
{
	CDeviceUtils::CopyLimitedString(name, cameraName);
}

long DualCamera::GetImageBufferSize() const
{
	return 0;
}

unsigned DualCamera::GetBitDepth() const
{
	return 16;
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

}

double DualCamera::GetExposure() const
{
	return 0.0;
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
	return nullptr;
}

unsigned DualCamera::GetNumberOfComponents() const
{
	return 1;
}

unsigned DualCamera::GetImageWidth() const
{
	return 0;
}

unsigned DualCamera::GetImageHeight() const
{
	return 0;
}

unsigned DualCamera::GetImageBytesPerPixel() const
{
	return 2;
}

int DualCamera::SnapImage()
{
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
