#include "quadart.h"

#include "editor.h"
#include "editor_actions.h"

#include <base/log.h>

#include <game/editor/mapitems/image.h>

#include <algorithm>
#include <array>
#include <exception>
#include <vector>

CQuadArt::CQuadArt(CQuadArtParameters Parameters, CImageInfo &&Img) :
	m_Parameters(Parameters), m_Img(std::move(Img))
{
}

CQuadArt::~CQuadArt()
{
	m_Img.Free();
}

ivec2 CQuadArt::GetOptimizedQuadSize(const ColorRGBA &Pixel, const ivec2 &Pos)
{
	ivec2 OptimizedSize(0, 0);
	ivec2 Size(0, 0);
	size_t ImgPixelSize = m_Parameters.m_ImagePixelSize;

	while(IsPixelOptimizable(Pos + Size, Pixel))
	{
		while(IsPixelOptimizable(Pos + Size, Pixel) && (!OptimizedSize.y || Size.y < OptimizedSize.y))
			Size.y += ImgPixelSize;

		if(!OptimizedSize.y || Size.y < OptimizedSize.y)
			OptimizedSize.y = Size.y;

		Size.y = 0;
		Size.x += ImgPixelSize;
		OptimizedSize.x = Size.x;
	}

	quad_art::MarkVisitedPixels(m_vVisitedPixels, m_Img, Pos, OptimizedSize);
	Size = OptimizedSize / ImgPixelSize;
	return Size;
}

size_t CQuadArt::FindSuperPixelSize(const ColorRGBA &Pixel, const ivec2 &Pos, const size_t CurrentSize)
{
	ivec2 Size(CurrentSize, CurrentSize);
	if(Pos.x + CurrentSize >= m_Img.m_Width || Pos.y + CurrentSize >= m_Img.m_Height)
	{
		quad_art::MarkVisitedPixels(m_vVisitedPixels, m_Img, Pos, Size);
		return CurrentSize;
	}

	for(int i = 0; i < 2; i++)
	{
		for(size_t j = 0; j < CurrentSize + 1; j++)
		{
			ivec2 CurrentPos = Pos;
			CurrentPos.x += i == 0 ? j : CurrentSize;
			CurrentPos.y += i == 0 ? CurrentSize : j;

			ColorRGBA CheckPixel = GetPixelClamped(CurrentPos);
			if(CurrentPos.x >= (int)m_Img.m_Width || CurrentPos.y >= (int)m_Img.m_Height || Pixel != CheckPixel)
			{
				quad_art::MarkVisitedPixels(m_vVisitedPixels, m_Img, Pos, Size);
				return CurrentSize;
			}
		}
	}

	return FindSuperPixelSize(Pixel, Pos, CurrentSize + 1);
}

ColorRGBA CQuadArt::GetPixelClamped(const ivec2 &Pos) const
{
	size_t x = std::clamp<size_t>(Pos.x, 0, m_Img.m_Width - 1);
	size_t y = std::clamp<size_t>(Pos.y, 0, m_Img.m_Height - 1);
	return m_Img.PixelColor(x, y);
}

bool CQuadArt::IsPixelOptimizable(const ivec2 &Pos, const ColorRGBA &Pixel) const
{
	if(Pos.x >= (int)m_Img.m_Width || Pos.y >= (int)m_Img.m_Height)
		return false;
	ColorRGBA CheckPixel = m_Img.PixelColor(Pos.x, Pos.y);
	return !m_vVisitedPixels[Pos.x + Pos.y * m_Img.m_Width] && CheckPixel.a > 0 && Pixel == CheckPixel;
}

CQuad CQuadArt::CreateNewQuad(const ivec2 &Pos, const ivec2 &Size, const ColorRGBA &Color) const
{
	CQuad Quad;
	Quad.m_PosEnv = Quad.m_ColorEnv = -1;
	Quad.m_PosEnvOffset = Quad.m_ColorEnvOffset = 0;
	const int X = i2fx(Pos.x);
	const int Y = i2fx(Pos.y);
	const int Width = i2fx(Size.x);
	const int Height = i2fx(Size.y);

	for(int i = 0; i < 2; ++i)
	{
		Quad.m_aPoints[i].y = Y;
		Quad.m_aPoints[i + 2].y = Y + Height;
		Quad.m_aPoints[i * 2].x = X;
		Quad.m_aPoints[i * 2 + 1].x = X + Width;
	}

	for(auto &QuadColor : Quad.m_aColors)
	{
		QuadColor.r = (int)(Color.r * 255);
		QuadColor.g = (int)(Color.g * 255);
		QuadColor.b = (int)(Color.b * 255);
		QuadColor.a = (int)(Color.a * 255);
	}

	Quad.m_aPoints[4].x = m_Parameters.m_Centralize ? i2fx(-1) : X + Width / 2;
	Quad.m_aPoints[4].y = m_Parameters.m_Centralize ? i2fx(-1) : Y + Height / 2;

	for(int i = 0; i < 4; ++i)
	{
		Quad.m_aTexcoords[i].x = i2fx(i % 2);
		Quad.m_aTexcoords[i].y = i2fx(i / 2);
	}
	return Quad;
}

