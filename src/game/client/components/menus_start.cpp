/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <algorithm>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#ifdef CONF_RMLUI
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

#include <base/system.h>

#include <engine/gfx/image_loader.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#endif

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

using namespace FontIcons;

#ifdef CONF_RMLUI
namespace
{
constexpr float BUTTON_HEIGHT = 40.0f;
constexpr float BUTTON_GAP = 5.0f;
constexpr float BUTTON_GAP_LARGE = 100.0f;
constexpr float BUTTONS_AREA_HEIGHT = BUTTON_HEIGHT * 6 + BUTTON_GAP * 4 + BUTTON_GAP_LARGE;

enum class EStartMenuAction
{
	None,
	Play,
	Demos,
	Editor,
	LocalServer,
	Settings,
	Quit,
};

struct SRmlUiGeometry
{
	std::vector<Rml::Vertex> m_Vertices;
	std::vector<int> m_Indices;
};

struct SRmlUiTexture
{
	IGraphics::CTextureHandle m_Handle;
	int m_Width = 0;
	int m_Height = 0;
};

static void Unpremultiply(std::vector<uint8_t> &Data)
{
	for(size_t i = 0; i + 3 < Data.size(); i += 4)
	{
		const uint8_t A = Data[i + 3];
		if(A == 0)
		{
			Data[i] = 0;
			Data[i + 1] = 0;
			Data[i + 2] = 0;
			continue;
		}
		Data[i] = (uint8_t)std::min(255u, (unsigned)(Data[i] * 255u + A / 2u) / A);
		Data[i + 1] = (uint8_t)std::min(255u, (unsigned)(Data[i + 1] * 255u + A / 2u) / A);
		Data[i + 2] = (uint8_t)std::min(255u, (unsigned)(Data[i + 2] * 255u + A / 2u) / A);
	}
}

class CRmlUiRenderInterface final : public Rml::RenderInterface
{
public:
	explicit CRmlUiRenderInterface(CMenusStart *pOwner) :
		m_pOwner(pOwner)
	{
	}

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
	{
		auto *pGeometry = new SRmlUiGeometry;
		pGeometry->m_Vertices.assign(vertices.begin(), vertices.end());
		pGeometry->m_Indices.assign(indices.begin(), indices.end());
		return reinterpret_cast<Rml::CompiledGeometryHandle>(pGeometry);
	}

	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override
	{
		const auto *pGeometry = reinterpret_cast<const SRmlUiGeometry *>(geometry);
		if(!pGeometry || pGeometry->m_Indices.empty())
			return;

		IGraphics *pGraphics = m_pOwner->Graphics();

		if(texture)
		{
			const auto *pTexture = reinterpret_cast<const SRmlUiTexture *>(texture);
			if(pTexture && pTexture->m_Handle.IsValid())
				pGraphics->TextureSet(pTexture->m_Handle);
			else
				pGraphics->TextureClear();
			pGraphics->WrapClamp();
		}
		else
		{
			pGraphics->TextureClear();
		}

		pGraphics->BlendNormal();
		pGraphics->TrianglesBegin();

		constexpr float Inv255 = 1.0f / 255.0f;
		for(size_t i = 0; i + 2 < pGeometry->m_Indices.size(); i += 3)
		{
			const Rml::Vertex &V0 = pGeometry->m_Vertices[pGeometry->m_Indices[i]];
			const Rml::Vertex &V1 = pGeometry->m_Vertices[pGeometry->m_Indices[i + 1]];
			const Rml::Vertex &V2 = pGeometry->m_Vertices[pGeometry->m_Indices[i + 2]];

			const float X0 = V0.position.x + translation.x;
			const float Y0 = V0.position.y + translation.y;
			const float X1 = V1.position.x + translation.x;
			const float Y1 = V1.position.y + translation.y;
			const float X2 = V2.position.x + translation.x;
			const float Y2 = V2.position.y + translation.y;

			pGraphics->QuadsSetSubsetFree(
				V0.tex_coord.x, V0.tex_coord.y,
				V1.tex_coord.x, V1.tex_coord.y,
				V2.tex_coord.x, V2.tex_coord.y,
				V2.tex_coord.x, V2.tex_coord.y);

			const IGraphics::CColorVertex Colors[4] = {
				IGraphics::CColorVertex(0, V0.colour.red * Inv255, V0.colour.green * Inv255, V0.colour.blue * Inv255, V0.colour.alpha * Inv255),
				IGraphics::CColorVertex(1, V1.colour.red * Inv255, V1.colour.green * Inv255, V1.colour.blue * Inv255, V1.colour.alpha * Inv255),
				IGraphics::CColorVertex(2, V2.colour.red * Inv255, V2.colour.green * Inv255, V2.colour.blue * Inv255, V2.colour.alpha * Inv255),
				IGraphics::CColorVertex(3, V2.colour.red * Inv255, V2.colour.green * Inv255, V2.colour.blue * Inv255, V2.colour.alpha * Inv255),
			};
			pGraphics->SetColorVertex(Colors, 4);

			const IGraphics::CFreeformItem Item(X0, Y0, X1, Y1, X2, Y2, X2, Y2);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}

		pGraphics->TrianglesEnd();

		if(texture)
			pGraphics->WrapNormal();
	}

	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
	{
		delete reinterpret_cast<SRmlUiGeometry *>(geometry);
	}

	Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source) override
	{
		Rml::FileInterface *pFileInterface = Rml::GetFileInterface();
		Rml::FileHandle FileHandle = pFileInterface->Open(source);
		if(!FileHandle)
			return {};

		pFileInterface->Seek(FileHandle, 0, SEEK_END);
		const size_t BufferSize = pFileInterface->Tell(FileHandle);
		pFileInterface->Seek(FileHandle, 0, SEEK_SET);
		if(BufferSize == 0)
		{
			pFileInterface->Close(FileHandle);
			return {};
		}

		std::vector<uint8_t> Buffer(BufferSize);
		pFileInterface->Read(Buffer.data(), BufferSize, FileHandle);
		pFileInterface->Close(FileHandle);

		CImageInfo Image;
		int PngliteIncompatible = 0;
		CByteBufferReader Reader(Buffer.data(), Buffer.size());
		if(!CImageLoader::LoadPng(Reader, source.c_str(), Image, PngliteIncompatible))
			return {};

		texture_dimensions.x = (int)Image.m_Width;
		texture_dimensions.y = (int)Image.m_Height;

		IGraphics::CTextureHandle TexHandle = m_pOwner->Graphics()->LoadTextureRawMove(Image, 0, source.c_str());
		if(!TexHandle.IsValid())
			return {};

		auto *pTexture = new SRmlUiTexture;
		pTexture->m_Handle = TexHandle;
		pTexture->m_Width = texture_dimensions.x;
		pTexture->m_Height = texture_dimensions.y;
		return reinterpret_cast<Rml::TextureHandle>(pTexture);
	}

	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override
	{
		const size_t ExpectedSize = (size_t)source_dimensions.x * (size_t)source_dimensions.y * 4;
		if(source.size() != ExpectedSize)
			return {};

		std::vector<uint8_t> Buffer(source.begin(), source.end());
		Unpremultiply(Buffer);

		CImageInfo Image;
		Image.m_Width = (size_t)source_dimensions.x;
		Image.m_Height = (size_t)source_dimensions.y;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = (uint8_t *)malloc(Buffer.size());
		if(!Image.m_pData)
			return {};
		memcpy(Image.m_pData, Buffer.data(), Buffer.size());

		IGraphics::CTextureHandle TexHandle = m_pOwner->Graphics()->LoadTextureRawMove(Image, 0, "rmlui");
		if(!TexHandle.IsValid())
			return {};

		auto *pTexture = new SRmlUiTexture;
		pTexture->m_Handle = TexHandle;
		pTexture->m_Width = source_dimensions.x;
		pTexture->m_Height = source_dimensions.y;
		return reinterpret_cast<Rml::TextureHandle>(pTexture);
	}

