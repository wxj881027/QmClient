#include "auto_map.h"

#include <base/log.h>

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/map.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

// Based on triple32inc from https://github.com/skeeto/hash-prospector/tree/79a6074062a84907df6e45b756134b74e2956760
static uint32_t HashUInt32(uint32_t Num)
{
	Num++;
	Num ^= Num >> 17;
	Num *= 0xed5ad4bbu;
	Num ^= Num >> 11;
	Num *= 0xac4c1b51u;
	Num ^= Num >> 15;
	Num *= 0x31848babu;
	Num ^= Num >> 14;
	return Num;
}

#define HASH_MAX 65536

static const char *NextWord(const char *pCursor, char *pBuf, int BufSize)
{
	return str_next_token(pCursor, " \t\r\n", pBuf, BufSize);
}

static const char *NextInt(const char *pCursor, int &Value)
{
	char aBuf[128] = "";
	const char *pNext = NextWord(pCursor, aBuf, sizeof(aBuf));
	if(!pNext || (*pNext != '\0' && !str_isspace(*pNext)) || !str_toint(aBuf, &Value))
		return nullptr;
	return pNext;
}

[[maybe_unused]] static const char *NextFloat(const char *pCursor, float &Value)
{
	char aBuf[128] = "";
	const char *pNext = NextWord(pCursor, aBuf, sizeof(aBuf));
	if(!pNext || !str_tofloat(aBuf, &Value))
		return nullptr;
	return pNext;
}

static bool ParseFloatWithPercentSuffix(const char *pWord, float &Value, char &Specifier)
{
	char *pEnd = nullptr;
	Value = std::strtof(pWord, &pEnd);
	if(pEnd == pWord)
		return false;
	Specifier = *pEnd;
	return *pEnd == '\0' || *pEnd == '%';
}

static int HashLocation(uint32_t Seed, uint32_t Run, uint32_t Rule, uint32_t X, uint32_t Y)
{
	const uint32_t Prime = 31;
	uint32_t Hash = 1;
	Hash = Hash * Prime + HashUInt32(Seed);
	Hash = Hash * Prime + HashUInt32(Run);
	Hash = Hash * Prime + HashUInt32(Rule);
	Hash = Hash * Prime + HashUInt32(X);
	Hash = Hash * Prime + HashUInt32(Y);
	Hash = HashUInt32(Hash * Prime); // Just to double-check that values are well-distributed
	return Hash % HASH_MAX;
}

CAutomapper::CAutomapper(CEditorMap *pMap) :
	CMapObject(pMap)
{
}

