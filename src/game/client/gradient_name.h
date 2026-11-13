#ifndef GAME_CLIENT_GRADIENT_NAME_H
#define GAME_CLIENT_GRADIENT_NAME_H

#include <base/color.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

enum
{
	NAME_GRADIENT_NONE = -1,
	NAME_GRADIENT_OWN = 0,
	NAME_GRADIENT_FRIEND = 1,
	NAME_GRADIENT_OTHER = 2,
};

struct SNameGradientParams
{
	int m_Mode = 1;
	int m_Speed = 30;
	bool m_Wave = true;
	ColorRGBA m_aColors[3];

	static SNameGradientParams FromConfig(int Group)
	{
		SNameGradientParams P;
		unsigned C1, C2, C3;
		switch(Group)
		{
		case NAME_GRADIENT_FRIEND:
			P.m_Mode = g_Config.m_ClNameGradientFriendMode;
			P.m_Speed = g_Config.m_ClNameGradientFriendSpeed;
			P.m_Wave = g_Config.m_ClNameGradientFriendWave != 0;
			C1 = g_Config.m_ClNameGradientFriendColor1;
			C2 = g_Config.m_ClNameGradientFriendColor2;
			C3 = g_Config.m_ClNameGradientFriendColor3;
			break;
		case NAME_GRADIENT_OTHER:
			P.m_Mode = g_Config.m_ClNameGradientOtherMode;
			P.m_Speed = g_Config.m_ClNameGradientOtherSpeed;
			P.m_Wave = g_Config.m_ClNameGradientOtherWave != 0;
			C1 = g_Config.m_ClNameGradientOtherColor1;
			C2 = g_Config.m_ClNameGradientOtherColor2;
			C3 = g_Config.m_ClNameGradientOtherColor3;
			break;
		default:
			P.m_Mode = g_Config.m_ClNameGradientOwnMode;
			P.m_Speed = g_Config.m_ClNameGradientOwnSpeed;
			P.m_Wave = g_Config.m_ClNameGradientOwnWave != 0;
			C1 = g_Config.m_ClNameGradientOwnColor1;
			C2 = g_Config.m_ClNameGradientOwnColor2;
			C3 = g_Config.m_ClNameGradientOwnColor3;
			break;
		}
		P.m_aColors[0] = color_cast<ColorRGBA>(ColorHSLA(C1));
		P.m_aColors[1] = color_cast<ColorRGBA>(ColorHSLA(C2));
		P.m_aColors[2] = color_cast<ColorRGBA>(ColorHSLA(C3));
		return P;
	}
};

inline ColorRGBA CalcGradientColor(int CharIndex, int TotalChars, int Mode, int Speed, float Time,
	ColorRGBA C1, ColorRGBA C2, ColorRGBA C3, bool Wave = true)
{
	const ColorRGBA aPalette[3] = {C1, C2, C3};
	const float Phase = Time * (float)std::clamp(Speed, 1, 100) / 60.0f;
	const float Spread = Wave ? (TotalChars > 0 ? 1.0f / (float)TotalChars : 0.0f) : (1.0f / 3.0f);
	float Pos = (float)CharIndex * Spread + Phase;
	Pos -= std::floor(Pos);
	const float Seg = Pos * 3.0f;
	const int Idx = std::min((int)Seg, 2);
	if(Mode == 0)
		return ColorRGBA(aPalette[Idx].r, aPalette[Idx].g, aPalette[Idx].b, 1.0f);
	const float F = Seg - (float)Idx;
	const ColorRGBA &A = aPalette[Idx];
	const ColorRGBA &B = aPalette[(Idx + 1) % 3];
	return ColorRGBA(A.r + (B.r - A.r) * F, A.g + (B.g - A.g) * F, A.b + (B.b - A.b) * F, 1.0f);
}

inline ColorRGBA CalcGradientColor(int CharIndex, int TotalChars, const SNameGradientParams &P, float Time)
{
	return CalcGradientColor(CharIndex, TotalChars, P.m_Mode, P.m_Speed, Time, P.m_aColors[0], P.m_aColors[1], P.m_aColors[2], P.m_Wave);
}

inline bool IsGradientCombining(int Code)
{
	return (Code >= 0x0300 && Code <= 0x036F) || (Code >= 0x1AB0 && Code <= 0x1AFF) ||
	       (Code >= 0x1DC0 && Code <= 0x1DFF) || (Code >= 0x20D0 && Code <= 0x20FF) ||
	       (Code >= 0x200B && Code <= 0x200D) || (Code >= 0xFE00 && Code <= 0xFE0F) ||
	       (Code >= 0xFE20 && Code <= 0xFE2F);
}

inline int GradientGlyphLength(const char *pStr)
{
	const char *p = pStr;
	str_utf8_decode(&p);
	while(*p)
	{
		const char *pNext = p;
		const int Code = str_utf8_decode(&pNext);
		if(!IsGradientCombining(Code))
			break;
		p = pNext;
	}
	return (int)(p - pStr);
}

inline int CountGradientGlyphs(const char *pStr)
{
	int Count = 0;
	while(*pStr)
	{
		pStr += GradientGlyphLength(pStr);
		Count++;
	}
	return Count;
}

inline void RenderGradientTextEx(ITextRender *pTextRender, CTextCursor *pCursor, const char *pText,
	const SNameGradientParams &Params, float Time = 0.0f)
{
	const ColorRGBA OldColor = pTextRender->GetTextColor();
	const int Total = CountGradientGlyphs(pText);
	int Index = 0;
	while(*pText)
	{
		const int Len = GradientGlyphLength(pText);
		pTextRender->TextColor(CalcGradientColor(Index++, Total, Params, Time).WithAlpha(OldColor.a));
		pTextRender->TextEx(pCursor, pText, Len);
		pText += Len;
	}
	pTextRender->TextColor(OldColor);
}