	void ReleaseTexture(Rml::TextureHandle texture) override
	{
		auto *pTexture = reinterpret_cast<SRmlUiTexture *>(texture);
		if(!pTexture)
			return;
		m_pOwner->Graphics()->UnloadTexture(&pTexture->m_Handle);
		delete pTexture;
	}

	void EnableScissorRegion(bool enable) override
	{
		m_ScissorEnabled = enable;
		if(m_ScissorEnabled)
			UpdateScissor();
		else
			m_pOwner->Graphics()->ClipDisable();
	}

	void SetScissorRegion(Rml::Rectanglei region) override
	{
		m_ScissorRegion = region;
		if(m_ScissorEnabled)
			UpdateScissor();
	}

private:
	void UpdateScissor()
	{
		const CUIRect *pScreen = m_pOwner->Ui()->Screen();
		const float XScale = m_pOwner->Graphics()->ScreenWidth() / pScreen->w;
		const float YScale = m_pOwner->Graphics()->ScreenHeight() / pScreen->h;
		const int ClipX = (int)(m_ScissorRegion.Left() * XScale);
		const int ClipY = (int)(m_ScissorRegion.Top() * YScale);
		const int ClipW = (int)(m_ScissorRegion.Width() * XScale);
		const int ClipH = (int)(m_ScissorRegion.Height() * YScale);
		m_pOwner->Graphics()->ClipEnable(ClipX, ClipY, ClipW, ClipH);
	}

