// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_bind_status_hud.h"

EQmBindStatusTone QmResolveBuiltinBindStatusTone(EQmBindStatusLine Line, int Value)
{
	switch(Line)
	{
	case EQmBindStatusLine::KEY_STICKING:
		switch(Value)
		{
		case 0: return EQmBindStatusTone::OK; // Key Sticking: On
		case 1: return EQmBindStatusTone::DANGER; // Key Sticking: Off
		case 2: return EQmBindStatusTone::WARNING; // Key Sticking: Reset Self
		default: return EQmBindStatusTone::NONE; // 越界值显示 "Key Sticking: ?"，不配色
		}
	case EQmBindStatusLine::HAMMER:
		switch(Value)
		{
		case 0: return EQmBindStatusTone::OK; // Hammer: Normal
		case 1: return EQmBindStatusTone::DANGER; // Hammer: DF
		case 2: return EQmBindStatusTone::WARNING; // Hammer: HDF
		case 3: return EQmBindStatusTone::WARNING; // Hammer: Custom
		default: return EQmBindStatusTone::NONE;
		}
	case EQmBindStatusLine::DUMMY_CONTROL:
	case EQmBindStatusLine::DUMMY_COPY:
		return Value != 0 ? EQmBindStatusTone::OK : EQmBindStatusTone::DANGER;
	}
	return EQmBindStatusTone::NONE;
}
