/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_WEATHER_H
#define GAME_CLIENT_COMPONENTS_WEATHER_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

class CWeather : public CComponent
{
	struct SSnowflake
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Size;
		float m_Phase;
		float m_Rotation;      // текущий угол спрайта
		float m_RotationSpeed; // угловая скорость (rad/s)
		float m_Alpha;
		float m_TargetAlpha;
		bool m_InteractsWithTiles;
		bool m_IgnoreCollisionUntilFree; // спавн попал в тайл (например, потолок над камерой)
		bool m_Landed;
		float m_LandTime;
		float m_MaxLandTime;
	};

	std::vector<SSnowflake> m_vSnowflakes;
	bool m_WasEnabled = false;

	void ResetFlakes();
	void SpawnFlake(SSnowflake &Flake, bool RandomizeY);
	void UpdateFlake(SSnowflake &Flake, float Dt);

public:
	CWeather();
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnWindowResize() override;
	void OnRender() override;
};

#endif