	CMenusStart *m_pOwner = nullptr;
	bool m_ScissorEnabled = false;
	Rml::Rectanglei m_ScissorRegion;
};

class CRmlUiSystemInterface final : public Rml::SystemInterface
{
public:
	explicit CRmlUiSystemInterface(CMenusStart *pOwner) :
		m_pOwner(pOwner)
	{
	}

	double GetElapsedTime() override
	{
		return m_pOwner->Client()->LocalTime();
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
	{
		const char *pPrefix = "info";
		if(type == Rml::Log::LT_ERROR)
			pPrefix = "error";
		else if(type == Rml::Log::LT_WARNING)
			pPrefix = "warning";
		dbg_msg("rmlui", "%s: %s", pPrefix, message.c_str());
		return true;
	}

	void JoinPath(Rml::String &translated_path, const Rml::String &document_path, const Rml::String &path) override
	{
		if(path.empty())
		{
			translated_path = path;
			return;
		}

		if(path.find(':') != Rml::String::npos || (!path.empty() && (path[0] == '/' || path[0] == '\\')))
		{
			translated_path = path;
			return;
		}

		const size_t SlashPos = document_path.find_last_of("/\\");
		if(SlashPos == Rml::String::npos)
			translated_path = path;
		else
			translated_path = document_path.substr(0, SlashPos + 1) + path;
	}

	void SetClipboardText(const Rml::String &text) override
	{
		m_pOwner->Input()->SetClipboardText(text.c_str());
	}

	void GetClipboardText(Rml::String &text) override
	{
		text = m_pOwner->Input()->GetClipboardText();
	}

private:
	CMenusStart *m_pOwner = nullptr;
};

class CRmlUiFileInterface final : public Rml::FileInterface
{
public:
	explicit CRmlUiFileInterface(CMenusStart *pOwner) :
		m_pOwner(pOwner)
	{
	}

	Rml::FileHandle Open(const Rml::String &path) override
	{
		IOHANDLE File = m_pOwner->Storage()->OpenFile(path.c_str(), IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE);
		return (Rml::FileHandle)File;
	}

	void Close(Rml::FileHandle file) override
	{
		if(file)
			io_close((IOHANDLE)file);
	}

	size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
	{
		if(!file || size == 0)
			return 0;
		return (size_t)io_read((IOHANDLE)file, buffer, (unsigned)size);
	}

	bool Seek(Rml::FileHandle file, long offset, int origin) override
	{
		if(!file)
			return false;
		ESeekOrigin SeekOrigin = IOSEEK_START;
		if(origin == SEEK_CUR)
			SeekOrigin = IOSEEK_CUR;
		else if(origin == SEEK_END)
			SeekOrigin = IOSEEK_END;
		return io_seek((IOHANDLE)file, offset, SeekOrigin) == 0;
	}

	size_t Tell(Rml::FileHandle file) override
	{
		if(!file)
			return 0;
		const int64_t Pos = io_tell((IOHANDLE)file);
		return Pos < 0 ? 0u : (size_t)Pos;
	}

private:
	CMenusStart *m_pOwner = nullptr;
};

class CRmlUiStartMenu;

class CStartMenuButtonListener final : public Rml::EventListener
{
public:
	CStartMenuButtonListener(CRmlUiStartMenu *pOwner, EStartMenuAction Action) :
		m_pOwner(pOwner),
		m_Action(Action)
	{
	}

