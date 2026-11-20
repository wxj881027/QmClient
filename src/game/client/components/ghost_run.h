#ifndef GAME_CLIENT_COMPONENTS_GHOST_RUN_H
#define GAME_CLIENT_COMPONENTS_GHOST_RUN_H

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

namespace ghost_run
{
	template<typename TGhost, typename FCreateSkin>
	bool PrepareSkin(TGhost &Candidate, FCreateSkin &&CreateSkin) noexcept
	{
		try
		{
			Candidate.m_pManagedTeeRenderInfo = CreateSkin(Candidate);
		}
		catch(...)
		{
			Candidate.m_pManagedTeeRenderInfo.reset();
		}
		return static_cast<bool>(Candidate.m_pManagedTeeRenderInfo);
	}

	// 从文件加载时，轨迹与皮肤都准备好后才占用槽位。
	template<typename TGhost, typename FCreateSkin>
	bool Prepare(TGhost &Target, TGhost &Candidate, FCreateSkin &&CreateSkin) noexcept
	{
		static_assert(std::is_nothrow_move_assignable_v<TGhost>);
		if(!PrepareSkin(Candidate, std::forward<FCreateSkin>(CreateSkin)))
			return false;
		Target = std::move(Candidate);
		return true;
	}

	// 菜单扩容可能失败。成功前保留旧行和槽位，下一次发布仍可使用刚完成的轨迹。
	template<typename TRows, typename TItem, typename FLess, typename FRemove>
	bool PublishRow(TRows &vRows, const TItem &Item, bool SaveBest, FLess Less, FRemove RemoveOldFile)
	{
		try
		{
			if(vRows.size() == vRows.max_size())
				return false;
			vRows.reserve(vRows.size() + 1);
		}
		catch(...)
		{
			return false;
		}
		std::optional<TItem> Removed;
		if(Item.m_Own)
		{
			auto Old = std::find_if(vRows.begin(), vRows.end(), [](const TItem &Row) { return Row.m_Own; });
			if(Old != vRows.end())
			{
				if(SaveBest && (Item.HasFile() || !Old->HasFile()))
				{
					Removed = *Old;
					vRows.erase(Old);
				}
				else
				{
					Old->m_InMemoryBest = false;
					Old->m_Own = false;
					Old->m_Slot = -1;
				}
			}
		}
		const auto Position = std::find_if(vRows.begin(), vRows.end(), [&](const TItem &Row) { return Less(Item, Row); });
		vRows.insert(Position, Item);
		if(Removed && Removed->HasFile())
		{
			try
			{
				RemoveOldFile(*Removed);
			}
			catch(...)
			{
				// 清理失败时旧文件仍可找回，菜单已经收录了新最佳。
			}
		}
		return true;
	}

	template<typename TGhost, typename TItem>
	class CPersonalBest
	{
		struct CRun
		{
			TItem m_Item;
			std::optional<TGhost> m_Track;
			bool m_AutoActivate;
		};

		std::optional<CRun> m_Finished;
		std::optional<CRun> m_Unsaved;

	public:
		static_assert(std::is_nothrow_move_constructible_v<TGhost> && std::is_nothrow_move_assignable_v<TGhost>);
		static_assert(std::is_nothrow_copy_constructible_v<TItem> && std::is_nothrow_copy_assignable_v<TItem>);

		const TItem *FinishedItem() const { return m_Finished ? &m_Finished->m_Item : nullptr; }
		void ForgetFinished() noexcept { m_Finished.reset(); }
		const TItem *UnsavedItem() const { return m_Unsaved ? &m_Unsaved->m_Item : nullptr; }
		// 磁盘扫描前还原内存行，最佳标记由本次扫描统一判断。
		template<typename TRows>
		void RestoreRow(TRows &vRows) const
		{
			if(m_Unsaved)
			{
				vRows.push_back(m_Unsaved->m_Item);
				vRows.back().m_Own = false;
			}
		}

		bool ShouldActivate() const { return m_Unsaved && m_Unsaved->m_AutoActivate && m_Unsaved->m_Track; }
		void Reset() noexcept
		{
			m_Finished.reset();
			m_Unsaved.reset();
		}

		// 已完成的新成绩单独持有，菜单暂时无法扩容时旧最佳仍可播放、保存。
		void Finish(const TItem &Item, TGhost &Track) noexcept
		{
			if(!m_Finished || Item.m_Time < m_Finished->m_Item.m_Time)
				m_Finished.emplace(CRun{Item, std::move(Track), true});
		}

		template<typename FPublish, typename FClearSlot>
		bool Publish(TGhost *pSlots, int PreviousSlot, int FreeSlot, FPublish PublishRow, FClearSlot ClearSlot)
		{
			if(!m_Finished)
				return false;
			TItem Item = m_Finished->m_Item;
			Item.m_Slot = m_Finished->m_AutoActivate ? (PreviousSlot >= 0 ? PreviousSlot : FreeSlot) : -1;
			if(!PublishRow(Item))
				return false;
			if(PreviousSlot >= 0)
				ClearSlot(PreviousSlot);
			if(Item.m_Slot >= 0)
			{
				pSlots[Item.m_Slot] = std::move(*m_Finished->m_Track);
				m_Finished->m_Track.reset();
			}
			m_Finished->m_Item = Item;
			if(Item.HasFile())
				m_Unsaved.reset();
			else
				m_Unsaved = std::move(m_Finished);
			m_Finished.reset();
			return true;
		}

		void Suspend() noexcept
		{
			if(m_Finished)
				m_Finished->m_AutoActivate = false;
			if(m_Unsaved)
				m_Unsaved->m_AutoActivate = false;
		}

		void Unload(int Slot, TGhost &Track) noexcept
		{
			if(m_Unsaved && m_Unsaved->m_Item.m_Slot == Slot)
			{
				m_Unsaved->m_Track.emplace(std::move(Track));
				m_Unsaved->m_Item.m_Slot = -1;
				m_Unsaved->m_AutoActivate = false;
			}
		}

		bool Activate(TGhost *pSlots, int Slot) noexcept
		{
			if(!m_Unsaved || !m_Unsaved->m_Track || Slot < 0 || !pSlots[Slot].Empty())
				return false;
			pSlots[Slot] = std::move(*m_Unsaved->m_Track);
			m_Unsaved->m_Item.m_Slot = Slot;
			m_Unsaved->m_Track.reset();
			return true;
		}

		TGhost *UnsavedTrack(TGhost *pSlots) noexcept
		{
			if(!m_Unsaved)
				return nullptr;
			return m_Unsaved->m_Track ? &*m_Unsaved->m_Track : &pSlots[m_Unsaved->m_Item.m_Slot];
		}

		void ForgetUnsaved() noexcept { m_Unsaved.reset(); }
	};
}

#endif
