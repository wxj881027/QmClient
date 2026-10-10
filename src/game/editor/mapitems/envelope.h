#ifndef GAME_EDITOR_MAPITEMS_ENVELOPE_H
#define GAME_EDITOR_MAPITEMS_ENVELOPE_H

#include <game/map/render_map.h>
#include <game/mapitems.h>

#include <array>
#include <vector>

class CEnvelope
{
public:
	class CPoint : public CEnvPoint_runtime
	{
	public:
		int m_Channel = 0;
		bool HasChannel(int Channel) const { return m_Channel == Channel; }
	};
	std::vector<CPoint> m_vPoints;
	char m_aName[32] = "";
	bool m_Synchronized = true;

	enum class EType
	{
		POSITION,
		COLOR,
		SOUND
	};
	explicit CEnvelope(EType Type);
	explicit CEnvelope(int NumChannels);

	std::pair<float, float> GetValueRange(int ChannelMask);
	void Eval(float Time, ColorRGBA &Result, size_t Channels, bool Loop = true) const;
	void AddPoint(CFixedTime Time, std::array<int, CEnvPoint::MAX_CHANNELS> aValues, int ChannelMask = 0xf);
	void ImportPoints(const std::vector<CEnvPoint_runtime> &vPoints);
	std::vector<CEnvPoint_runtime> ExportPoints() const;
	std::vector<int> SerializeChannels(const std::vector<CEnvPoint_runtime> &vRuntimePoints) const;
	bool DeserializeChannels(const int *pData, size_t NumInts, const std::vector<CEnvPoint_runtime> &vRuntimePoints);
	int PreviousPoint(int Index, int Channel) const;
	int NextPoint(int Index, int Channel) const;
	CFixedTime ClampPointTime(int Index, int Channel, CFixedTime Time) const;
	float EndTime() const;
	int GetChannels() const;
	EType Type() const { return m_Type; }

private:
	void Resort();

	EType m_Type;
};

#endif
