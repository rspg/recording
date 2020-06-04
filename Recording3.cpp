// Recording3.cpp : スタティック ライブラリ用の関数を定義します。
//

#include "pch.h"
#include "framework.h"

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Media::Transcoding;

using namespace ::DirectX;

IDirect3DDevice _direct3dDevice{ nullptr };
Direct3D11CaptureFramePool _captureFramePool{ nullptr };
GraphicsCaptureSession _captureSession{ nullptr };
Direct3D11CaptureFramePool::FrameArrived_revoker _frameArrived;
MediaStreamSource _mediaSource{ nullptr };
MediaTranscoder _transcorder;
IMediaStreamSource::SampleRequested_revoker _sampleRequested;
DateTime _startTime;
std::vector<MediaStreamSample> _samples;

std::mutex _captureMutex;
std::condition_variable _captureCond;

winrt::Windows::Foundation::IAsyncAction CreateCaptureItemForWindowImpl(HWND hwnd)
{
	namespace abi = winrt::Windows::Graphics::Capture;

	try
	{
		auto factory = get_activation_factory<GraphicsCaptureItem>();
		auto interop = factory.as<::IGraphicsCaptureItemInterop>();
		GraphicsCaptureItem item{ nullptr };
		check_hresult(interop->CreateForWindow(hwnd, guid_of<abi::IGraphicsCaptureItem>(), reinterpret_cast<void**>(put_abi(item))));

		const DWORD createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		com_ptr<ID3D11Device> d3dDevice;
		check_hresult(D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			d3dDevice.put(),
			nullptr,
			nullptr));
		//

		com_ptr<::IInspectable> device;
		check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(d3dDevice.as<IDXGIDevice>().get(), device.put()));

		_direct3dDevice = device.as<IDirect3DDevice>();
		_captureFramePool = Direct3D11CaptureFramePool::Create(
			_direct3dDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());

		//

		_frameArrived = _captureFramePool.FrameArrived(auto_revoke, [](
			Direct3D11CaptureFramePool const& sender,
			winrt::Windows::Foundation::IInspectable const& args)
		{
			std::lock_guard<std::mutex> lock(_captureMutex);

			auto frame = sender.TryGetNextFrame();
			auto sample = MediaStreamSample::CreateFromDirect3D11Surface(frame.Surface(), DateTime::clock::now() - _startTime);
			_samples.push_back(sample);

			/*auto giInterface = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
			void* giSurface;
			giInterface->GetInterface(guid_of<::IDXGISurface>(), &giSurface);
			void* d3dTexture;
			giInterface->GetInterface(guid_of<::ID3D11Texture2D>(), &d3dTexture);
			void* d3dBuffer;
			giInterface->GetInterface(guid_of<::ID3D11Buffer>(), &d3dBuffer);*/

			_captureCond.notify_all();
		});

		_startTime = DateTime::clock::now();
		_captureSession = _captureFramePool.CreateCaptureSession(item);
		_captureSession.StartCapture();

		//
		auto videoProperties = VideoEncodingProperties::CreateUncompressed(L"BGRA8", item.Size().Width, item.Size().Height);
		VideoStreamDescriptor videoDescriptor(videoProperties);
		_mediaSource = MediaStreamSource(videoDescriptor);
		_mediaSource.IsLive(true);
		_sampleRequested = _mediaSource.SampleRequested(auto_revoke, [](
			MediaStreamSource const& sender,
			MediaStreamSourceSampleRequestedEventArgs const& args)
		{	
			std::unique_lock lock(_captureMutex);
			_captureCond.wait(lock, []() { return !_samples.empty(); });

			for (auto sample : _samples)
			{
				args.Request().Sample(sample);
			}
			_samples.clear();
		});

		auto profile = MediaEncodingProfile::CreateWmv(VideoEncodingQuality::HD720p);

		auto storageFolder = winrt::Windows::Storage::KnownFolders::AppCaptures();
		auto storageFile = co_await storageFolder.CreateFileAsync(L"test.wmv", winrt::Windows::Storage::CreationCollisionOption::ReplaceExisting);
		auto stream = co_await storageFile.OpenAsync(FileAccessMode::ReadWrite);
		auto result = co_await _transcorder.PrepareMediaStreamSourceTranscodeAsync(_mediaSource, stream, profile);
		if (result.CanTranscode())
		{
			co_await result.TranscodeAsync();
		}
	}
	catch (...)
	{
	}
}

winrt::Windows::Foundation::IAsyncAction StartCaptureImpl()
{
	auto manager = winrt::Windows::Media::AppRecording::AppRecordingManager::GetDefault();

	auto storageFolder = winrt::Windows::Storage::KnownFolders::AppCaptures();
	auto storageFile = co_await storageFolder.CreateFileAsync(L"test.wmv", winrt::Windows::Storage::CreationCollisionOption::ReplaceExisting);
	auto status = manager.GetStatus();
	bool canRecord = status.CanRecord();
	co_await manager.StartRecordingToFileAsync(storageFile);
}

extern "C"
{
	void StartCapture()
	{
		StartCaptureImpl();
	}

	void CreateCaptureItemForWindow(HWND hwnd)
	{
		CreateCaptureItemForWindowImpl(hwnd);
	}
}