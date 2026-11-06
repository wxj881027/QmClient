#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_CODE_PUBLICATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_CODE_PUBLICATION_H

#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace qm_friend_code
{
	template<typename TState>
	bool TryCreateUiState(std::shared_ptr<TState> &pState)
	{
		if(pState)
			return true;
		try
		{
			auto pCandidate = std::make_shared<TState>();
			pState = std::move(pCandidate);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	class CImportPublication
	{
	public:
		enum class EStage
		{
			IDLE,
			BROWSER_UPDATE,
			FRIENDLIST_UPDATE,
			SAVE,
			COMPLETE,
		};

	private:
		EStage m_Stage = EStage::IDLE;
		uint64_t m_LastAttemptFrame = 0;
		bool m_HasAttempted = false;

	public:
		EStage Stage() const noexcept { return m_Stage; }
		bool Pending() const noexcept { return m_Stage != EStage::IDLE && m_Stage != EStage::COMPLETE; }
		bool Saved() const noexcept { return m_Stage == EStage::COMPLETE; }

		template<typename TImport>
		bool TryCommit(bool &HasPreview, int &Replace, TImport &&Import)
		{
			if(!HasPreview || Pending())
				return false;
			// false 表示未提交；提交边界的异常不能伪装成可安全重试。
			if(!Import())
				return false;
			// 名单已经提交；先撤回旧预览，收尾失败也不能再次导入它。
			HasPreview = false;
			Replace = 0;
			m_Stage = EStage::BROWSER_UPDATE;
			m_HasAttempted = false;
			return true;
		}

		template<typename TBrowserUpdate, typename TFriendlistUpdate, typename TSave>
		bool TryPublish(uint64_t Frame, bool Interactive, TBrowserUpdate &&BrowserUpdate, TFriendlistUpdate &&FriendlistUpdate, TSave &&Save)
		{
			if(!Interactive || !Pending() || (m_HasAttempted && m_LastAttemptFrame == Frame))
				return Saved();
			m_LastAttemptFrame = Frame;
			m_HasAttempted = true;
			try
			{
				if(m_Stage == EStage::BROWSER_UPDATE)
				{
					BrowserUpdate();
					m_Stage = EStage::FRIENDLIST_UPDATE;
				}
				if(m_Stage == EStage::FRIENDLIST_UPDATE)
				{
					FriendlistUpdate();
					m_Stage = EStage::SAVE;
				}
				if(m_Stage == EStage::SAVE)
				{
					if(!Save())
						return false;
					m_Stage = EStage::COMPLETE;
				}
				return Saved();
			}
			catch(const std::bad_alloc &)
			{
				return false;
			}
			catch(const std::length_error &)
			{
				return false;
			}
		}
	};
}

#endif