void CAutomapper::Load(const char *pTileName)
try
{
	Unload();

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "editor/automap/%s.rules", pTileName);
	if(!Storage()->FileExists(aPath, IStorage::TYPE_ALL))
	{
		return; // Avoid error message if no rules exist
	}

	CLineReader LineReader;
	if(!LineReader.OpenFile(Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		log_error("editor/automap", "Failed to load rules from '%s'", aPath);
		return;
	}

	std::vector<CConfiguration> vConfigs;
	CConfiguration *pCurrentConf = nullptr;
	CRun *pCurrentRun = nullptr;
	CIndexRule *pCurrentIndex = nullptr;

	// read each line
	while(const char *pLine = LineReader.Get())
	{
		// skip blank/empty lines as well as comments
		if(str_length(pLine) > 0 && pLine[0] != '#' && pLine[0] != '\n' && pLine[0] != '\r' && pLine[0] != '\t' && pLine[0] != '\v' && pLine[0] != ' ')
		{
			if(pLine[0] == '[')
			{
				pCurrentIndex = nullptr;
				pLine++;
				pCurrentConf = &vConfigs.emplace_back();
				str_truncate(pCurrentConf->m_aName, sizeof(pCurrentConf->m_aName), pLine, maximum(0, str_length(pLine) - 1));

				pCurrentRun = &pCurrentConf->m_vRuns.emplace_back();
				pCurrentRun->m_AutomapCopy = true;
			}
			else if(str_startswith(pLine, "NewRun") && pCurrentConf)
			{
				pCurrentIndex = nullptr;
				pCurrentRun = &pCurrentConf->m_vRuns.emplace_back();
				pCurrentRun->m_AutomapCopy = true;
			}
			else if(str_startswith(pLine, "Index") && pCurrentRun)
			{
				// new index
				pCurrentIndex = nullptr;
				CIndexRule NewIndexRule{};

				char aOrientation1[128] = "";
				char aOrientation2[128] = "";
				char aOrientation3[128] = "";

				const char *pIndexCursor = str_trim_words(pLine, 1);
				pIndexCursor = NextInt(pIndexCursor, NewIndexRule.m_Id);
				if(!pIndexCursor)
					continue;
				pIndexCursor = NextWord(pIndexCursor, aOrientation1, sizeof(aOrientation1));
				if(pIndexCursor)
					pIndexCursor = NextWord(pIndexCursor, aOrientation2, sizeof(aOrientation2));
				if(pIndexCursor)
					NextWord(pIndexCursor, aOrientation3, sizeof(aOrientation3));

				NewIndexRule.m_Flag = 0;
				NewIndexRule.m_RandomProbability = 1.0f;
				NewIndexRule.m_DefaultRule = true;
				NewIndexRule.m_SkipEmpty = false;
				NewIndexRule.m_SkipFull = false;

				if(str_length(aOrientation1) > 0)
					NewIndexRule.m_Flag = CheckIndexFlag(NewIndexRule.m_Flag, aOrientation1, false);

				if(str_length(aOrientation2) > 0)
					NewIndexRule.m_Flag = CheckIndexFlag(NewIndexRule.m_Flag, aOrientation2, false);

				if(str_length(aOrientation3) > 0)
					NewIndexRule.m_Flag = CheckIndexFlag(NewIndexRule.m_Flag, aOrientation3, false);

				// add the index rule object and make it current
				pCurrentRun->m_vIndexRules.push_back(std::move(NewIndexRule));
				pCurrentIndex = &pCurrentRun->m_vIndexRules.back();
			}
			else if(str_startswith(pLine, "Pos") && pCurrentIndex)
			{
				int x = 0, y = 0;
				char aValue[128] = "";
				int Value = CPosRule::NORULE;
				std::vector<CIndexInfo> vNewIndexList;

				const char *pPosCursor = str_trim_words(pLine, 1);
				pPosCursor = NextInt(pPosCursor, x);
				if(pPosCursor)
					pPosCursor = NextInt(pPosCursor, y);
				if(!pPosCursor)
					continue;
				NextWord(pPosCursor, aValue, sizeof(aValue));

				if(!str_comp(aValue, "EMPTY"))
				{
					Value = CPosRule::INDEX;
					CIndexInfo NewIndexInfo = {0, 0, false};
					vNewIndexList.push_back(NewIndexInfo);
				}
				else if(!str_comp(aValue, "FULL"))
				{
					Value = CPosRule::NOTINDEX;
					CIndexInfo NewIndexInfo1 = {0, 0, false};
					// CIndexInfo NewIndexInfo2 = {-1, 0};
					vNewIndexList.push_back(NewIndexInfo1);
					// vNewIndexList.push_back(NewIndexInfo2);
				}
				else if(!str_comp(aValue, "INDEX") || !str_comp(aValue, "NOTINDEX"))
				{
					if(!str_comp(aValue, "INDEX"))
						Value = CPosRule::INDEX;
					else
						Value = CPosRule::NOTINDEX;

					const char *pIndexStart = str_trim_words(pLine, 4);
					while(pIndexStart)
					{
						CIndexInfo NewIndexInfo{};
						const char *pOrientation = NextInt(pIndexStart, NewIndexInfo.m_Id);
						if(!pOrientation)
							break;

						const char *pNextIndex = nullptr;
						for(int Orientation = 0; Orientation < 4; ++Orientation)
						{
							char aOrientation[128];
							pOrientation = NextWord(pOrientation, aOrientation, sizeof(aOrientation));
							if(!pOrientation || aOrientation[0] == '\0')
								break;

							if(!str_comp(aOrientation, "OR"))
							{
								// 从当前索引继续扫描，避免每个 OR 都从行首计数。
								pNextIndex = str_trim_words(pIndexStart, Orientation + 2);
								break;
							}

							if(Orientation == 3 || (Orientation > 0 && NewIndexInfo.m_Flag == 0))
								break;

							NewIndexInfo.m_Flag = CheckIndexFlag(NewIndexInfo.m_Flag, aOrientation, Orientation == 0);
							if(Orientation == 0)
								NewIndexInfo.m_TestFlag = NewIndexInfo.m_Flag != 0 || !str_comp(aOrientation, "NONE");
						}

						vNewIndexList.push_back(NewIndexInfo);
						pIndexStart = pNextIndex;
					}
					if(vNewIndexList.empty())
						continue;
				}

				if(Value != CPosRule::NORULE)
				{
					pCurrentIndex->m_vRules.push_back({x, y, Value, std::move(vNewIndexList)});
					const CPosRule &NewPosRule = pCurrentIndex->m_vRules.back();

					pCurrentConf->m_StartX = minimum(pCurrentConf->m_StartX, NewPosRule.m_X);
					pCurrentConf->m_StartY = minimum(pCurrentConf->m_StartY, NewPosRule.m_Y);
					pCurrentConf->m_EndX = maximum(pCurrentConf->m_EndX, NewPosRule.m_X);
					pCurrentConf->m_EndY = maximum(pCurrentConf->m_EndY, NewPosRule.m_Y);

					if(x == 0 && y == 0)
					{
						for(const auto &Index : NewPosRule.m_vIndexList)
						{
							if(Index.m_Id == 0 && Value == CPosRule::INDEX)
							{
								// Skip full tiles if we have a rule "POS 0 0 INDEX 0"
								// because that forces the tile to be empty
								pCurrentIndex->m_SkipFull = true;
							}
							else if((Index.m_Id > 0 && Value == CPosRule::INDEX) || (Index.m_Id == 0 && Value == CPosRule::NOTINDEX))
							{
								// Skip empty tiles if we have a rule "POS 0 0 INDEX i" where i > 0
								// or if we have a rule "POS 0 0 NOTINDEX 0"
								pCurrentIndex->m_SkipEmpty = true;
							}
						}
					}
				}
			}
			else if(str_startswith(pLine, "Random") && pCurrentIndex)
			{
				float Value = 0.0f;
				char Specifier = ' ';
				char aValue[128] = "";
				NextWord(str_trim_words(pLine, 1), aValue, sizeof(aValue));
				if(!ParseFloatWithPercentSuffix(aValue, Value, Specifier))
					continue;

				if(Specifier == '%')
					pCurrentIndex->m_RandomProbability = Value / 100.0f;
				else if(Value == 0.0f)
					pCurrentIndex->m_RandomProbability = std::signbit(Value) ? 0.0f : 1.0f;
				else
					pCurrentIndex->m_RandomProbability = 1.0f / Value;
			}
			else if(str_startswith(pLine, "Modulo") && pCurrentIndex)
			{
				CModuloRule NewModuloRule = {};
				const char *pModuloCursor = str_trim_words(pLine, 1);
				pModuloCursor = NextInt(pModuloCursor, NewModuloRule.m_ModX);
				if(pModuloCursor)
					pModuloCursor = NextInt(pModuloCursor, NewModuloRule.m_ModY);
				if(pModuloCursor)
					pModuloCursor = NextInt(pModuloCursor, NewModuloRule.m_OffsetX);
				if(pModuloCursor)
					NextInt(pModuloCursor, NewModuloRule.m_OffsetY);
				if(NewModuloRule.m_ModX == 0)
					NewModuloRule.m_ModX = 1;
				if(NewModuloRule.m_ModY == 0)
					NewModuloRule.m_ModY = 1;
				pCurrentIndex->m_vModuloRules.push_back(NewModuloRule);
			}
			else if(str_startswith(pLine, "NoDefaultRule") && pCurrentIndex)
			{
				pCurrentIndex->m_DefaultRule = false;
			}
			else if(str_startswith(pLine, "NoLayerCopy") && pCurrentRun)
			{
				pCurrentRun->m_AutomapCopy = false;
			}
		}
	}

	// add default rule for Pos 0 0 if there is none
	for(auto &Config : vConfigs)
	{
		for(auto &Run : Config.m_vRuns)
		{
			for(auto &IndexRule : Run.m_vIndexRules)
			{
				bool Found = false;

				// Search for the exact rule "POS 0 0 INDEX 0" which corresponds to the default rule
				for(const auto &Rule : IndexRule.m_vRules)
				{
					if(Rule.m_X == 0 && Rule.m_Y == 0 && Rule.m_Value == CPosRule::INDEX)
					{
						for(const auto &Index : Rule.m_vIndexList)
						{
							if(Index.m_Id == 0)
								Found = true;
						}
						break;
					}

					if(Found)
						break;
				}

				// If the default rule was not found, and we require it, then add it
				if(!Found && IndexRule.m_DefaultRule)
				{
					std::vector<CIndexInfo> vNewIndexList;
					CIndexInfo NewIndexInfo = {0, 0, false};
					vNewIndexList.push_back(NewIndexInfo);
					CPosRule NewPosRule = {0, 0, CPosRule::NOTINDEX, vNewIndexList};
					IndexRule.m_vRules.push_back(NewPosRule);

					IndexRule.m_SkipEmpty = true;
					IndexRule.m_SkipFull = false;
				}

				if(IndexRule.m_SkipEmpty && IndexRule.m_SkipFull)
				{
					IndexRule.m_SkipEmpty = false;
					IndexRule.m_SkipFull = false;
				}
			}
		}
	}

	m_vConfigs = std::move(vConfigs);
	m_FileLoaded = !m_vConfigs.empty();
	if(m_FileLoaded)
		log_trace("editor/automap", "Loaded '%s'", aPath);
}
catch(const std::bad_alloc &)
{
	log_error("editor/automap", "Not enough memory to load rules for '%s'", pTileName);
}
catch(const std::length_error &)
{
	log_error("editor/automap", "Too many automapper rules for '%s'", pTileName);
}

