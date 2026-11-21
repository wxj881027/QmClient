#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_PREDICT_PUBLISH_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_PREDICT_PUBLISH_H

#include "practice_predict_plan.h"

#include <engine/shared/protocol.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>

// 预测状态写回：把一份 CGameWorld 与世界计划铺到渲染/预测读取面。
//
// 两条预测循环和五个快照入口（启用、延迟接入分身、进入旁观、暂停冻结、
// 复位）过去各自手写同一组赋值，漏写一处就会让镜头插值或分身预测抖一帧。
// 这里用模板接收目标槽位，生产代码传 CGameClient 适配器，行为测试传
// 记录结构；写回规则本身只在这里维护。
//
// TSink 需要提供：
//   CGameWorld &PredictedWorld();
//   CGameWorld &PrevPredictedWorld();
//   CCharacterCore &PredictedChar();
//   CCharacterCore &PredictedPrevChar();
//   CCharacterCore &ClientPredicted(int ClientId);
//   CCharacterCore &ClientPrevPredicted(int ClientId);
//   vec2 &ClientPredPos(int ClientId, int Slot);
//   int &ClientPredTick(int ClientId, int Slot);
// 预测 tick 由参数单独传入：CGameClient 的这个字段是私有的，只有友元
// CFastPractice 能写，避免模板反过来要求扩大客户端公开面。

template<typename TSink>
bool QmCopyPracticePredictionWorlds(TSink &Sink, CGameWorld &World, bool CopyWorldToPredicted)
{
	if(!CopyWorldToPredicted)
		return Sink.PrevPredictedWorld().CopyWorldClean(&World);
	CGameWorld Predicted;
	CGameWorld Previous;
	if(!Predicted.CopyWorldClean(&World) || !Previous.CopyWorldClean(&World))
		return false;
	Sink.PredictedWorld().AdoptCleanWorld(Predicted);
	Sink.PrevPredictedWorld().AdoptCleanWorld(Previous);
	return true;
}

template<typename TSink>
bool QmPublishPracticeWorldSnapshot(TSink &Sink, CGameWorld &World, int LocalClientId, int DummyClientId, int &PredictedTick)
{
	if(!QmCopyPracticePredictionWorlds(Sink, World, true))
		return false;
	if(CCharacter *pLocalChar = World.GetCharacterById(LocalClientId))
	{
		Sink.PredictedChar() = pLocalChar->GetCore();
		Sink.PredictedPrevChar() = pLocalChar->GetCore();
		Sink.ClientPredicted(LocalClientId) = pLocalChar->GetCore();
		Sink.ClientPrevPredicted(LocalClientId) = pLocalChar->GetCore();
	}
	if(DummyClientId >= 0)
	{
		if(CCharacter *pDummyChar = World.GetCharacterById(DummyClientId))
		{
			Sink.ClientPredicted(DummyClientId) = pDummyChar->GetCore();
			Sink.ClientPrevPredicted(DummyClientId) = pDummyChar->GetCore();
		}
	}
	PredictedTick = World.GameTick();
	return true;
}

template<typename TSink>
bool QmPublishPracticePredictionBeforeStep(TSink &Sink, CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, bool CopyWorldToPredicted, int LocalClientId)
{
	const SPracticePublishActions Actions = QmResolvePracticePublishActions(Plan, Tick);
	if(Actions.m_CopyPrevWorld && !QmCopyPracticePredictionWorlds(Sink, World, CopyWorldToPredicted))
		return false;
	if(Actions.m_PublishPrevRole)
	{
		if(CCharacter *pLocalChar = World.GetCharacterById(LocalClientId))
		{
			Sink.PredictedPrevChar() = pLocalChar->GetCore();
			Sink.ClientPrevPredicted(LocalClientId) = pLocalChar->GetCore();
		}
	}
	if(Actions.m_PublishPrevOthers)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = World.GetCharacterById(i))
				Sink.ClientPrevPredicted(i) = pChar->GetCore();
	}
	return true;
}

template<typename TSink>
void QmPublishPracticePredictionAfterStep(TSink &Sink, CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, int LocalClientId)
{
	const SPracticePublishActions Actions = QmResolvePracticePublishActions(Plan, Tick);
	if(Actions.m_PublishPredRole)
	{
		if(CCharacter *pLocalChar = World.GetCharacterById(LocalClientId))
		{
			Sink.PredictedChar() = pLocalChar->GetCore();
			Sink.ClientPredicted(LocalClientId) = pLocalChar->GetCore();
		}
	}
	if(Actions.m_PublishPredOthers)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = World.GetCharacterById(i))
				Sink.ClientPredicted(i) = pChar->GetCore();
	}
}

template<typename TSink>
void QmPublishPracticePredictedPositions(TSink &Sink, CGameWorld &World, int Tick)
{
	// 200 tick 环形缓冲供延迟补偿与实体位置回查读取，两条循环必须写同一索引。
	const int Slot = Tick % 200;
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(CCharacter *pChar = World.GetCharacterById(i))
		{
			Sink.ClientPredPos(i, Slot) = pChar->Core()->m_Pos;
			Sink.ClientPredTick(i, Slot) = Tick;
		}
}

#endif
