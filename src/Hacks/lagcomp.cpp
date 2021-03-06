#include "lagcomp.h"

#include "../Utils/bonemaps.h"
#include "../Utils/math.h"
#include "../interfaces.h"
#include "../settings.h"

std::vector<LagComp::LagCompTickInfo> LagComp::lagCompTicks;

static float GetLerpTime()
{
	int updateRate = cvar->FindVar("cl_updaterate")->GetInt();
	ConVar *minUpdateRate = cvar->FindVar("sv_minupdaterate");
	ConVar *maxUpdateRate = cvar->FindVar("sv_maxupdaterate");

	if (minUpdateRate && maxUpdateRate)
		updateRate = maxUpdateRate->GetInt();

	float ratio = cvar->FindVar("cl_interp_ratio")->GetFloat();

	if (ratio == 0)
		ratio = 1.0f;

	float lerp = cvar->FindVar("cl_interp")->GetFloat();
	ConVar *c_min_ratio = cvar->FindVar("sv_client_min_interp_ratio");
	ConVar *c_max_ratio = cvar->FindVar("sv_client_max_interp_ratio");

	if (c_min_ratio && c_max_ratio && c_min_ratio->GetFloat() != 1)
		ratio = std::clamp(ratio, c_min_ratio->GetFloat(), c_max_ratio->GetFloat());

	return std::max(lerp, (ratio / updateRate));
}

static bool IsTickValid(float time) // pasted from polak getting some invalid ticks need some fix
{
	float correct = 0;

	correct += GetLerpTime();
	correct = std::clamp(correct, 0.f, cvar->FindVar("sv_maxunlag")->GetFloat());

	float deltaTime = correct - (globalVars->curtime - time);

	if (fabsf(deltaTime) < 0.2f)
		return true;

	return false;
}

static void RemoveInvalidTicks()
{
	auto &records = LagComp::lagCompTicks;

	for (auto record = records.begin(); record != records.end(); record++)
	{
		if (!IsTickValid(record->simulationTime))
		{
			records.erase(record);

			if (!records.empty())
				record = records.begin();
			else
				break;
		}
	}
}

static void RegisterTicks(C_BasePlayer *localplayer)
{
	const auto curtick = LagComp::lagCompTicks.insert(LagComp::lagCompTicks.begin(), {globalVars->tickcount, globalVars->curtime});

	for (int i = 1; i < engine->GetMaxClients(); ++i)
	{
		C_BasePlayer *player = (C_BasePlayer *)entityList->GetClientEntity(i);

		if (!player || player == localplayer || player->GetDormant() || !player->GetAlive() || Entity::IsTeamMate(player, localplayer) || player->GetImmune())
			continue;

		LagComp::LagCompRecord record;

		const std::unordered_map<int, int> *modelType = BoneMaps::GetModelTypeBoneMap(player);

		record.entity = player;
		record.origin = player->GetVecOrigin();
		record.head = player->GetBonePosition((*modelType).at(BONE_HEAD));

		if (player->SetupBones(record.bone_matrix, 128, BONE_USED_BY_HITBOX, globalVars->curtime))
			curtick->records.push_back(record);
	}
}

void LagComp::CreateMove(CUserCmd *cmd)
{
	if (!Settings::LagComp::enabled)
		return;

	C_BasePlayer *localplayer = (C_BasePlayer *)entityList->GetClientEntity(engine->GetLocalPlayer());

	if (!localplayer || !localplayer->GetAlive())
		return;

	C_BaseCombatWeapon *weapon = (C_BaseCombatWeapon *)entityList->GetClientEntityFromHandle(localplayer->GetActiveWeapon());

	if (!weapon)
		return;

	RemoveInvalidTicks();
	RegisterTicks(localplayer);

	float serverTime = localplayer->GetTickBase() * globalVars->interval_per_tick;

	QAngle angle;
	engine->GetViewAngles(angle);
	QAngle rcsAngle = angle + *localplayer->GetAimPunchAngle();

	if (cmd->buttons & IN_ATTACK && weapon->GetNextPrimaryAttack() <= serverTime)
	{
		float fov = 180.0f;

		int tickcount = 0;
		bool has_target = false;

		for (auto &tick : LagComp::lagCompTicks)
		{
			for (auto &record : tick.records)
			{
				float tmpFOV = Math::GetFov(rcsAngle, Math::CalcAngle(localplayer->GetEyePosition(), record.head));

				if (tmpFOV < fov)
				{
					fov = tmpFOV;
					tickcount = tick.tickCount;
					has_target = true;
				}
			}
		}

		if (has_target)
			cmd->tick_count = tickcount;
	}
}