void CAutomapper::Unload()
{
	m_FileLoaded = false;
	m_vConfigs.clear();
}

int CAutomapper::CheckIndexFlag(int Flag, const char *pFlag, bool CheckNone) const
{
	if(!str_comp(pFlag, "XFLIP"))
		Flag |= TILEFLAG_XFLIP;
	else if(!str_comp(pFlag, "YFLIP"))
		Flag |= TILEFLAG_YFLIP;
	else if(!str_comp(pFlag, "ROTATE"))
		Flag |= TILEFLAG_ROTATE;
	else if(!str_comp(pFlag, "NONE") && CheckNone)
		Flag = 0;

	return Flag;
}

const char *CAutomapper::GetConfigName(int Index) const
{
	if(Index < 0 || Index >= (int)m_vConfigs.size())
	{
		return "(unknown)";
	}
	return m_vConfigs[Index].m_aName;
}

static bool CanIndexTileLayer(const CLayerTiles &Layer)
{
	return Layer.m_Width > 0 && Layer.m_Height > 0 && Layer.m_Width <= std::numeric_limits<int>::max() / Layer.m_Height;
}

static bool CanReadAutomapperReference(const CLayerTiles *pGameLayer, int ReferenceId)
{
	return ReferenceId < 0 || (ReferenceId < static_cast<int>(std::size(AUTOMAP_REFERENCE_NAMES)) && pGameLayer && CanIndexTileLayer(*pGameLayer));
}

