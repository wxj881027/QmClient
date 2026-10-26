#include <game/client/components/qmclient/menu_background_prepare.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>

namespace
{
	// 只替换资源分配边界，不模拟地图、菜单或交接状态。
	struct CPreparedResource
	{
		int &m_Alive;
		int m_Id;
		CPreparedResource(int &Alive, int Id) :
			m_Alive(Alive), m_Id(Id) { ++m_Alive; }
		~CPreparedResource() { --m_Alive; }
	};
	struct CResourceFactory
	{
		int m_Alive = 0;
		int m_Attempts = 0;
		int m_FailAt = 0;
		std::unique_ptr<CPreparedResource> Create()
		{
			const int Id = ++m_Attempts;
			if(Id == m_FailAt)
				throw std::bad_alloc();
			return std::make_unique<CPreparedResource>(m_Alive, Id);
		}
	};
}

TEST(MenuBackgroundPrepare, CompleteCandidatesRemainOwnedUntilCallerReleasesThem)
{
	CResourceFactory Factory;
	bool Initialised = false;
	{
		auto Create = [&Factory] { return Factory.Create(); };
		auto Prepared = PrepareMenuBackgroundResources(Create, Create, Create, Create, [&](auto &Candidate) {
			EXPECT_EQ(Factory.m_Alive, 4);
			EXPECT_EQ(Candidate.m_pMap->m_Id, 1);
			EXPECT_EQ(Candidate.m_pLayers->m_Id, 2);
			EXPECT_EQ(Candidate.m_pImages->m_Id, 3);
			EXPECT_EQ(Candidate.m_pRenderer->m_Id, 4);
			Initialised = true;
		});
		EXPECT_TRUE(Initialised);
		EXPECT_EQ(Factory.m_Alive, 4);
		EXPECT_NE(Prepared.m_pRenderer, nullptr);
	}
	EXPECT_EQ(Factory.m_Alive, 0);
}

TEST(MenuBackgroundPrepare, EveryFactoryFailureReleasesEarlierCandidatesWithoutInitialising)
{
	for(int FailAt = 1; FailAt <= 4; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		CResourceFactory Factory;
		Factory.m_FailAt = FailAt;
		int Initialisations = 0;
		auto Create = [&Factory] { return Factory.Create(); };
		EXPECT_THROW(PrepareMenuBackgroundResources(Create, Create, Create, Create, [&](auto &) { ++Initialisations; }), std::bad_alloc);
		EXPECT_EQ(Factory.m_Attempts, FailAt);
		EXPECT_EQ(Initialisations, 0);
		EXPECT_EQ(Factory.m_Alive, 0);
	}
}

TEST(MenuBackgroundPrepare, InitialisationFailureReleasesAllPreparedResources)
{
	CResourceFactory Factory;
	auto Create = [&Factory] { return Factory.Create(); };
	EXPECT_THROW(PrepareMenuBackgroundResources(Create, Create, Create, Create, [&](auto &) {
		EXPECT_EQ(Factory.m_Alive, 4);
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	EXPECT_EQ(Factory.m_Attempts, 4);
	EXPECT_EQ(Factory.m_Alive, 0);
}
