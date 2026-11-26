// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiDocs.h"

#include "QmWikiMarkdown.h"
#include "SettingsSearchQuery.h"

#include <base/mem.h>
#include <base/system.h>

#include <engine/shared/localization.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
	// 视频导出说明：内容取自 docs/规格/2026-10-21-视频导出HUD说明文案.md，
	// 面向第一次导出视频的玩家；标题尽量复用弹窗里已有的行标签，避免同一概念两种叫法。
	const char *const s_apVideoExportIntro[] = {
		Localizable("Renders the demo that is currently playing into a video file. It runs as fast as the encoder allows instead of in real time, so a five-minute demo usually finishes in tens of seconds."),
	};
	const char *const s_apOutputResolution[] = {
		Localizable("Sets the size of the finished video; \"Follow window\" uses the current render resolution."),
		Localizable("Anything smaller than the window supersamples: the frame is drawn at window size and scaled down, so 1080p to 720p gives smoother edges. Sizes above the window are ignored."),
	};
	const char *const s_apEncoder[] = {
		Localizable("\"Default (software)\" is x264: CPU only, best quality, slowest."),
		Localizable("Entries with NVENC, AMF, Quick Sync or VideoToolbox use the GPU encoding chip and are several times faster. Only encoders that really work on this machine are listed."),
	};
	const char *const s_apQuality[] = {
		Localizable("0 to 51, lower is clearer and bigger. Leave it empty to use the video encoding CRF from the settings page."),
		Localizable("The scale name depends on the encoder (CRF, CQ, QP, ICQ) but the direction is the same. ProRes fixes quality by profile, so the field is disabled."),
	};
	const char *const s_apChromaSampling[] = {
		Localizable("How much color detail is kept, and the setting that matters most for editing. 4:2:0 is smallest and most compatible, 4:2:2 is the common editing middle ground, 4:4:4 keeps thin lines and small colored text sharp."),
		Localizable("Rendering reads back 8-bit frames, so 10-bit formats add nothing by themselves; the gain comes from chroma density. If the encoder cannot do 4:4:4, the export fails with an error instead of reducing quality silently."),
	};
	const char *const s_apContainer[] = {
		Localizable("MP4 is the most compatible for sharing, MOV is the editing standard and required by ProRes, and MKV suits lossless archives but is poorly supported by editors."),
	};
	const char *const s_apExportPreset[] = {
		Localizable("Sets encoder, container and quality in one step: Daily is H.264 + MP4, High quality is HEVC + MP4 at a smaller size, Editing is all-intra H.264 + MP4 with instant seeking, and Exchange is ProRes 422 HQ + MOV for other editors."),
	};
	const char *const s_apKeyframeInterval[] = {
		Localizable("How often a keyframe is written. Cut points can only land on keyframes: 0 uses the encoder default, 1 makes every frame a keyframe, and 60 leaves one keyframe per second at 60 fps."),
		Localizable("NVIDIA hardware encoders refuse 1 and fall back to 2, with a note in the log; use a software encoder when you need true all-intra."),
	};
	const char *const s_apExportRange[] = {
		Localizable("Optional start and end time in seconds. Leave both empty to export the whole demo."),
	};
	const char *const s_apFrequentlyAsked[] = {
		Localizable("Why is a ProRes file huge but not sharper? ProRes is an exchange format: every editor opens it and scrubbing is smooth. Its 10-bit profile carries the same 8-bit source, so raise chroma sampling for more detail."),
		Localizable("Why does scrubbing lag in my editor? The file uses a normal GOP, so the editor decodes from the previous keyframe. Re-export with the Editing preset."),
		Localizable("Why will some players not open a 4:4:4 file? H.264 4:4:4 uses the High 4:4:4 Predictive profile, which some hardware decoders and phones lack. Use 4:2:0 for wide compatibility."),
	};

	const SWikiSection s_aVideoExportSections[] = {
		{Localizable("Video export"), s_apVideoExportIntro, (int)std::size(s_apVideoExportIntro)},
		{Localizable("Output resolution"), s_apOutputResolution, (int)std::size(s_apOutputResolution)},
		{Localizable("Encoder"), s_apEncoder, (int)std::size(s_apEncoder)},
		{Localizable("Quality"), s_apQuality, (int)std::size(s_apQuality)},
		{Localizable("Chroma sampling"), s_apChromaSampling, (int)std::size(s_apChromaSampling)},
		{Localizable("Container"), s_apContainer, (int)std::size(s_apContainer)},
		{Localizable("Export preset"), s_apExportPreset, (int)std::size(s_apExportPreset)},
		{Localizable("Keyframe interval (GOP)"), s_apKeyframeInterval, (int)std::size(s_apKeyframeInterval)},
		{Localizable("Export range"), s_apExportRange, (int)std::size(s_apExportRange)},
		{Localizable("Frequently asked questions"), s_apFrequentlyAsked, (int)std::size(s_apFrequentlyAsked)},
	};

	const SWikiDocument s_VideoExportDocument = {
		"demo:video_export",
		Localizable("Video export"),
		s_aVideoExportSections,
		(int)std::size(s_aVideoExportSections),
	};

	constexpr qm_wiki_docs::SDdnetWikiArticle s_aDdnetArticles[] = {
		{"settings-and-commands", "deck:ddnet-wiki-settings-and-commands", {"Settings and Commands", "设置与命令"},
			{"Look up DDNet map settings, client and server commands, their arguments, defaults and allowed values.",
				"查找 DDNet 地图设置、客户端和服务器命令，以及参数、默认值和取值范围。"}},
		{"binds", "deck:ddnet-wiki-binds", {"Binds", "按键绑定"},
			{"Use the F1 console to bind DDNet commands to a keyboard or mouse button. Enter bind followed by a key and the command to run.",
				"在 F1 控制台输入 bind、按键和要执行的命令，就能把 DDNet 操作绑定到键盘或鼠标按键上。"}},
	};
	constexpr const char *s_apDdnetWikiAliases[][5] = {
		{"设置与指令", "控制台命令", "客户端命令", "服务器命令", "console commands"},
		{"绑定", "绑键", "bind", "key bindings", "keyboard shortcuts"},
	};
	constexpr const char *s_apDdnetWikiLanguageDirs[] = {"english", "simplified_chinese"};
	static_assert(std::size(s_aDdnetArticles) == qm_wiki_docs::NUM_DDNET_WIKI_ARTICLES);
	static_assert(std::size(s_apDdnetWikiAliases) == std::size(s_aDdnetArticles));
	static_assert(std::size(s_apDdnetWikiLanguageDirs) == qm_wiki_docs::NUM_LANGUAGES);

	void AppendSpanText(std::string &Text, const std::vector<SWikiSpan> &vSpans)
	{
		for(const SWikiSpan &Span : vSpans)
			Text += Span.m_Text;
	}
}

