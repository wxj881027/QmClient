/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_BINDS_H
#define GAME_CLIENT_COMPONENTS_BINDS_H

#include <engine/console.h>
#include <engine/keys.h>
#include <engine/shared/qm_legacy_config.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/shortcut_held_bind.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

class IConfigManager;

namespace KeyModifier
{
	inline constexpr int32_t NONE = 0;
	inline constexpr int32_t CTRL = 1;
	inline constexpr int32_t ALT = 2;
	inline constexpr int32_t SHIFT = 3;
	inline constexpr int32_t GUI = 4;
	inline constexpr int32_t COUNT = 5;
	inline constexpr int32_t COMBINATION_COUNT = 1 << COUNT;
};

class CBindSlot
{
public:
	int m_Key;
	int m_ModifierMask;

	constexpr CBindSlot(int Key, int ModifierMask) :
		m_Key(Key),
		m_ModifierMask(ModifierMask)
	{
	}

	constexpr bool operator==(const CBindSlot &Other) const { return m_Key == Other.m_Key && m_ModifierMask == Other.m_ModifierMask; }
	constexpr bool operator!=(const CBindSlot &Other) const { return !(*this == Other); }
};

inline constexpr CBindSlot EMPTY_BIND_SLOT = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);

// 绑定存储与版本独立于客户端接口，编辑器刷新和行为测试共用生产状态。
class CBindStorage
{
	friend class CBinds;
	char *m_aapKeyBindings[KeyModifier::COMBINATION_COUNT][KEY_LAST]{};
	uint64_t m_Revision = 0;

public:
	CBindStorage() = default;
	CBindStorage(const CBindStorage &) = delete;
	CBindStorage &operator=(const CBindStorage &) = delete;
	~CBindStorage() { UnbindAll(); }
	uint64_t Revision() const { return m_Revision; }
	const char *Get(int Key, int Modifiers) const
	{
		dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid");
		dbg_assert(Modifiers >= KeyModifier::NONE && Modifiers < KeyModifier::COMBINATION_COUNT, "Modifiers invalid");
		return m_aapKeyBindings[Modifiers][Key] ? m_aapKeyBindings[Modifiers][Key] : "";
	}
	// 分配器返回的非空缓冲必须能够由 free 释放。
	bool Bind(int Key, const char *pCommand, bool FreeOnly = false, int Modifiers = KeyModifier::NONE, void *(*pAllocate)(size_t) = std::malloc)
	{
		if(FreeOnly && Get(Key, Modifiers)[0])
			return false;
		Get(Key, Modifiers);
		// 先复制后释放，允许用同一槽位的当前值覆盖自身。
		char *pCopy = nullptr;
		if(pCommand[0])
		{
			try
			{
				const std::string Migrated = QmLegacyConfig::MigrateBindCommand(pCommand);
				if(Migrated.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
					return false;
				const int Size = static_cast<int>(Migrated.size()) + 1;
				pCopy = static_cast<char *>(pAllocate(static_cast<size_t>(Size)));
				if(pCopy == nullptr)
					return false;
				str_copy(pCopy, Migrated.c_str(), Size);
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
		free(m_aapKeyBindings[Modifiers][Key]);
		m_aapKeyBindings[Modifiers][Key] = pCopy;
		++m_Revision;
		return true;
	}
	void UnbindAll()
	{
		for(auto &apBindings : m_aapKeyBindings)
			for(auto &pBinding : apBindings)
			{
				free(pBinding);
				pBinding = nullptr;
			}
		++m_Revision;
	}
};

class CBinds : public CComponent
{
	static void ConBind(IConsole::IResult *pResult, void *pUserData);
	static void ConBinds(IConsole::IResult *pResult, void *pUserData);
	static void ConUnbind(IConsole::IResult *pResult, void *pUserData);
	static void ConUnbindAll(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	CBindSlot GetBindSlot(const char *pBindString) const;
	int DetectDeepflyModeFromAllBinds() const;

	// free buffer after use
	char *GetKeyBindCommand(int ModifierCombination, int Key) const;

public:
	CBinds();
	~CBinds() override;
	int Sizeof() const override { return sizeof(*this); }

	class CBindsSpecial : public CComponent
	{
	public:
		CBinds *m_pBinds;
		int Sizeof() const override { return sizeof(*this); }
		bool OnInput(const IInput::CEvent &Event) override;
	};

	bool m_MouseOnAction;

	CBindsSpecial m_SpecialBinds;

	void Bind(int KeyId, const char *pStr, bool FreeOnly = false, int ModifierCombination = KeyModifier::NONE);
	void SetDefaults();
	void UnbindAll();
	uint64_t Revision() const { return m_Storage.Revision(); }
	const char *Get(int KeyId, int ModifierCombination) const;
	const char *Get(const CBindSlot &BindSlot) const;
	void GetKey(const char *pBindStr, char *pBuf, size_t BufSize) const;
	static int GetModifierMask(IInput *pInput);
	static int GetModifierMaskOfKey(int Key);
	static bool IsReservedShortcutChord(int ModifierMask)
	{
		return ModifierMask == ((1 << KeyModifier::CTRL) | (1 << KeyModifier::SHIFT)) ||
		       ModifierMask == ((1 << KeyModifier::ALT) | (1 << KeyModifier::SHIFT)) ||
		       ModifierMask == ((1 << KeyModifier::GUI) | (1 << KeyModifier::SHIFT));
	}
	static bool AllowsUnmodifiedFallback(int Key, int ModifierMask)
	{
		if(IsReservedShortcutChord(ModifierMask))
			return false;

		if((Key == KEY_LSHIFT || Key == KEY_RSHIFT) &&
			(ModifierMask & ((1 << KeyModifier::CTRL) | (1 << KeyModifier::ALT) | (1 << KeyModifier::GUI))) != 0)
			return false;

		return true;
	}
	static bool ShouldRestrictUnmodifiedShiftBindOnModifierPress(const CBindSlot &ActiveBind, int PressedKeyModifierMask)
	{
		return ActiveBind.m_ModifierMask == KeyModifier::NONE &&
		       (ActiveBind.m_Key == KEY_LSHIFT || ActiveBind.m_Key == KEY_RSHIFT) &&
		       (PressedKeyModifierMask == (1 << KeyModifier::CTRL) ||
			       PressedKeyModifierMask == (1 << KeyModifier::ALT) ||
			       PressedKeyModifierMask == (1 << KeyModifier::GUI));
	}
	static const char *GetModifierName(int Modifier);
	void GetKeyBindName(int Key, int ModifierMask, char *pBuf, size_t BufSize) const;
	void RefreshActiveBinds();
	static void ExecuteCommand(IConsole *pConsole, const char *pBind, bool AppendGoresPrevWeapon, int Stroke, int ClientId)
	{
		if(pBind == nullptr)
			return;
		// 命令回调可能重绑或清空原槽位，控制台解析后续分号时必须继续持有副本。
		std::string Command(pBind);
		if(AppendGoresPrevWeapon && str_find(Command.c_str(), "+fire") != nullptr && str_find(Command.c_str(), "+prevweapon") == nullptr)
			Command += ";+prevweapon";
		pConsole->ExecuteLineStroked(Stroke, Command.c_str(), ClientId);
	}
	static void RefreshCommand(IConsole *pConsole, const char *pBind, int ClientId)
	{
		if(pBind == nullptr || pBind[0] == '\0')
			return;
		const std::string Command(pBind);
		ForEachTopLevelBindCommand(Command.c_str(), [&](const char *pCommand) {
			if(pCommand[0] == '+')
				pConsole->ExecuteLineStroked(1, pCommand, ClientId);
		});
	}

	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override { m_vActiveBinds.clear(); }
	void OnRelease() override { m_vActiveBinds.clear(); }
	bool OnInput(const IInput::CEvent &Event) override;

	// DDRace

	void SetDDRaceBinds(bool FreeOnly);

private:
	CBindStorage m_Storage;
	struct CActiveBind : CBindSlot
	{
		CQmShortcutHeldBind m_ShortcutState;
		CActiveBind(int Key, int ModifierMask) noexcept :
			CBindSlot(Key, ModifierMask) {}
	};
	std::vector<CActiveBind> m_vActiveBinds;
};
#endif