class CGradientText
{
	struct SGlyph
	{
		STextContainerIndex m_Container;
		vec2 m_Position = vec2(0.0f, 0.0f);
		float m_Width = 0.0f;
	};

	std::vector<SGlyph> m_vGlyphs;
	char m_aText[256] = "";
	float m_FontSize = -INFINITY;
	float m_MaxWidth = -1.0f;
	vec2 m_Size = vec2(0.0f, 0.0f);

public:
	void Reset(ITextRender *pTextRender)
	{
		for(auto &Glyph : m_vGlyphs)
			pTextRender->DeleteTextContainer(Glyph.m_Container);
		m_vGlyphs.clear();
		m_aText[0] = '\0';
		m_FontSize = -INFINITY;
		m_MaxWidth = -1.0f;
		m_Size = vec2(0.0f, 0.0f);
	}

	bool NeedsUpdate(const char *pText, float FontSize) const
	{
		return m_FontSize != FontSize || str_comp(m_aText, pText) != 0;
	}

	bool NeedsUpdate(const char *pText, float FontSize, float MaxWidth) const
	{
		return m_FontSize != FontSize || m_MaxWidth != MaxWidth || str_comp(m_aText, pText) != 0;
	}

	void Update(ITextRender *pTextRender, const char *pText, float FontSize)
	{
		Update(pTextRender, pText, FontSize, -1.0f);
	}

	void Update(ITextRender *pTextRender, const char *pText, float FontSize, float MaxWidth)
	{
		if(!NeedsUpdate(pText, FontSize, MaxWidth) && !m_vGlyphs.empty())
			return;

		Reset(pTextRender);
		str_copy(m_aText, pText);
		m_FontSize = FontSize;
		m_MaxWidth = MaxWidth;

		// Один курсор для всех глифов — DDNet сам считает cumulative advance
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(0.0f, 0.0f));
		Cursor.m_FontSize = FontSize;
		if(MaxWidth > 0.0f)
			Cursor.m_LineWidth = MaxWidth;

		const char *pCur = pText;
		while(*pCur)
		{
			const int Len = GradientGlyphLength(pCur);
			SGlyph Glyph;
			Glyph.m_Position = vec2(Cursor.m_X, Cursor.m_Y);
			pTextRender->CreateOrAppendTextContainer(Glyph.m_Container, &Cursor, pCur, Len);
			if(Glyph.m_Container.Valid())
				m_vGlyphs.push_back(Glyph);
			pCur += Len;
		}

		// Точная ширина из bbox всего текста (а не из последнего глифа)
		STextContainerIndex SizeIdx;
		CTextCursor SizeCursor;
		SizeCursor.SetPosition(vec2(0.0f, 0.0f));
		SizeCursor.m_FontSize = FontSize;
		if(MaxWidth > 0.0f)
			SizeCursor.m_LineWidth = MaxWidth;
		pTextRender->CreateOrAppendTextContainer(SizeIdx, &SizeCursor, pText);
		STextBoundingBox Bbox = pTextRender->GetBoundingBoxTextContainer(SizeIdx);
		pTextRender->DeleteTextContainer(SizeIdx);
		m_Size = vec2(Bbox.m_W, Bbox.m_H);
	}

	bool Valid() const { return !m_vGlyphs.empty(); }
	vec2 Size() const { return m_Size; }
	float Width() const { return m_Size.x; }

	void Render(ITextRender *pTextRender, vec2 Pos, const SNameGradientParams &Params, float Time,
		float Alpha, ColorRGBA OutlineColor) const
	{
		if(m_vGlyphs.empty())
			return;
		const int Total = (int)m_vGlyphs.size();
		for(int i = 0; i < Total; ++i)
		{
			ColorRGBA Color = CalcGradientColor(i, Total, Params, Time);
			Color.a = Alpha;
			pTextRender->RenderTextContainer(m_vGlyphs[i].m_Container, Color, OutlineColor, Pos.x, Pos.y);
		}
	}
};

// -------- free helpers (СНАРУЖИ класса) --------

template<typename TGameClient>
int NameGradientExtraId(TGameClient &GameClient, int ExtraIndex)
{
	return -1;
}

template<typename TGameClient>
bool NameGradientIsOwn(TGameClient &GameClient, int ClientId)
{
	if(GameClient.Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return ClientId == GameClient.m_Snap.m_LocalClientId;
	if(ClientId == GameClient.m_aLocalIds[0] || ClientId == GameClient.m_aLocalIds[1])
		return true;
	for(int Extra = 0; Extra < 2; ++Extra)
	{
		const int ExtraId = NameGradientExtraId(GameClient, Extra);
		if(ExtraId >= 0 && ClientId == ExtraId)
			return true;
	}
	return false;
}

template<typename TGameClient>
int NameGradientGroup(TGameClient &GameClient, int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return NAME_GRADIENT_NONE;
	if(NameGradientIsOwn(GameClient, ClientId))
		return g_Config.m_ClNameGradientOwn ? NAME_GRADIENT_OWN : NAME_GRADIENT_NONE;
	if(GameClient.m_aClients[ClientId].m_Friend)
		return g_Config.m_ClNameGradientFriend ? NAME_GRADIENT_FRIEND : NAME_GRADIENT_NONE;
	return g_Config.m_ClNameGradientOther ? NAME_GRADIENT_OTHER : NAME_GRADIENT_NONE;
}

template<typename TGameClient>
bool NameGradientAppliesTo(TGameClient &GameClient, int ClientId)
{
	return NameGradientGroup(GameClient, ClientId) != NAME_GRADIENT_NONE;
}

#endif // GAME_CLIENT_GRADIENT_NAME_H