static int ClampAutomapperCoordinate(int64_t Position, int LayerSize)
{
	return static_cast<int>(std::clamp<int64_t>(Position, 0, LayerSize));
}

void CAutomapper::ProceedLocalized(CLayerTiles *pLayer, CLayerTiles *pGameLayer, int ReferenceId, int ConfigId, int Seed, int X, int Y, int Width, int Height)
try
{
	if(!m_FileLoaded || pLayer->m_Readonly || pLayer == pGameLayer || ConfigId < 0 || ConfigId >= (int)m_vConfigs.size() ||
		!CanIndexTileLayer(*pLayer) || !CanReadAutomapperReference(pGameLayer, ReferenceId))
		return;

	const CConfiguration &Conf = m_vConfigs[ConfigId];
	const int64_t FromX = X;
	const int64_t FromY = Y;
	const int64_t ToX = FromX + (Width < 0 ? pLayer->m_Width : Width);
	const int64_t ToY = FromY + (Height < 0 ? pLayer->m_Height : Height);

	const int CommitFromX = ClampAutomapperCoordinate(FromX + Conf.m_StartX, pLayer->m_Width);
	const int CommitFromY = ClampAutomapperCoordinate(FromY + Conf.m_StartY, pLayer->m_Height);
	const int CommitToX = ClampAutomapperCoordinate(ToX + Conf.m_EndX, pLayer->m_Width);
	const int CommitToY = ClampAutomapperCoordinate(ToY + Conf.m_EndY, pLayer->m_Height);
	if(CommitFromX >= CommitToX || CommitFromY >= CommitToY)
		return;

	const int UpdateFromX = ClampAutomapperCoordinate(FromX + 3LL * Conf.m_StartX, pLayer->m_Width);
	const int UpdateFromY = ClampAutomapperCoordinate(FromY + 3LL * Conf.m_StartY, pLayer->m_Height);
	const int UpdateToX = ClampAutomapperCoordinate(ToX + 3LL * Conf.m_EndX, pLayer->m_Width);
	const int UpdateToY = ClampAutomapperCoordinate(ToY + 3LL * Conf.m_EndY, pLayer->m_Height);

	const int UpdateWidth = UpdateToX - UpdateFromX;
	const int UpdateHeight = UpdateToY - UpdateFromY;
	CLayerTiles UpdateLayer(pLayer->Map(), UpdateWidth, UpdateHeight);
	std::unique_ptr<CLayerTiles> pUpdateGame;
	if(ReferenceId >= 0)
		pUpdateGame = std::make_unique<CLayerTiles>(pLayer->Map(), UpdateWidth, UpdateHeight);

	for(int y = UpdateFromY; y < UpdateToY; y++)
	{
		for(int x = UpdateFromX; x < UpdateToX; x++)
		{
			const int UpdateIndex = (y - UpdateFromY) * UpdateWidth + (x - UpdateFromX);
			const CTile &InLayer = pLayer->m_pTiles[y * pLayer->m_Width + x];
			CTile &OutLayer = UpdateLayer.m_pTiles[UpdateIndex];
			OutLayer.m_Index = InLayer.m_Index;
			OutLayer.m_Flags = InLayer.m_Flags;

			if(!pUpdateGame || x >= pGameLayer->m_Width || y >= pGameLayer->m_Height)
				continue;

			const CTile &InGame = pGameLayer->m_pTiles[y * pGameLayer->m_Width + x];
			CTile &OutGame = pUpdateGame->m_pTiles[UpdateIndex];
			OutGame.m_Index = InGame.m_Index;
			OutGame.m_Flags = InGame.m_Flags;
		}
	}

	Proceed(&UpdateLayer, pUpdateGame.get(), ReferenceId, ConfigId, Seed, UpdateFromX, UpdateFromY);
	if(UpdateLayer.m_TilesHistory.empty())
		return;

	EditorTileStateChangeHistory<STileStateChange> Changes;
	for(const auto &[LocalY, Row] : UpdateLayer.m_TilesHistory)
	{
		const int y = LocalY + UpdateFromY;
		if(y < CommitFromY || y >= CommitToY)
			continue;

		for(const auto &[LocalX, Change] : Row)
		{
			const int x = LocalX + UpdateFromX;
			if(x < CommitFromX || x >= CommitToX)
				continue;

			const CTile &Previous = pLayer->m_pTiles[y * pLayer->m_Width + x];
			if(Previous.m_Index == Change.m_Current.m_Index && Previous.m_Flags == Change.m_Current.m_Flags)
				continue;

			CTile Updated = Previous;
			Updated.m_Index = Change.m_Current.m_Index;
			Updated.m_Flags = Change.m_Current.m_Flags;
			Changes[y].emplace(x, STileStateChange{true, Previous, Updated});
		}
	}

	// 提前分配撤销记录，提交时沿用本次笔画已有的初始瓦片。
	for(auto &[y, Row] : Changes)
	{
		for(const auto &[x, Change] : Row)
			pLayer->m_pTiles[y * pLayer->m_Width + x] = Change.m_Current;

		const auto ExistingRow = pLayer->m_TilesHistory.find(y);
		if(ExistingRow == pLayer->m_TilesHistory.end())
			continue;

		ExistingRow->second.merge(Row);
		for(const auto &[x, Change] : Row)
			pLayer->RecordStateChange(x, y, Change.m_Previous, Change.m_Current);
	}
	pLayer->m_TilesHistory.merge(Changes);
}
catch(const std::bad_alloc &)
{
	log_error("editor/automap", "Not enough memory for a localized automapping update");
}