const SWikiDocument &qm_wiki_docs::VideoExport()
{
	return s_VideoExportDocument;
}

const qm_wiki_docs::SDdnetWikiArticle &qm_wiki_docs::DdnetWikiArticle(int Article)
{
	return s_aDdnetArticles[Article];
}

std::string qm_wiki_docs::ReadDdnetWikiArticle(IStorage &Storage, int Article, EDdnetWikiLanguage Language)
{
	char aPath[128];
	str_format(aPath, sizeof(aPath), "wiki/ddnet/%s/%s.md", s_apDdnetWikiLanguageDirs[Language], DdnetWikiArticle(Article).m_pSlug);
	char aDataPath[IO_MAX_PATH_LENGTH];
	if(!Storage.GetDataPath(aPath, aDataPath, sizeof(aDataPath)))
		return {};
	void *pData = nullptr;
	unsigned Size = 0;
	const auto Result = Storage.ReadFileCheckedBounded(aDataPath, IStorage::TYPE_ABSOLUTE, &pData, &Size, 1024 * 1024);
	const std::unique_ptr<void, decltype(&free)> Data(pData, free);
	if(Result != IStorage::EReadFileResult::SUCCESS || Size == 0 || !Data || mem_has_null(Data.get(), Size))
		return {};
	return std::string(static_cast<const char *>(Data.get()), Size);
}

