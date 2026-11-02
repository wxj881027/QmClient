#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_HOOK_INPUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_HOOK_INPUT_H

class CQmPracticeHookInput
{
	bool m_WaitForRelease = false;

public:
	void Capture(int Hook, bool PreserveHeldHook = false)
	{
		m_WaitForRelease = Hook != 0 && !PreserveHeldHook;
	}

	void Reset() { m_WaitForRelease = false; }

	int Filter(int Hook, int LiveHook)
	{
		if(LiveHook == 0)
			m_WaitForRelease = false;
		return m_WaitForRelease ? 0 : Hook;
	}
};

#endif
