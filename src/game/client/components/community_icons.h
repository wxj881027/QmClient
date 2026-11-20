#ifndef GAME_CLIENT_COMPONENTS_COMMUNITY_ICONS_H
#define GAME_CLIENT_COMPONENTS_COMMUNITY_ICONS_H

#include <base/hash.h>

#include <engine/graphics.h>
#include <engine/http_response_validation.h>
#include <engine/serverbrowser.h>
#include <engine/shared/jobs.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/community_icon_scan.h>
#include <game/client/components/qmclient/community_icon_store.h>
#include <game/client/ui_rect.h>

#include <deque>
#include <optional>
#include <utility>

class CCommunityIcons : public CComponentInterfaces
{
public:
	const CCommunityIcon *Find(const char *pCommunityId);
	void Render(const CCommunityIcon *pIcon, CUIRect Rect, bool Active, float AlphaMul = 1.0f);
	void Load();
	void Update();
	void Shutdown();

	static bool AcceptLoadResult(bool Decoded, IHttpRequest *pResponse, bool InfoAvailable, const CCommunity *pCommunity, const SHA256_DIGEST &Sha256)
	{
		const bool Accepted = Decoded && qm_community_icons::AcceptsResult(InfoAvailable, pCommunity, Sha256);
		if(!pResponse)
			return Accepted;

		CHttpResponseValidation Validation(*pResponse);
		return Accepted && InfoAvailable && pCommunity != nullptr && pCommunity->IconSha256().has_value() && Validation.Commit();
	}

private:
	class CAbstractCommunityIconJob
	{
	protected:
		CCommunityIcons *m_pCommunityIcons;
		char m_aCommunityId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
		char m_aPath[IO_MAX_PATH_LENGTH];
		int m_StorageType;
		bool m_Success = false;
		SHA256_DIGEST m_Sha256{};

		CAbstractCommunityIconJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, int StorageType);

	public:
		const char *CommunityId() const { return m_aCommunityId; }
		bool Success() const { return m_Success; }
		const SHA256_DIGEST &Sha256() const { return m_Sha256; }
		virtual ~CAbstractCommunityIconJob() = default;
	};

	class CCommunityIconLoadJob : public IJob, public CAbstractCommunityIconJob // NOLINT(misc-multiple-inheritance): IJob provides queue state; the shared base stores icon metadata.
	{
		CImageInfo m_ImageInfo;
		CImageInfo m_ImageInfoGrayscale;
		std::shared_ptr<IHttpRequest> m_pResponse;

	protected:
		void Run() override;

	public:
		CCommunityIconLoadJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, int StorageType, std::shared_ptr<IHttpRequest> pResponse);
		~CCommunityIconLoadJob() override;

		void DiscardImages()
		{
			m_ImageInfo.Free();
			m_ImageInfoGrayscale.Free();
		}
		std::shared_ptr<IHttpRequest> TakeResponse() { return std::exchange(m_pResponse, nullptr); }
		CImageInfo &ImageInfo() { return m_ImageInfo; }
		CImageInfo &ImageInfoGrayscale() { return m_ImageInfoGrayscale; }
	};

	class CCommunityIconDownloadJob : public CAbstractCommunityIconJob
	{
		std::shared_ptr<IHttpRequest> m_pHttpRequest;

	public:
		CCommunityIconDownloadJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, const char *pUrl, const SHA256_DIGEST &Sha256);
		std::shared_ptr<IHttpRequest> HttpRequest() { return m_pHttpRequest; }
	};

	qm_community_icons::CStore m_Icons;
	qm_community_icons::CLoadQueue<CCommunityIconLoadJob> m_CommunityIconLoadJobs;
	std::deque<std::shared_ptr<CCommunityIconDownloadJob>> m_CommunityIconDownloadJobs;
	qm_community_icons::CScan m_IconScan;
	bool m_ScanPreparationFailed = false;
	bool m_CleanupFailed = false;
	bool QueueLoadJob(const char *pCommunityId, int StorageType, std::shared_ptr<IHttpRequest> pResponse = nullptr);
	static int FileScan(const char *pName, int IsDir, int DirType, void *pUser);
	bool LoadFile(const char *pPath, int DirType, CImageInfo &Info, CImageInfo &InfoGrayscale, SHA256_DIGEST &Sha256);
	bool LoadFinish(const char *pCommunityId, CImageInfo &Info, CImageInfo &InfoGrayscale, const SHA256_DIGEST &Sha256);
};

#endif
