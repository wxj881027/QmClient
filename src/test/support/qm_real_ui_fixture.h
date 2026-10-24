// 真实 UI 与引擎文字渲染的隔离集成 fixture；仅替换设备边界。
#ifndef TEST_SUPPORT_QM_REAL_UI_FIXTURE_H
#define TEST_SUPPORT_QM_REAL_UI_FIXTURE_H

#include <base/system.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/input.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/qm_ui_test_client.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qm_ui_test
{
	// 只替换 GPU 提交；文字、控件绘制与公共下拉入口仍执行生产实现。
	class CRealUiTestGraphics final : public CIconBenchmarkGraphics
	{
		std::unordered_map<int, int> m_QuadCounts;
		CUIRect m_MapRect{0, 0, 1920, 1080};

	public:
		std::vector<IGraphics::SRoundedRectSdfParams> m_vRoundedSubmissions;
		std::vector<CUIRect> m_vRectSubmissions;
		// 只在设备调用前注入一次失败，文字创建/删除仍由真实引擎执行。
		bool m_FailBufferObjectCreate = false;
		bool m_FailBufferContainerCreate = false;
		enum class EDeleteFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
			OTHER,
		};
		EDeleteFailure m_BufferObjectDeleteFailure = EDeleteFailure::NONE;
		bool m_FailQuadContainerDelete = false;
		bool m_FailTextDraw = false;
		bool m_TextBuffering = true;
		bool m_RecordLegacyText = false;
		int m_FailLegacyDrawAfter = -1;
		struct SLegacyDraw
		{
			CQuadItem m_Item;
			ColorRGBA m_Color;
			int m_Texture;
		};
		std::vector<SLegacyDraw> m_vLegacyDraws;
		std::vector<std::pair<size_t, ColorRGBA>> m_vLegacyRecolors;
		ColorRGBA m_LegacyColor{1, 1, 1, 1};
		int m_LegacyTexture = -1;
		int m_LegacyKeepCalls = 0, m_LegacyReplayCalls = 0;
		bool IsTextBufferingEnabled() override { return m_TextBuffering; }
		void TextureSet(CTextureHandle Texture) override
		{
			if(m_RecordLegacyText)
				m_LegacyTexture = Texture.Id();
		}
		void SetColor(float R, float G, float B, float A) override
		{
			if(m_RecordLegacyText)
				m_LegacyColor = ColorRGBA(R, G, B, A);
		}
		void SetColor(ColorRGBA Color) override { SetColor(Color.r, Color.g, Color.b, Color.a); }
		void QuadsEndKeepVertices() override
		{
			if(m_RecordLegacyText)
				++m_LegacyKeepCalls;
		}
		void QuadsDrawCurrentVertices(bool = true) override
		{
			if(m_RecordLegacyText)
				++m_LegacyReplayCalls;
		}
		void ChangeColorOfQuadVertices(size_t Offset, unsigned char R, unsigned char G, unsigned char B, unsigned char A) override
		{
			if(m_RecordLegacyText)
				m_vLegacyRecolors.emplace_back(Offset, ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f));
		}
		int m_DirectBufferObjectDeleteCalls = 0;
		size_t QuadContainerCount() const { return m_QuadCounts.size(); }
		void ResetDeviceBuffers()
		{
			m_QuadCounts.clear();
			m_Buffers.clear();
			m_Containers.clear();
		}
		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			if(std::exchange(m_FailTextDraw, false))
				throw std::bad_alloc();
			CIconBenchmarkGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
		int CreateBufferObject(size_t Size, void *pData, int Flags, bool Moved = false) override
		{
			if(std::exchange(m_FailBufferObjectCreate, false))
				throw std::bad_alloc();
			return CIconBenchmarkGraphics::CreateBufferObject(Size, pData, Flags, Moved);
		}
		int CreateBufferContainer(SBufferContainerInfo *pInfo) override
		{
			if(std::exchange(m_FailBufferContainerCreate, false))
				throw std::bad_alloc();
			return CIconBenchmarkGraphics::CreateBufferContainer(pInfo);
		}
		void DeleteBufferObject(int Index) override
		{
			++m_DirectBufferObjectDeleteCalls;
			const EDeleteFailure Failure = std::exchange(m_BufferObjectDeleteFailure, EDeleteFailure::NONE);
			if(Failure == EDeleteFailure::ALLOCATION)
				throw std::bad_alloc();
			if(Failure == EDeleteFailure::LENGTH)
				throw std::length_error("controlled device deletion limit");
			if(Failure == EDeleteFailure::OTHER)
				throw std::logic_error("unrelated device deletion failure");
			CIconBenchmarkGraphics::DeleteBufferObject(Index);
		}
		// 测试只改变 framebuffer 设备尺寸，真实字号量化与布局仍由文字引擎执行。
		void SetFramebufferSize(int Width, int Height)
		{
			m_ScreenWidth = m_DrawableWidth = Width;
			m_ScreenHeight = m_DrawableHeight = Height;
		}
		// 设备边界记录真实投影状态，让缩放测试确实进入指定像素比例。
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			m_MapRect = {X0, Y0, X1 - X0, Y1 - Y0};
		}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_MapRect.x;
			*pY0 = m_MapRect.y;
			*pX1 = m_MapRect.x + m_MapRect.w;
			*pY1 = m_MapRect.y + m_MapRect.h;
		}
		// 普通分隔线也属于 GPU 提交边界，不替换控件布局或输入。
		void DrawRect(float X, float Y, float W, float H, ColorRGBA, int, float) override
		{
			++m_Draws;
			m_vRectSubmissions.push_back({X, Y, W, H});
		}
		// 取色器渐变只记录 GPU 提交，颜色解析与输入回写仍执行生产实现。
		void DrawRect4(float X, float Y, float W, float H, ColorRGBA, ColorRGBA, ColorRGBA, ColorRGBA, int, float) override
		{
			++m_Draws;
			m_vRectSubmissions.push_back({X, Y, W, H});
		}
		bool HasRoundedRectSdf() override { return true; }
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override
		{
			// 设备接口允许销毁空句柄；这个 fixture 不创建离屏目标。
			if(pTarget != nullptr && pTarget->IsValid())
				throw std::logic_error("unexpected live render target in UI fixture");
		}
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override { m_vRoundedSubmissions.push_back(Params); }
		// 裁剪、光标与选区只提交 GPU 绘制，不替代真实文字布局和输入行为。
		void ClipEnable(int, int, int, int) override {}
		void ClipDisable() override {}
		void QuadsDrawTL(const CQuadItem *pItems, int Num) override
		{
			if(m_RecordLegacyText)
				for(int i = 0; i < Num; ++i)
				{
					if(m_FailLegacyDrawAfter >= 0 && m_vLegacyDraws.size() == static_cast<size_t>(m_FailLegacyDrawAfter))
					{
						m_FailLegacyDrawAfter = -1;
						throw std::bad_alloc();
					}
					m_vLegacyDraws.push_back({pItems[i], m_LegacyColor, m_LegacyTexture});
				}
			++m_Draws;
			m_Quads += Num;
		}

		// 只记录 GPU 容器句柄、提交数量与追加偏移；选区和光标布局仍由文字引擎生成。
		int CreateQuadContainer(bool = true) override
		{
			const int Id = m_NextId++;
			m_QuadCounts.emplace(Id, 0);
			return Id;
		}
		int QuadContainerAddQuads(int ContainerIndex, CQuadItem *, int Num) override
		{
			int &Count = m_QuadCounts.at(ContainerIndex);
			const int Offset = Count;
			Count += Num;
			return Offset;
		}
		void QuadContainerReset(int ContainerIndex) override
		{
			// 文字引擎在尚未创建选区容器时也会重置 -1；这是设备接口允许的空句柄。
			if(ContainerIndex != -1)
				m_QuadCounts.at(ContainerIndex) = 0;
		}
		void QuadContainerUpload(int ContainerIndex) override
		{
			(void)m_QuadCounts.at(ContainerIndex);
			++m_Uploads;
		}
		void DeleteQuadContainer(int &ContainerIndex) override
		{
			if(ContainerIndex == -1)
				return;
			if(std::exchange(m_FailQuadContainerDelete, false))
				throw std::bad_alloc();
			m_QuadCounts.erase(ContainerIndex);
			ContainerIndex = -1;
		}
		void RenderQuadContainerEx(int ContainerIndex, int QuadOffset, int QuadDrawNum, float, float, float = 1.0f, float = 1.0f) override
		{
			const int Count = m_QuadCounts.at(ContainerIndex);
			++m_Draws;
			m_Quads += QuadDrawNum == -1 ? Count - QuadOffset : QuadDrawNum;
		}
	};

	// 不创建完整客户端；输入设备可由具体测试提供外部边界 fake。
	class CRealUiTestKernel final : public IKernel
	{
		IGraphics *m_pGraphics;
		ITextRender *m_pText;
		IInput *m_pInput;
		IClient *m_pClient;

		void RegisterInterfaceImpl(const char *, IInterface *, bool) override
		{
			throw std::logic_error("unexpected UI interface registration");
		}
		void ReregisterInterfaceImpl(const char *, IInterface *) override
		{
			throw std::logic_error("unexpected UI interface replacement");
		}
		IInterface *RequestInterfaceImpl(const char *pName) override
		{
			if(str_comp(pName, IGraphics::InterfaceName()) == 0)
				return m_pGraphics;
			if(str_comp(pName, ITextRender::InterfaceName()) == 0)
				return m_pText;
			if(str_comp(pName, IInput::InterfaceName()) == 0)
				return m_pInput;
			if(str_comp(pName, IClient::InterfaceName()) == 0)
				return m_pClient;
			throw std::logic_error("unexpected UI interface request");
		}

	public:
		CRealUiTestKernel(IGraphics *pGraphics, ITextRender *pText, IInput *pInput, IClient *pClient = nullptr) :
			m_pGraphics(pGraphics), m_pText(pText), m_pInput(pInput), m_pClient(pClient) {}
		void Shutdown() override {}
	};

	class CScopedRealUiConfig
	{
		CConfig m_Previous = g_Config;

	public:
		~CScopedRealUiConfig() { g_Config = m_Previous; }
	};

	class CRealUiFixture : public ::testing::Test
	{
		CScopedRealUiConfig m_ConfigGuard;
		CTestInfo m_Info;
		std::unique_ptr<IKernel> m_pEngineKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IStorage> m_pStorage;
		CRealUiTestGraphics m_Graphics;
		std::unique_ptr<IEngineTextRender> m_pText;
		std::unique_ptr<CRealUiTestKernel> m_pUiKernel;
		bool m_TextInitialized = false;
		bool m_UiInitialized = false;
		bool m_PreviousAutoManaged = CLineInput::TextInputAutoManaged();

	protected:
		CUi m_Ui;
		CRealUiTestGraphics &Graphics() { return m_Graphics; }
		// 真实 UI 环境默认挂动画运行时（生产 gameclient 恒挂）；动效等级
		// 默认 0 时资格门仍禁动画，需要动画的测试显式开 FullMotion 并用
		// AdvanceAnimFrame 同步驱动轨道时钟。
		CUiV2AnimationRuntime m_AnimRuntime;
		void AdvanceAnimFrame(float Dt = 1.0f / 60.0f) { m_AnimRuntime.Advance(Dt); }
		virtual IInput *UiInput() { return nullptr; }
		virtual IClient *UiClient() { return nullptr; }

		void SetUp() override
		{
			ASSERT_EQ(CLineInput::GetActiveInput(), nullptr);
			CLineInput::SetTextInputAutoManaged(false);
			g_Config = CConfig();
			g_Config.m_QmUiMotionLevel = 0;
			g_Config.m_QmGaussianBlur = 0;
			g_Config.m_QmPerfDebug = g_Config.m_QmPerfLogfile = g_Config.m_QmPerfStutterDiagnostics = 0;
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pEngineKernel.reset(IKernel::Create());
			m_pConsole = CreateConsole(CFGFLAG_CLIENT);
			m_Graphics.m_pStorage = m_pStorage.get();
			m_pEngineKernel->RegisterInterface<IConsole>(m_pConsole.get(), false);
			m_pEngineKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pEngineKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			m_pText.reset(CreateEngineTextRender());
			m_pEngineKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
			ASSERT_TRUE(m_pText->Init());
			m_TextInitialized = true;
			ASSERT_TRUE(m_pText->LoadFonts());
			m_pUiKernel = std::make_unique<CRealUiTestKernel>(&m_Graphics, m_pText.get(), UiInput(), UiClient());
			m_Ui.Init(m_pUiKernel.get());
			m_Ui.SetQmAnimationRuntime(&m_AnimRuntime);
			if(auto *pTestClient = dynamic_cast<qm_ui_test::CTestUiClient *>(UiClient()))
				pTestClient->AttachAnimRuntime(&m_AnimRuntime);
			m_UiInitialized = true;
		}

		void TearDown() override
		{
			if(m_UiInitialized)
			{
				if(CLineInput::GetActiveInput() != nullptr)
					m_Ui.ReleaseActiveTextInput(CLineInput::GetActiveInput());
				m_Ui.ClosePopupMenus();
				m_Ui.SetQmAnimationRuntime(nullptr);
				m_Ui.OnShutdown();
				// 独立 UI 测试进程没有外部 owner，每个 fixture 恢复到未绑定设备状态。
				CUIRect::Init(nullptr, nullptr);
				CUIElementBase::Init(nullptr);
				CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
			}
			CLineInput::SetTextInputAutoManaged(m_PreviousAutoManaged);
			if(m_TextInitialized)
				m_pText->Shutdown();
		}
	};
}

#endif
