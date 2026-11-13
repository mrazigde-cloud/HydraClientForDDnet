/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_TRAILS_H
#define GAME_CLIENT_COMPONENTS_TRAILS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <deque>

class CTrails : public CComponent
{
	struct STrailPoint
	{
		vec2 m_Pos;
		vec2 m_Dir;
		ColorRGBA m_Color;
		int64_t m_Time;
	};

	std::deque<STrailPoint> m_aTrailPoints[MAX_CLIENTS];
	vec2 m_aLastPos[MAX_CLIENTS];
	int m_aLastGroup[MAX_CLIENTS];

	static ColorRGBA GenerateRandomColor();

public:
	CTrails();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
};

#endif