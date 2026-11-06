#ifndef GAME_EDITOR_MAPITEMS_MAP_SETTING_H
#define GAME_EDITOR_MAPITEMS_MAP_SETTING_H

#include <base/str.h>

// 地图设置的值对象，保留既有命令容量与截断方式。
struct CEditorMapSetting
{
	char m_aCommand[256];

	CEditorMapSetting(const char *pCommand)
	{
		str_copy(m_aCommand, pCommand);
	}
};

#endif
