#ifndef TEST_SUPPORT_SERVERBROWSER_ENTRY_BROWSER_FIXTURE_H
#define TEST_SUPPORT_SERVERBROWSER_ENTRY_BROWSER_FIXTURE_H

#include <test/support/serverbrowser_fixture.h>

namespace ServerBrowserEntryTests
{
	class CFailureFavorites : public IFavorites
	{
		IFavorites &m_Delegate;
		void OnConfigSave(IConfigManager *) override {}

	public:
		bool m_FailFavorite = false;
		bool m_FailPing = false;
		explicit CFailureFavorites(IFavorites &Delegate) :
			m_Delegate(Delegate) {}
		TRISTATE IsFavorite(const NETADDR *pAddresses, int Count) const override
		{
			if(m_FailFavorite)
				throw std::bad_alloc();
			return m_Delegate.IsFavorite(pAddresses, Count);
		}
		TRISTATE IsPingAllowed(const NETADDR *pAddresses, int Count) const override
		{
			if(m_FailPing)
				throw 7;
			return m_Delegate.IsPingAllowed(pAddresses, Count);
		}
		void Add(const NETADDR *pAddresses, int Count) override { m_Delegate.Add(pAddresses, Count); }
		void AllowPing(const NETADDR *pAddresses, int Count, bool Allow) override { m_Delegate.AllowPing(pAddresses, Count, Allow); }
		void Remove(const NETADDR *pAddresses, int Count) override { m_Delegate.Remove(pAddresses, Count); }
		void AllEntries(const CEntry **ppEntries, int *pCount) override { m_Delegate.AllEntries(ppEntries, pCount); }
	};

	class CEntryBrowserFixture : public ServerBrowserTests::CServerBrowserFixture
	{
	protected:
		CFailureFavorites m_Favorites{*m_pFavorites};
		void SetUp() override
		{
			CServerBrowserFixture::SetUp();
			CServerBrowserTestAccess::Initialize(m_Browser, &m_Friends, &m_Favorites);
		}
	};
}

#endif