std::vector<qm_wiki_docs::SDdnetWikiMatch> qm_wiki_docs::CDdnetWikiIndex::Search(const char *pQuery, IStorage *pStorage)
{
	if(pStorage == nullptr)
		return {};
	const std::string Normalized = qm_settings_search::NormalizeQuery(pQuery);
	const std::string_view Query = qm_settings_search::StripQuestion(Normalized);
	if(Query.empty() || static_cast<size_t>(str_utf8_forward(Query.data(), 0)) >= Query.size())
		return {};

	if(m_pStorage != pStorage)
	{
		m_aTranslations = {};
		m_pStorage = pStorage;
	}

	std::vector<std::string_view> vWords;
	for(size_t Start = Query.find_first_not_of(' '); Start != std::string_view::npos;)
	{
		const size_t End = Query.find(' ', Start);
		vWords.push_back(Query.substr(Start, End == std::string_view::npos ? End : End - Start));
		Start = End == std::string_view::npos ? End : Query.find_first_not_of(' ', End);
	}
	const auto PhraseScore = [Query](std::string_view Text) {
		if(Text == Query)
			return 7;
		return Text.find(Query) != std::string_view::npos ? 6 : 0;
	};

	struct SScoredMatch
	{
		SDdnetWikiMatch m_Match;
		int m_Score;
	};
	std::vector<SScoredMatch> vMatches;
	for(int Article = 0; Article < NUM_DDNET_WIKI_ARTICLES; ++Article)
	{
		SDdnetWikiMatch Match{Article, {}, {-1, -1}, {}};
		int TitleScore = Query == "wiki" || Query == "ddnet wiki" ? 1 : 0;
		for(const char *pAlias : s_apDdnetWikiAliases[Article])
			TitleScore = std::max(TitleScore, PhraseScore(pAlias));
		for(const char *pTitle : DdnetWikiArticle(Article).m_apTitles)
			TitleScore = std::max(TitleScore, PhraseScore(qm_settings_search::NormalizeQuery(pTitle)));
		int ArticleScore = TitleScore;

		for(int Language = 0; Language < NUM_LANGUAGES; ++Language)
		{
			auto &Translation = m_aTranslations[Article][Language];
			if(!Translation)
			{
				const std::string Markdown = ReadDdnetWikiArticle(*pStorage, Article, static_cast<EDdnetWikiLanguage>(Language));
				CWikiMarkdown Document;
				if(!Document.Parse(Markdown.c_str()))
					continue;
				std::vector<SSearchBlock> vBlocks;
				int BlockIndex = 0;
				for(const SWikiBlock &Block : Document.Blocks())
				{
					if(Block.m_Type == EWikiBlockType::TABLE)
					{
						for(size_t RowIndex = 1; RowIndex < Block.m_Rows.size(); ++RowIndex)
						{
							const auto &vCells = Block.m_Rows[RowIndex].m_Cells;
							const auto &vHeaders = Block.m_Rows.front().m_Cells;
							std::string Excerpt;
							std::string Key;
							for(size_t Column = 0; Column < vCells.size(); ++Column)
							{
								std::string Cell;
								AppendSpanText(Cell, vCells[Column]);
								if(Column == 0)
									Key = qm_settings_search::NormalizeQuery(Cell.c_str());
								if(Cell.empty())
									continue;
								if(!Excerpt.empty())
									Excerpt += " · ";
								if(Column > 0 && Column < vHeaders.size())
								{
									AppendSpanText(Excerpt, vHeaders[Column]);
									Excerpt += ' ';
								}
								Excerpt += Cell;
							}
							vBlocks.push_back({qm_settings_search::NormalizeQuery(Excerpt.c_str()), std::move(Excerpt), std::move(Key), BlockIndex, false});
						}
					}
					else
					{
						std::string Text = Block.m_Text;
						AppendSpanText(Text, Block.m_Spans);
						if(!Text.empty())
							vBlocks.push_back({qm_settings_search::NormalizeQuery(Text.c_str()), std::move(Text), {}, BlockIndex, Block.m_Type == EWikiBlockType::HEADING});
					}
					++BlockIndex;
				}
				Translation.emplace(std::move(vBlocks));
			}
			Match.m_aAvailable[Language] = true;
			int BlockScore = 0;
			const SSearchBlock *pBest = nullptr;
			for(const auto &Block : *Translation)
			{
				int Score = 0;
				if(Block.m_Key == Query)
					Score = 8;
				else if(Block.m_Text.find(Query) != std::string::npos)
					Score = Block.m_Heading ? 5 : 3;
				else if(vWords.size() > 1 && std::all_of(vWords.begin(), vWords.end(), [&](std::string_view Word) { return Block.m_Text.find(Word) != std::string::npos; }))
					Score = 1;
				if(Score > BlockScore)
				{
					BlockScore = Score;
					pBest = &Block;
				}
			}
			if(pBest != nullptr && BlockScore >= TitleScore)
			{
				Match.m_aBlocks[Language] = pBest->m_Block;
				const auto *pExcerpt = pBest;
				if(pBest->m_Heading && pBest + 1 != Translation->data() + Translation->size() && !(pBest + 1)->m_Heading)
					pExcerpt = pBest + 1;
				Match.m_aExcerpts[Language] = pExcerpt->m_Excerpt;
			}
			ArticleScore = std::max(ArticleScore, BlockScore);
		}
		if(ArticleScore != 0 && std::any_of(Match.m_aAvailable.begin(), Match.m_aAvailable.end(), [](bool Available) { return Available; }))
			vMatches.push_back({Match, ArticleScore});
	}
	std::stable_sort(vMatches.begin(), vMatches.end(), [](const auto &Left, const auto &Right) { return Left.m_Score > Right.m_Score; });
	std::vector<SDdnetWikiMatch> vResults;
	for(const auto &Entry : vMatches)
		vResults.push_back(Entry.m_Match);
	return vResults;
}
