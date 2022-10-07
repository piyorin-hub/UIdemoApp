#include "pch.h"
#include "AppMain.h"
#include "Cannon/DrawCall.h"

using namespace winrt;

using namespace winrt::Windows;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Composition;

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
	CompositionTarget m_target{ nullptr };
	VisualCollection m_visuals{ nullptr };
	Visual m_selected{ nullptr };
	float2 m_offset{};

	bool F_windowClosed = false;
	std::unique_ptr<AppMain> F_main = nullptr;

	IFrameworkView CreateView()
	{
		return *this;
	}

	void Initialize(CoreApplicationView const&)
	{
	}

	void Load(hstring const&)
	{
	}

	void Uninitialize()
	{
		DrawCall::Uninitialize();
	}

	void Run()
	{
		CoreWindow window = CoreWindow::GetForCurrentThread();
		window.Activate();

		while (!F_windowClosed)
		{
			winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
			F_windowClosed = F_main->Update();
			F_main->Render();
		}
	}

	void SetWindow(CoreWindow const& window)
	{
		DrawCall::Initialize();
		if (!F_main)
			F_main = std::make_unique<AppMain>();
		F_main->init();
		window.Closed({ this, &App::OnWindowClosed });

		//horographicSpace‚ÌŽæ“¾H
		if (!MixedReality::IsAvailable())
			DrawCall::InitializeSwapChain((unsigned)window.Bounds().Width, (unsigned)window.Bounds().Height, window);
	}

	void OnWindowClosed(winrt::Windows::UI::Core::CoreWindow const& sender, winrt::Windows::UI::Core::CoreWindowEventArgs const& args)
	{
		F_windowClosed = true;
	}

	void OnPointerPressed(IInspectable const&, PointerEventArgs const& args)
	{
		float2 const point = args.CurrentPoint().Position();

		for (Visual visual : m_visuals)
		{
			float3 const offset = visual.Offset();
			float2 const size = visual.Size();

			if (point.x >= offset.x &&
				point.x < offset.x + size.x &&
				point.y >= offset.y &&
				point.y < offset.y + size.y)
			{
				m_selected = visual;
				m_offset.x = offset.x - point.x;
				m_offset.y = offset.y - point.y;
			}
		}

		if (m_selected)
		{
			m_visuals.Remove(m_selected);
			m_visuals.InsertAtTop(m_selected);
		}
		else
		{
			AddVisual(point);
		}
	}

	void OnPointerMoved(IInspectable const&, PointerEventArgs const& args)
	{
		if (m_selected)
		{
			float2 const point = args.CurrentPoint().Position();

			m_selected.Offset(
				{
					point.x + m_offset.x,
					point.y + m_offset.y,
					0.0f
				});
		}
	}

	void AddVisual(float2 const point)
	{
		Compositor compositor = m_visuals.Compositor();
		SpriteVisual visual = compositor.CreateSpriteVisual();

		static Color colors[] =
		{
			{ 0xDC, 0x5B, 0x9B, 0xD5 },
			{ 0xDC, 0xED, 0x7D, 0x31 },
			{ 0xDC, 0x70, 0xAD, 0x47 },
			{ 0xDC, 0xFF, 0xC0, 0x00 }
		};

		static unsigned last = 0;
		unsigned const next = ++last % _countof(colors);
		visual.Brush(compositor.CreateColorBrush(colors[next]));

		float const BlockSize = 100.0f;

		visual.Size(
			{
				BlockSize,
				BlockSize
			});

		visual.Offset(
			{
				point.x - BlockSize / 2.0f,
				point.y - BlockSize / 2.0f,
				0.0f,
			});

		m_visuals.InsertAtTop(visual);

		m_selected = visual;
		m_offset.x = -BlockSize / 2.0f;
		m_offset.y = -BlockSize / 2.0f;
	}
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoreApplication::Run(make<App>());
}