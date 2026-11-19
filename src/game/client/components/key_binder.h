/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_KEY_BINDER_H
#define GAME_CLIENT_COMPONENTS_KEY_BINDER_H

#include <game/client/component.h>
#include <game/client/components/binds.h>
#include <game/client/ui.h>

#include <optional>
#include <utility>

class CKeyReaderState
{
public:
	void Start(const CButtonContainer *pReader)
	{
		m_pReader = pReader;
		m_Result.reset();
		m_ReaderSeenThisFrame = false;
	}

	bool Owns(const CButtonContainer *pReader) const { return m_pReader == pReader; }
	bool IsCapturing() const { return m_pReader != nullptr && !m_Result; }
	bool HasPendingInput() const { return m_pReader != nullptr && (!m_Result || m_Result->m_Key != KEY_ESCAPE); }

	void Capture(CBindSlot Bind) { m_Result = Bind; }

	bool Abort()
	{
		if(!HasPendingInput())
			return false;
		// 原控件可能暂时隐藏。保留取消结果供其清理，停止占用输入和返回键。
		m_Result = CBindSlot(KEY_ESCAPE, KeyModifier::NONE);
		return true;
	}

	void FinishRenderFrame()
	{
		if(!std::exchange(m_ReaderSeenThisFrame, false))
			Abort();
	}

	std::optional<CBindSlot> TakeResult(const CButtonContainer *pReader)
	{
		if(!Owns(pReader))
			return std::nullopt;
		m_ReaderSeenThisFrame = true;
		if(!m_Result)
			return std::nullopt;
		m_pReader = nullptr;
		return std::exchange(m_Result, std::nullopt);
	}

private:
	const CButtonContainer *m_pReader = nullptr;
	std::optional<CBindSlot> m_Result;
	bool m_ReaderSeenThisFrame = false;
};

// component to fetch keypresses, override all other input
class CKeyBinder : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	bool OnInput(const IInput::CEvent &Event) override;
	// 菜单组件先处理按键录入控件，随后检查发起录入的控件是否还在交互。
	void OnRender() override { m_Reader.FinishRenderFrame(); }
	void OnRelease() override { AbortPendingKey(); }
	void OnStateChange(int, int) override { AbortPendingKey(); }

	class CKeyReaderResult
	{
	public:
		CBindSlot m_Bind;
		bool m_Aborted;
	};
	CKeyReaderResult DoKeyReader(CButtonContainer *pReaderButton, CButtonContainer *pClearButton, const CUIRect *pRect, const CBindSlot &CurrentBind, bool Activate, float FontSize = -1.0f);
	bool IsActive() const;
	bool HasPendingKeyReader() const { return m_Reader.HasPendingInput(); }
	bool AbortPendingKey();

private:
	CKeyReaderState m_Reader;
};

#endif
