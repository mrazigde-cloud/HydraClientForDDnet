/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_TRAILS_H
#define GAME_CLIENT_COMPONENTS_TRAILS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <deque>
#include <vector>
#include <string>
#include <map>

class CTrails : public CComponent
{
public:
	enum ETrailStyle
	{
		TRAIL_STYLE_DASH = 0,
		TRAIL_STYLE_RIBBON = 1,
		TRAIL_STYLE_NEON = 2,
	};

private:
	enum ETrailState
	{
		TRAIL_STATE_NONE = 0,     // no player info (disconnected)
		TRAIL_STATE_INACTIVE = 1, // player exists but is dead / spectating
		TRAIL_STATE_ACTIVE = 2,   // player is in the game
	};

	struct STrailPoint
	{
		vec2 m_Pos;
		vec2 m_Dir;
		ColorRGBA m_Color;
		int64_t m_Time;
		vec2 m_Perp; // perpendicular to m_Dir, used by ribbon style
		uint32_t m_Id; // used by neon style for wave/noise pattern
		int m_SpriteIndex = -1; // used by dash style in cycle mode
	};

	std::deque<STrailPoint> m_aTrailPoints[MAX_CLIENTS];
	vec2 m_aLastPos[MAX_CLIENTS];
	int m_aLastGroup[MAX_CLIENTS];
	uint32_t m_aNextPointId[MAX_CLIENTS];
	int m_aPrevState[MAX_CLIENTS];

	IGraphics::CTextureHandle m_DashTrailTexture;
	bool m_DashTrailTextureTried = false;
	char m_aLastDashSpriteName[64] = "";
	
	std::vector<std::string> m_vAvailableSprites;
	std::map<std::string, IGraphics::CTextureHandle> m_PreviewTextures;

	static ColorRGBA GenerateRandomColor();

public:
	CTrails();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;

	void InvalidateDashTrailTexture();
	void UpdateDashTexture();
	IGraphics::CTextureHandle GetDashTrailTexture() const { return m_DashTrailTexture; }
	void RefreshAvailableSprites();
	const std::vector<std::string> &GetAvailableSprites() const { return m_vAvailableSprites; }
	
	IGraphics::CTextureHandle GetSpriteTexture(const char *pName);
	void ClearPreviewTextures();
};

#endif