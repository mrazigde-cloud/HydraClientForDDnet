/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "trails.h"

#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

static int GetTrailGroup(CGameClient *pGameClient, int ClientId)
{
	// 0 = own, 1 = friend, 2 = other
	if(pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return ClientId == pGameClient->m_Snap.m_LocalClientId ? 0 : (pGameClient->m_aClients[ClientId].m_Friend ? 1 : 2);
	if(ClientId == pGameClient->m_aLocalIds[0] || ClientId == pGameClient->m_aLocalIds[1])
		return 0;
	if(pGameClient->m_aClients[ClientId].m_Friend)
		return 1;
	return 2;
}

static bool IsTrailEnabledForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwn != 0;
	case 1: return g_Config.m_ClTrailFriend != 0;
	case 2: return g_Config.m_ClTrailOther != 0;
	}
	return false;
}

static unsigned int GetTrailColorForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnColor;
	case 1: return g_Config.m_ClTrailFriendColor;
	case 2: return g_Config.m_ClTrailOtherColor;
	}
	return 0;
}

static int GetTrailColorModeForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnColorMode;
	case 1: return g_Config.m_ClTrailFriendColorMode;
	case 2: return g_Config.m_ClTrailOtherColorMode;
	}
	return 0;
}

CTrails::CTrails()
{
	OnReset();
}

void CTrails::OnReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aTrailPoints[i].clear();
		m_aLastPos[i] = vec2(0.0f, 0.0f);
		m_aLastGroup[i] = -1;
	}
}

ColorRGBA CTrails::GenerateRandomColor()
{
	const float Hue = (float)rand() / (float)RAND_MAX;
	return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, 0.5f, 1.0f));
}

void CTrails::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!g_Config.m_ClTrail)
	{
		for(auto &Trail : m_aTrailPoints)
			Trail.clear();
		return;
	}

	const int64_t Now = time();
	const int64_t Lifetime = (int64_t)g_Config.m_ClTrailLifetime * time_freq() / 1000;
	const int64_t FadeTime = std::min((int64_t)g_Config.m_ClTrailFadeTime * time_freq() / 1000, Lifetime);
	const float Spacing = (float)g_Config.m_ClTrailSpacing;
	const float Size = (float)g_Config.m_ClTrailSize;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[i];
		if(!pInfo)
		{
			m_aTrailPoints[i].clear();
			continue;
		}

		const bool Active = GameClient()->m_Snap.m_aCharacters[i].m_Active;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;

		if(!Active)
		{
			m_aTrailPoints[i].clear();
			continue;
		}

		const int Group = GetTrailGroup(GameClient(), i);

		if(m_aLastGroup[i] != Group)
		{
			m_aTrailPoints[i].clear();
			m_aLastGroup[i] = Group;
			m_aLastPos[i] = Pos;
			continue;
		}

		if(!IsTrailEnabledForGroup(Group))
		{
			m_aTrailPoints[i].clear();
			m_aLastPos[i] = Pos;
			continue;
		}

		// Sample new point if moved far enough
		const vec2 Delta = Pos - m_aLastPos[i];
		if(length(Delta) >= Spacing)
		{
			STrailPoint Point;
			Point.m_Pos = Pos;
			Point.m_Dir = normalize(Delta);
			Point.m_Time = Now;

			const int ColorMode = GetTrailColorModeForGroup(Group);
			Point.m_Color = (ColorMode == 1)
							? GenerateRandomColor()
							: color_cast<ColorRGBA>(ColorHSLA(GetTrailColorForGroup(Group)));

			m_aTrailPoints[i].push_back(Point);
			m_aLastPos[i] = Pos;
		}

		// Prune expired points
		while(!m_aTrailPoints[i].empty() && Now - m_aTrailPoints[i].front().m_Time > Lifetime)
			m_aTrailPoints[i].pop_front();

		// Render
		for(const auto &Point : m_aTrailPoints[i])
		{
			const int64_t Age = Now - Point.m_Time;
			float Alpha = 1.0f;
			if(FadeTime > 0 && Age > Lifetime - FadeTime)
			{
				const int64_t FadeAge = Age - (Lifetime - FadeTime);
				Alpha = std::clamp(1.0f - (float)FadeAge / (float)FadeTime, 0.0f, 1.0f);
			}

			ColorRGBA Color = Point.m_Color;
			Color.a = Alpha;
			Graphics()->SetColor(Color);

			Graphics()->QuadsSetRotation(atan2f(Point.m_Dir.y, Point.m_Dir.x));

			const float DashLen = Size * 2.0f;
			IGraphics::CQuadItem QuadItem(Point.m_Pos.x - DashLen / 2.0f, Point.m_Pos.y - Size / 2.0f, DashLen, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
		}
	}

	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}