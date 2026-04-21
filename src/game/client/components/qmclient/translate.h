#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATE_H

#include <game/client/component.h>
#include <game/client/components/chat.h>

#include <memory>
#include <optional>
#include <vector>

class CTranslate;

class ITranslateBackend
{
public:
	virtual ~ITranslateBackend() = default;
	virtual const char *EncodeTarget(const char *pTarget) const;
	virtual bool CompareTargets(const char *pA, const char *pB) const;
	virtual const char *Name() const = 0;
	virtual std::optional<bool> Update(CTranslateResponse &Out) = 0;
};

class CTranslate : public CComponent
{
	class CTranslateJob
	{
	public:
		std::unique_ptr<ITranslateBackend> m_pBackend = nullptr;
		// For chat translations
		CChat::CLine *m_pLine = nullptr;
		std::shared_ptr<CTranslateResponse> m_pTranslateResponse = nullptr;
		bool m_AutoTriggered = false;
		char m_aTarget[16] = "";
	};
	std::vector<CTranslateJob> m_vJobs;

	class COutgoingTranslateJob
	{
	public:
		std::unique_ptr<ITranslateBackend> m_pBackend = nullptr;
		CTranslateResponse m_Response;
		int m_Team = 0;
		char m_aTarget[16] = "";
	};
	std::vector<COutgoingTranslateJob> m_vOutgoingJobs;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);
	static void ConTranslateId(IConsole::IResult *pResult, void *pUserData);

public:
	static constexpr size_t MAX_TRANSLATION_JOBS = 15;

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;

	void Translate(int Id, bool ShowProgress = true);
	void Translate(const char *pName, bool ShowProgress = true);
	void Translate(CChat::CLine &Line, bool ShowProgress = true, bool AutoTriggered = false);
	bool TryTranslateOutgoingChat(int Team, const char *pText);

	void AutoTranslate(CChat::CLine &Line);
};

#endif