class CAutomapperHistoryRollback
{
	CLayerTiles &m_Layer;
	EditorTileStateChangeHistory<STileStateChange> m_PreviousHistory;
	bool m_Committed = false;

public:
	explicit CAutomapperHistoryRollback(CLayerTiles &Layer) :
		m_Layer(Layer)
	{
		m_Layer.m_TilesHistory.swap(m_PreviousHistory);
	}

	CAutomapperHistoryRollback(const CAutomapperHistoryRollback &) = delete;
	CAutomapperHistoryRollback &operator=(const CAutomapperHistoryRollback &) = delete;

	~CAutomapperHistoryRollback()
	{
		if(m_Committed)
			return;

		for(const auto &[y, Row] : m_Layer.m_TilesHistory)
		{
			for(const auto &[x, Change] : Row)
				m_Layer.m_pTiles[y * m_Layer.m_Width + x] = Change.m_Previous;
		}
		m_Layer.m_TilesHistory.swap(m_PreviousHistory);
	}

	void Commit() { m_Committed = true; }
};

void CAutomapper::Proceed(CLayerTiles *pLayer, CLayerTiles *pGameLayer, int ReferenceId, int ConfigId, int Seed, int SeedOffsetX, int SeedOffsetY)
try
{
	if(!m_FileLoaded || pLayer->m_Readonly || ConfigId < 0 || ConfigId >= (int)m_vConfigs.size() ||
		!CanIndexTileLayer(*pLayer) || !CanReadAutomapperReference(pGameLayer, ReferenceId))
		return;

	const CConfiguration &Conf = m_vConfigs[ConfigId];
	const int LayerWidth = pLayer->m_Width;
	const int LayerHeight = pLayer->m_Height;

	std::unique_ptr<CLayerTiles> pReadCopy;
	if(std::any_of(Conf.m_vRuns.begin(), Conf.m_vRuns.end(), [](const CRun &Run) { return Run.m_AutomapCopy; }))
		pReadCopy = std::make_unique<CLayerTiles>(pLayer->Map(), LayerWidth, LayerHeight);

	CAutomapperHistoryRollback HistoryRollback(*pLayer);
	pLayer->ClearHistory();

	if(Seed == 0)
		Seed = rand();

	static const int s_aTileIndex[] = {TILE_SOLID, TILE_DEATH, TILE_NOHOOK, TILE_FREEZE, TILE_UNFREEZE, TILE_DFREEZE, TILE_DUNFREEZE, TILE_LFREEZE, TILE_LUNFREEZE};

	static_assert(std::size(AUTOMAP_REFERENCE_NAMES) == std::size(s_aTileIndex) + 1, "AUTOMAP_REFERENCE_NAMES and s_aTileIndex must include the same items");

	// for every run: copy tiles, automap, overwrite tiles
	for(size_t h = 0; h < Conf.m_vRuns.size(); ++h)
	{
		const CRun &Run = Conf.m_vRuns[h];
		const bool IsFilterable = h == 0 && ReferenceId >= 0;

		// don't make copy if it's requested
		const CLayerTiles *pBuffer = IsFilterable ? pGameLayer : pLayer;
		const CLayerTiles *pReadLayer = pBuffer;
		if(Run.m_AutomapCopy)
		{
			pReadLayer = pReadCopy.get();

			const int LoopWidth = std::min(pBuffer->m_Width, LayerWidth);
			const int LoopHeight = std::min(pBuffer->m_Height, LayerHeight);

			for(int y = 0; y < LoopHeight; y++)
			{
				for(int x = 0; x < LoopWidth; x++)
				{
					const CTile *pIn = &pBuffer->m_pTiles[y * pBuffer->m_Width + x];
					CTile *pOut = &pReadCopy->m_pTiles[y * LayerWidth + x];
					if(h == 0 && ReferenceId >= 1 && pIn->m_Index != s_aTileIndex[ReferenceId - 1])
						pOut->m_Index = 0;
					else
						pOut->m_Index = pIn->m_Index;
					pOut->m_Flags = pIn->m_Flags;
				}
			}
		}

		// auto map
		for(int y = 0; y < LayerHeight; y++)
		{
			const int64_t AbsoluteY = static_cast<int64_t>(y) + SeedOffsetY;
			for(int x = 0; x < LayerWidth; x++)
			{
				const int64_t AbsoluteX = static_cast<int64_t>(x) + SeedOffsetX;
				CTile &Tile = pLayer->m_pTiles[y * LayerWidth + x];
				const CTile &ReadTile = EditorAutomap::ReadTile(pReadLayer->m_pTiles, pReadLayer->m_Width, pReadLayer->m_Height, x, y);
				const auto ApplyTile = [&](unsigned char Index, unsigned char Flags) {
					if(Tile.m_Index == Index && Tile.m_Flags == Flags)
						return;

					CTile Updated = Tile;
					Updated.m_Index = Index;
					Updated.m_Flags = Flags;
					pLayer->RecordStateChange(x, y, Tile, Updated);
					Tile = Updated;
				};

				for(size_t i = 0; i < Run.m_vIndexRules.size(); ++i)
				{
					const CIndexRule *pIndexRule = &Run.m_vIndexRules[i];
					if(ReadTile.m_Index == 0)
					{
						if(Tile.m_Index != 0 && IsFilterable) // TODO: This is a lazy workaround
						{
							ApplyTile(0, pIndexRule->m_Flag);
							continue;
						}

						if(pIndexRule->m_SkipEmpty) // skip empty tiles
							continue;
					}
					if(pIndexRule->m_SkipFull && ReadTile.m_Index != 0) // skip full tiles
						continue;

					bool RespectRules = true;
					for(size_t j = 0; j < pIndexRule->m_vRules.size() && RespectRules; ++j)
					{
						const CPosRule *pRule = &pIndexRule->m_vRules[j];

						int CheckIndex, CheckFlags;
						const int64_t CheckX = static_cast<int64_t>(x) + pRule->m_X;
						const int64_t CheckY = static_cast<int64_t>(y) + pRule->m_Y;
						if(CheckX >= 0 && CheckX < LayerWidth && CheckY >= 0 && CheckY < LayerHeight)
						{
							const CTile &CheckTile = EditorAutomap::ReadTile(pReadLayer->m_pTiles, pReadLayer->m_Width, pReadLayer->m_Height,
								static_cast<int>(CheckX), static_cast<int>(CheckY));
							CheckIndex = CheckTile.m_Index;
							CheckFlags = CheckTile.m_Flags & (TILEFLAG_ROTATE | TILEFLAG_XFLIP | TILEFLAG_YFLIP);
						}
						else
						{
							CheckIndex = -1;
							CheckFlags = 0;
						}

						if(pRule->m_Value == CPosRule::INDEX)
						{
							RespectRules = false;
							for(const auto &Index : pRule->m_vIndexList)
							{
								if(CheckIndex == Index.m_Id && (!Index.m_TestFlag || CheckFlags == Index.m_Flag))
								{
									RespectRules = true;
									break;
								}
							}
						}
						else if(pRule->m_Value == CPosRule::NOTINDEX)
						{
							for(const auto &Index : pRule->m_vIndexList)
							{
								if(CheckIndex == Index.m_Id && (!Index.m_TestFlag || CheckFlags == Index.m_Flag))
								{
									RespectRules = false;
									break;
								}
							}
						}
					}

					bool PassesModuloCheck;
					if(pIndexRule->m_vModuloRules.empty())
						PassesModuloCheck = true;
					else
						PassesModuloCheck = std::any_of(pIndexRule->m_vModuloRules.cbegin(), pIndexRule->m_vModuloRules.cend(), [&](const CModuloRule &ModuloRule) {
							return (AbsoluteX + ModuloRule.m_OffsetX) % ModuloRule.m_ModX == 0 && (AbsoluteY + ModuloRule.m_OffsetY) % ModuloRule.m_ModY == 0;
						});

					if(RespectRules && PassesModuloCheck &&
						(pIndexRule->m_RandomProbability >= 1.0f ||
							HashLocation(Seed, h, i, static_cast<uint32_t>(AbsoluteX), static_cast<uint32_t>(AbsoluteY)) < HASH_MAX * pIndexRule->m_RandomProbability))
					{
						ApplyTile(pIndexRule->m_Id, pIndexRule->m_Flag);
					}
				}
			}
		}
	}

	HistoryRollback.Commit();
	pLayer->Map()->OnModify();
}
catch(const std::bad_alloc &)
{
	log_error("editor/automap", "Not enough memory to run the automapper");
}