	void ProcessEvent(Rml::Event & /*event*/) override;

private:
	CRmlUiStartMenu *m_pOwner = nullptr;
	EStartMenuAction m_Action = EStartMenuAction::None;
};

class CRmlUiStartMenu
{
public:
	~CRmlUiStartMenu()
	{
		if(m_pDocument)
		{
			m_pDocument->Close();
			m_pDocument->RemoveReference();
			m_pDocument = nullptr;
		}
		if(m_pContext)
		{
			Rml::RemoveContext("start_menu");
			m_pContext = nullptr;
		}
		if(m_Initialized)
			Rml::Shutdown();
	}

	bool RenderMenu(CMenusStart *pOwner, const CUIRect &MenuRect, bool LocalServerRunning, bool EditorDirty, EStartMenuAction &OutAction)
	{
		if(!EnsureInit(pOwner))
			return false;

		UpdateLayout(MenuRect);
		UpdateLabels(LocalServerRunning, EditorDirty);
		UpdateInput();
		m_pContext->Update();
		m_pContext->Render();
		OutAction = ConsumeAction();
		return true;
	}

	void QueueAction(EStartMenuAction Action)
	{
		m_PendingAction = Action;
	}

private:
	bool EnsureInit(CMenusStart *pOwner)
	{
		m_pOwner = pOwner;
		if(m_Initialized)
		{
			if(!m_pDocument)
				LoadDocument();
			return m_pContext != nullptr && m_pDocument != nullptr;
		}

		m_RenderInterface = std::make_unique<CRmlUiRenderInterface>(m_pOwner);
		m_SystemInterface = std::make_unique<CRmlUiSystemInterface>(m_pOwner);
		m_FileInterface = std::make_unique<CRmlUiFileInterface>(m_pOwner);

		Rml::SetRenderInterface(m_RenderInterface.get());
		Rml::SetSystemInterface(m_SystemInterface.get());
		Rml::SetFileInterface(m_FileInterface.get());

		if(!Rml::Initialise())
			return false;
		m_Initialized = true;

		const CUIRect *pScreen = m_pOwner->Ui()->Screen();
		m_pContext = Rml::CreateContext("start_menu", Rml::Vector2i((int)pScreen->w, (int)pScreen->h));
		if(!m_pContext)
			return false;

		Rml::LoadFontFace("fonts/GlowSansJ-Compressed-Book.otf");
		Rml::LoadFontFace("fonts/SourceHanSans.ttc", true);
		Rml::LoadFontFace("fonts/DejaVuSans.ttf");

		LoadDocument();
		return m_pDocument != nullptr;
	}