bool CQuadArt::Create(std::shared_ptr<CLayerQuads> &pQuadLayer)
{
	size_t ImageBytes;
	if(m_Img.m_pData == nullptr || !m_Parameters.MaximumQuadCount(m_Img) || !m_Img.DataSize(ImageBytes))
		return false;
	if(m_Parameters.m_Optimize)
		m_vVisitedPixels.assign(m_Img.m_Height * m_Img.m_Width, false);
	pQuadLayer->m_vQuads.clear();

	size_t ImgPixelSize = m_Parameters.m_ImagePixelSize;
	ivec2 Scale(1, 1);

	for(size_t y = 0; y < m_Img.m_Height; y += ImgPixelSize)
	{
		for(size_t x = 0; x < m_Img.m_Width; x += ImgPixelSize)
		{
			ivec2 ImgPos(x, y);
			ColorRGBA Pixel = GetPixelClamped(ImgPos);
			if(Pixel.a == 0.f || (m_Parameters.m_Optimize && m_vVisitedPixels[x + y * m_Img.m_Width]))
				continue;

			if(m_Parameters.m_Optimize)
				Scale = GetOptimizedQuadSize(Pixel, ImgPos);

			ivec2 Size = Scale * m_Parameters.m_QuadPixelSize;
			ivec2 Pos(x / ImgPixelSize * m_Parameters.m_QuadPixelSize,
				y / ImgPixelSize * m_Parameters.m_QuadPixelSize);

			CQuad Quad = CreateNewQuad(Pos, Size, Pixel);
			pQuadLayer->m_vQuads.emplace_back(Quad);
		}
	}
	pQuadLayer->m_vQuads.shrink_to_fit();
	return true;
}

void CEditorMap::AddQuadArt(CImageInfo &&Image, const CQuadArtParameters &Parameters, bool IgnoreHistory)
{
	char aQuadArtName[IO_MAX_PATH_LENGTH];
	IStorage::StripPathAndExtension(Parameters.m_aFilename, aQuadArtName, sizeof(aQuadArtName));
	const size_t Width = Image.m_Width;
	const size_t Height = Image.m_Height;

	try
	{
		CQuadArt QuadArt(Parameters, std::move(Image));
		auto pLayer = std::make_shared<CLayerQuads>(this);
		if(QuadArt.Create(pLayer))
		{
			auto pGroup = std::make_shared<CLayerGroup>(this);
			str_copy(pGroup->m_aName, aQuadArtName);
			pGroup->m_UseClipping = true;
			pGroup->m_ClipX = -1;
			pGroup->m_ClipY = -1;
			pGroup->m_ClipH = std::ceil(Height * 1.f * Parameters.m_QuadPixelSize / Parameters.m_ImagePixelSize) + 2;
			pGroup->m_ClipW = std::ceil(Width * 1.f * Parameters.m_QuadPixelSize / Parameters.m_ImagePixelSize) + 2;
			str_copy(pLayer->m_aName, aQuadArtName);
			pLayer->m_Flags |= LAYERFLAG_DETAIL;
			pGroup->m_vpLayers.push_back(pLayer);

			// 历史记录成功后，把已经生成的图层组交给地图。
			m_vpGroups.reserve(m_vpGroups.size() + 1);
			if(!IgnoreHistory)
				m_EditorHistory.RecordAction(std::make_shared<CEditorActionQuadArt>(this, pGroup));
			m_vpGroups.push_back(std::move(pGroup));
			OnModify();
			return;
		}
	}
	catch(const std::exception &Error)
	{
		log_error("editor/quadart", "Generation failed: %s", Error.what());
	}

	// 调用方会关闭当前确认弹层，先关闭它才能保留接下来打开的错误提示。
	Editor()->Ui()->ClosePopupMenus();
	Editor()->ShowFileDialogError(Localize("Could not generate the quad art image.", "Editor"));
}

bool CEditor::CallbackAddQuadArt(const char *pFilepath, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;

	pEditor->m_QuadArtParameters.m_ImagePixelSize = 1;
	pEditor->m_QuadArtParameters.m_QuadPixelSize = 4;
	pEditor->m_QuadArtParameters.m_Optimize = true;
	pEditor->m_QuadArtParameters.m_Centralize = false;

	pEditor->m_QuadArtImageInfo.Free();
	if(!pEditor->Graphics()->LoadPng(pEditor->m_QuadArtImageInfo, pFilepath, StorageType))
	{
		pEditor->ShowFileDialogError(Localize("Failed to load image from file '%s'.", "Editor"), pFilepath);
		return false;
	}

	str_copy(pEditor->m_QuadArtParameters.m_aFilename, pFilepath);

	CUIRect View = *(pEditor->Ui()->Screen());

	static SPopupMenuId s_PopupQuadArtId;
	constexpr float PopupWidth = 400.0f;
	constexpr float PopupHeight = 120.0f;
	SPopupMenuProperties Props;
	Props.m_OnClosed = [pEditor] {
		// 大图确认弹层接管已读取的像素。
		if(!pEditor->m_PopupEventActivated || pEditor->m_PopupEventType != POPEVENT_QUADART_BIG_IMAGE)
			pEditor->m_QuadArtImageInfo.Free();
	};
	pEditor->Ui()->DoPopupMenu(&s_PopupQuadArtId, View.w / 2.0f - PopupWidth / 2.0f, View.h / 2.0f - PopupHeight / 2.0f, PopupWidth, PopupHeight, pEditor, PopupQuadArt, Props);
	return false;
}
