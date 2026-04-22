#ifndef GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H
#define GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H

constexpr bool IsSettingsWarmupStageReady(int CurrentStage, int RequiredStage)
{
	return CurrentStage < 0 || CurrentStage >= RequiredStage;
}

constexpr int AdvanceSettingsWarmupStage(int CurrentStage, int LastStage)
{
	if(CurrentStage < 0 || LastStage < 0)
		return -1;
	return CurrentStage < LastStage ? CurrentStage + 1 : -1;
}

#endif // GAME_CLIENT_COMPONENTS_SETTINGS_WARMUP_H