	void LoadDocument()
	{
		m_pDocument = m_pContext->LoadDocument("ui/start_menu.rml");
		if(!m_pDocument)
			return;
		m_pDocument->Show();

		m_pMenuButtons = m_pDocument->GetElementById("menu-buttons");
		m_pButtonPlay = m_pDocument->GetElementById("play");
		m_pButtonDemos = m_pDocument->GetElementById("demos");
		m_pButtonEditor = m_pDocument->GetElementById("editor");
		m_pButtonLocalServer = m_pDocument->GetElementById("local_server");
		m_pButtonSettings = m_pDocument->GetElementById("settings");
		m_pButtonQuit = m_pDocument->GetElementById("quit");

		m_ListenerPlay = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::Play);
		m_ListenerDemos = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::Demos);
		m_ListenerEditor = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::Editor);
		m_ListenerLocalServer = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::LocalServer);
		m_ListenerSettings = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::Settings);
		m_ListenerQuit = std::make_unique<CStartMenuButtonListener>(this, EStartMenuAction::Quit);

		if(m_pButtonPlay)
			m_pButtonPlay->AddEventListener("click", m_ListenerPlay.get());
		if(m_pButtonDemos)
			m_pButtonDemos->AddEventListener("click", m_ListenerDemos.get());
		if(m_pButtonEditor)
			m_pButtonEditor->AddEventListener("click", m_ListenerEditor.get());
		if(m_pButtonLocalServer)
			m_pButtonLocalServer->AddEventListener("click", m_ListenerLocalServer.get());
		if(m_pButtonSettings)
			m_pButtonSettings->AddEventListener("click", m_ListenerSettings.get());
		if(m_pButtonQuit)
			m_pButtonQuit->AddEventListener("click", m_ListenerQuit.get());
	}

	void UpdateLayout(const CUIRect &MenuRect)
	{
		if(!m_pMenuButtons)
			return;

		const float Top = MenuRect.y + (MenuRect.h - BUTTONS_AREA_HEIGHT);

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%.2fpx", MenuRect.x);
		m_pMenuButtons->SetProperty("left", aBuf);
		str_format(aBuf, sizeof(aBuf), "%.2fpx", Top);
		m_pMenuButtons->SetProperty("top", aBuf);
		str_format(aBuf, sizeof(aBuf), "%.2fpx", MenuRect.w);
		m_pMenuButtons->SetProperty("width", aBuf);
		str_format(aBuf, sizeof(aBuf), "%.2fpx", BUTTONS_AREA_HEIGHT);
		m_pMenuButtons->SetProperty("height", aBuf);
	}

	void UpdateLabels(bool LocalServerRunning, bool EditorDirty)
	{
		if(m_pButtonPlay)
			m_pButtonPlay->SetInnerRML(Localize("Play", "Start menu"));
		if(m_pButtonDemos)
			m_pButtonDemos->SetInnerRML(Localize("Demos"));
		if(m_pButtonEditor)
		{
			m_pButtonEditor->SetInnerRML(Localize("Editor"));
			m_pButtonEditor->SetClass("highlight", EditorDirty);
		}
		if(m_pButtonLocalServer)
		{
			m_pButtonLocalServer->SetInnerRML(LocalServerRunning ? Localize("Stop server") : Localize("Run server"));
			m_pButtonLocalServer->SetClass("highlight", LocalServerRunning);
		}
		if(m_pButtonSettings)
			m_pButtonSettings->SetInnerRML(Localize("Settings"));
		if(m_pButtonQuit)
			m_pButtonQuit->SetInnerRML(Localize("Quit"));
	}

	void UpdateInput()
	{
		const CUIRect *pScreen = m_pOwner->Ui()->Screen();
		m_pContext->SetDimensions(Rml::Vector2i((int)pScreen->w, (int)pScreen->h));

		const int MouseX = (int)m_pOwner->Ui()->MouseX();
		const int MouseY = (int)m_pOwner->Ui()->MouseY();
		m_pContext->ProcessMouseMove(MouseX, MouseY, 0);

		for(int Button = 0; Button < 3; ++Button)
		{
			const bool Pressed = m_pOwner->Ui()->MouseButton(Button);
			if(Pressed != m_aMouseButtons[Button])
			{
				if(Pressed)
					m_pContext->ProcessMouseButtonDown(Button, 0);
				else
					m_pContext->ProcessMouseButtonUp(Button, 0);
				m_aMouseButtons[Button] = Pressed;
			}
		}
	}

	EStartMenuAction ConsumeAction()
	{
		const EStartMenuAction Action = m_PendingAction;
		m_PendingAction = EStartMenuAction::None;
		return Action;
	}

	CMenusStart *m_pOwner = nullptr;
	bool m_Initialized = false;
	Rml::Context *m_pContext = nullptr;
	Rml::ElementDocument *m_pDocument = nullptr;
	Rml::Element *m_pMenuButtons = nullptr;
	Rml::Element *m_pButtonPlay = nullptr;
	Rml::Element *m_pButtonDemos = nullptr;
	Rml::Element *m_pButtonEditor = nullptr;
	Rml::Element *m_pButtonLocalServer = nullptr;
	Rml::Element *m_pButtonSettings = nullptr;
	Rml::Element *m_pButtonQuit = nullptr;

	std::unique_ptr<CRmlUiRenderInterface> m_RenderInterface;
	std::unique_ptr<CRmlUiSystemInterface> m_SystemInterface;
	std::unique_ptr<CRmlUiFileInterface> m_FileInterface;

	std::unique_ptr<CStartMenuButtonListener> m_ListenerPlay;
	std::unique_ptr<CStartMenuButtonListener> m_ListenerDemos;
	std::unique_ptr<CStartMenuButtonListener> m_ListenerEditor;
	std::unique_ptr<CStartMenuButtonListener> m_ListenerLocalServer;
	std::unique_ptr<CStartMenuButtonListener> m_ListenerSettings;
	std::unique_ptr<CStartMenuButtonListener> m_ListenerQuit;

	bool m_aMouseButtons[3] = {false, false, false};
	EStartMenuAction m_PendingAction = EStartMenuAction::None;
};

void CStartMenuButtonListener::ProcessEvent(Rml::Event & /*event*/)
{
	if(m_pOwner)
		m_pOwner->QueueAction(m_Action);
}

CRmlUiStartMenu g_RmlUiStartMenu;
}
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem QuadItem(MainView.w / 2 - 170, 60, 360, 103);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://ddnet.org/discord"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_LearnButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TutorialButton;
	static float s_JoinTutorialTime = 0.0f;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) ||
		(s_JoinTutorialTime != 0.0f && Client()->LocalTime() >= s_JoinTutorialTime))
	{
		// Activate internet tab before joining tutorial to make sure the server info
		// for the tutorial servers is available.
		GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_INTERNET);
		GameClient()->m_Menus.RefreshBrowserTab(true);
		const char *pAddr = ServerBrowser()->GetTutorialServer();
		if(pAddr)
		{
			Client()->Connect(pAddr);
			s_JoinTutorialTime = 0.0f;
		}
		else if(s_JoinTutorialTime == 0.0f)
		{
			dbg_msg("menus", "couldn't find tutorial server, retrying in 5 seconds");
			s_JoinTutorialTime = Client()->LocalTime() + 5.0f;
		}
		else
		{
			Client()->AddWarning(SWarning(Localize("Can't find a Tutorial server")));
			s_JoinTutorialTime = 0.0f;
		}
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_WebsiteButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://ddnet.org/");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_NewsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_UpdateButton;
	const bool UpdateChecking = GameClient()->m_TClient.IsUpdateChecking();
	const bool UpdateDownloading = GameClient()->m_TClient.IsUpdateDownloading();
	const bool UpdateBusy = UpdateChecking || UpdateDownloading;
	const char *pUpdateLabel = UpdateDownloading ? Localize("Updating…") : (UpdateChecking ? Localize("Checking…") : Localize("Update"));
	if(GameClient()->m_Menus.DoButton_Menu(&s_UpdateButton, pUpdateLabel, 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		if(!UpdateBusy)
			GameClient()->m_TClient.RequestUpdateCheckAndUpdate();
	}

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	CUIRect QuitNote;
	Menu.HSplitBottom(22.0f, &Menu, &QuitNote);
	CUIRect Line1, Line2;
	QuitNote.HSplitTop(11.0f, &Line1, &QuitNote);
	QuitNote.HSplitTop(11.0f, &Line2, nullptr);
	Ui()->DoLabel(&Line1, "在我死去之前", 6.0f, TEXTALIGN_MC);
	Ui()->DoLabel(&Line2, "    谨以此端,回忆我", 3.0f, TEXTALIGN_MC);

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	const bool EditorDirty = GameClient()->Editor()->HasUnsavedData();
	bool UseRmlUi = false;
#ifdef CONF_RMLUI
	EStartMenuAction RmlAction = EStartMenuAction::None;
	if(g_RmlUiStartMenu.RenderMenu(this, Menu, LocalServerRunning, EditorDirty, RmlAction))
	{
		UseRmlUi = true;

		bool UsedEscape = false;
		if(RmlAction == EStartMenuAction::Quit || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
		{
			if(UsedEscape || EditorDirty || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			{
				GameClient()->m_Menus.ShowQuitPopup();
			}
			else
			{
				Client()->Quit();
			}
		}

		if(RmlAction == EStartMenuAction::Settings || CheckHotKey(KEY_S))
			NewPage = CMenus::PAGE_SETTINGS;

		if(RmlAction == EStartMenuAction::LocalServer || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
		{
			if(LocalServerRunning)
				GameClient()->m_LocalServer.KillServer();
			else
				GameClient()->m_LocalServer.RunServer({});
		}

		if(RmlAction == EStartMenuAction::Editor || CheckHotKey(KEY_E))
		{
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}

		if(RmlAction == EStartMenuAction::Demos || CheckHotKey(KEY_D))
			NewPage = CMenus::PAGE_DEMOS;

		if(RmlAction == EStartMenuAction::Play || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		{
			NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
		}
	}
#endif

	if(!UseRmlUi)
	{
		constexpr int MenuButtonCount = 6;
		CUIRect aMenuButtons[MenuButtonCount];
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[0]);
		Menu.HSplitBottom(100.0f, &Menu, nullptr);
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[1]);
		Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[2]);

		Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[3]);

		Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[4]);

		Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
		Menu.HSplitBottom(40.0f, &Menu, &aMenuButtons[5]);

		static float s_aMenuButtonScale[MenuButtonCount] = {};
		static bool s_MenuButtonScaleInit = false;
		if(!s_MenuButtonScaleInit)
		{
			for(float &Scale : s_aMenuButtonScale)
				Scale = 1.0f;
			s_MenuButtonScaleInit = true;
		}

		const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) {
			CUIRect Out = Base;
			Out.w *= Scale;
			Out.h *= Scale;
			Out.x = Base.x + (Base.w - Out.w) * 0.5f;
			Out.y = Base.y + (Base.h - Out.h) * 0.5f;
			return Out;
		};

		constexpr int QuitIndex = 0;
		int HoveredIndex = -1;
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			if(i == QuitIndex)
				continue;
			const CUIRect HoverRect = ScaleButtonRect(aMenuButtons[i], s_aMenuButtonScale[i]);
			if(Ui()->MouseHovered(&HoverRect))
			{
				HoveredIndex = i;
				break;
			}
		}
		const CUIRect QuitHoverRect = ScaleButtonRect(aMenuButtons[QuitIndex], s_aMenuButtonScale[QuitIndex]);
		const bool QuitHovered = Ui()->MouseHovered(&QuitHoverRect);

		const bool AnyHovered = HoveredIndex != -1;
		const float HoverScale = 1.08f;
		const float OtherScale = 0.94f;
		const float Speed = 12.0f;
		const float Blend = std::clamp(Client()->RenderFrameTime() * Speed, 0.0f, 1.0f);
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			float Target = 1.0f;
			if(i == QuitIndex)
			{
				Target = QuitHovered ? HoverScale : 1.0f;
			}
			else if(QuitHovered)
			{
				Target = 1.0f;
			}
			else if(AnyHovered)
			{
				Target = (i == HoveredIndex) ? HoverScale : OtherScale;
			}
			s_aMenuButtonScale[i] += (Target - s_aMenuButtonScale[i]) * Blend;
		}

		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[0], s_aMenuButtonScale[0]);
		static CButtonContainer s_QuitButton;
		bool UsedEscape = false;
		if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &ScaledButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
		{
			if(UsedEscape || EditorDirty || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			{
				GameClient()->m_Menus.ShowQuitPopup();
			}
			else
			{
				Client()->Quit();
			}
		}

		ScaledButton = ScaleButtonRect(aMenuButtons[1], s_aMenuButtonScale[1]);
		static CButtonContainer s_SettingsButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
			NewPage = CMenus::PAGE_SETTINGS;

		ScaledButton = ScaleButtonRect(aMenuButtons[2], s_aMenuButtonScale[2]);
		static CButtonContainer s_LocalServerButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
		{
			if(LocalServerRunning)
			{
				GameClient()->m_LocalServer.KillServer();
			}
			else
			{
				GameClient()->m_LocalServer.RunServer({});
			}
		}

		ScaledButton = ScaleButtonRect(aMenuButtons[3], s_aMenuButtonScale[3]);
		static CButtonContainer s_MapEditorButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, EditorDirty ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_E))
		{
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}

		ScaledButton = ScaleButtonRect(aMenuButtons[4], s_aMenuButtonScale[4]);
		static CButtonContainer s_DemoButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D))
		{
			NewPage = CMenus::PAGE_DEMOS;
		}

		ScaledButton = ScaleButtonRect(aMenuButtons[5], s_aMenuButtonScale[5]);
		static CButtonContainer s_PlayButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		{
			NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
		}
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

	CUIRect TClientVersion;
	MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
	TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
	char aTBuf[64];
	str_format(aTBuf, sizeof(aTBuf), CLIENT_NAME " %s", CLIENT_RELEASE_VERSION);
	Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateToDateText;
	MainView.HSplitTop(15.0f, &UpdateToDateText, nullptr);
	UpdateToDateText.VSplitRight(40.0f, &UpdateToDateText, nullptr);
	if(!GameClient()->m_TClient.NeedUpdate() && GameClient()->m_TClient.m_FetchedTClientInfo)
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(On Latest)"), 14.0f, TEXTALIGN_MR);
	}
	else
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(Fetching Update Info)"), 14.0f, TEXTALIGN_MR);
	}
#endif
	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FONT_ICON_TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
