/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "trails.h"

#include <base/io.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <vector>

static int GetTrailGroup(CGameClient *pGameClient, int ClientId)
{
	// 0 = own, 1 = friend, 2 = other
	if(pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return ClientId == pGameClient->m_Snap.m_LocalClientId ? 0 : (pGameClient->m_aClients[ClientId].m_Friend ? 1 : 2);
	
	// Используем встроенную функцию движка, которая проверяет Main, Dummy и Extra
	if(pGameClient->IsCopyAllOwnTee(ClientId))
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

static int GetTrailStyleForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnStyle;
	case 1: return g_Config.m_ClTrailFriendStyle;
	case 2: return g_Config.m_ClTrailOtherStyle;
	}
	return CTrails::TRAIL_STYLE_DASH;
}

static int GetTrailSizeForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnSize;
	case 1: return g_Config.m_ClTrailFriendSize;
	case 2: return g_Config.m_ClTrailOtherSize;
	}
	return 8;
}

static int GetTrailRibbonWidthForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnRibbonWidth;
	case 1: return g_Config.m_ClTrailFriendRibbonWidth;
	case 2: return g_Config.m_ClTrailOtherRibbonWidth;
	}
	return 14;
}

static int GetTrailSpacingForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnSpacing;
	case 1: return g_Config.m_ClTrailFriendSpacing;
	case 2: return g_Config.m_ClTrailOtherSpacing;
	}
	return 15;
}

static int GetTrailLifetimeForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnLifetime;
	case 1: return g_Config.m_ClTrailFriendLifetime;
	case 2: return g_Config.m_ClTrailOtherLifetime;
	}
	return 1000;
}

static int GetTrailFadeTimeForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnFadeTime;
	case 1: return g_Config.m_ClTrailFriendFadeTime;
	case 2: return g_Config.m_ClTrailOtherFadeTime;
	}
	return 500;
}

static int GetTrailFadeStyleForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnFadeStyle;
	case 1: return g_Config.m_ClTrailFriendFadeStyle;
	case 2: return g_Config.m_ClTrailOtherFadeStyle;
	}
	return 0;
}

static int GetTrailNarrowTimeForGroup(int Group)
{
	switch(Group)
	{
	case 0: return g_Config.m_ClTrailOwnNarrowTime;
	case 1: return g_Config.m_ClTrailFriendNarrowTime;
	case 2: return g_Config.m_ClTrailOtherNarrowTime;
	}
	return 0;
}

static int TrailDirCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	if(IsDir)
		return 0;
	
	const int Len = str_length(pName);
	if(Len > 4 && str_comp_nocase(pName + Len - 4, ".png") == 0)
	{
		std::vector<std::string> *pList = (std::vector<std::string> *)pUser;
		pList->push_back(std::string(pName, Len - 4));
	}
	return 0;
}

void CTrails::RefreshAvailableSprites()
{
	m_vAvailableSprites.clear();
	ClearPreviewTextures();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "trail", TrailDirCallback, &m_vAvailableSprites);
	
	// Сортируем и удаляем дубликаты (если файлы лежат в нескольких папках)
	std::sort(m_vAvailableSprites.begin(), m_vAvailableSprites.end());
	m_vAvailableSprites.erase(std::unique(m_vAvailableSprites.begin(), m_vAvailableSprites.end()), m_vAvailableSprites.end());
}

void CTrails::ClearPreviewTextures()
{
	for(auto &Pair : m_PreviewTextures)
	{
		if(Pair.second.IsValid())
			Graphics()->UnloadTexture(&Pair.second);
	}
	m_PreviewTextures.clear();
}

IGraphics::CTextureHandle CTrails::GetSpriteTexture(const char *pName)
{
	std::string Name(pName);
	auto it = m_PreviewTextures.find(Name);
	if(it != m_PreviewTextures.end())
		return it->second;

	char aPath[IO_MAX_PATH_LENGTH];
	const char *apFormats[] = { "trail/%s.png", "trail/%s", "%s.png" };
	IGraphics::CTextureHandle Tex = IGraphics::CTextureHandle();

	for(const char *pFormat : apFormats)
	{
		str_format(aPath, sizeof(aPath), pFormat, pName);
		IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
		if(File)
		{
			unsigned char aHeader[8];
			if(io_read(File, aHeader, sizeof(aHeader)) == sizeof(aHeader))
			{
				if(aHeader[0] == 0x89 && aHeader[1] == 0x50 && aHeader[2] == 0x4E && aHeader[3] == 0x47 &&
				   aHeader[4] == 0x0D && aHeader[5] == 0x0A && aHeader[6] == 0x1A && aHeader[7] == 0x0A)
				{
					// Читаем весь файл в память, чтобы обойти кэш движка
					io_seek(File, 0, (EIoSeekOrigin)0);
					unsigned int FileSize = io_length(File);
					unsigned char *pData = (unsigned char *)malloc(FileSize);
					if(io_read(File, pData, FileSize) == FileSize)
					{
						CImageInfo Image;
						if(Graphics()->LoadPng(Image, pData, FileSize, aPath))
						{
							Tex = Graphics()->LoadTextureRawMove(Image, IGraphics::TEXLOAD_NEAREST_FILTER);
						}
					}
					free(pData);
				}
			}
			io_close(File);
		}
		if(Tex.IsValid() && !Tex.IsNullTexture())
			break;
	}

	m_PreviewTextures[Name] = Tex;
	return Tex;
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
		m_aNextPointId[i] = 0;
		m_aPrevState[i] = TRAIL_STATE_NONE;
	}
	// Здесь НЕЛЬЗЯ вызывать Graphics() и Storage(), иначе игра вылетит при запуске!
}

void CTrails::InvalidateDashTrailTexture()
{
	if(m_DashTrailTexture.IsValid())
		Graphics()->UnloadTexture(&m_DashTrailTexture);
	m_DashTrailTexture = IGraphics::CTextureHandle();
	m_DashTrailTextureTried = false;
	m_aLastDashSpriteName[0] = '\0';
}

ColorRGBA CTrails::GenerateRandomColor()
{
	const float Hue = (float)rand() / (float)RAND_MAX;
	return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, 0.5f, 1.0f));
}

void CTrails::UpdateDashTexture()
{
	const bool UseDashSprite = g_Config.m_ClTrailDashSprite != 0;
	if(!UseDashSprite)
		return;

	const char *pName = g_Config.m_ClTrailDashSpriteName;
	if(str_comp(pName, m_aLastDashSpriteName) != 0)
	{
		InvalidateDashTrailTexture();
		str_copy(m_aLastDashSpriteName, pName, sizeof(m_aLastDashSpriteName));
	}
	if(!m_DashTrailTextureTried)
	{
		m_DashTrailTextureTried = true;
		char aPath[IO_MAX_PATH_LENGTH];
		
		const char *apFormats[] = {
			"trail/%s.png",
			"trail/%s",
			"%s.png"
		};

		for(const char *pFormat : apFormats)
		{
			str_format(aPath, sizeof(aPath), pFormat, pName);
			
			IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
			if(File)
			{
				unsigned char aHeader[8];
				if(io_read(File, aHeader, sizeof(aHeader)) == sizeof(aHeader))
				{
					// Проверяем PNG-магию: 89 50 4E 47 0D 0A 1A 0A
					if(aHeader[0] == 0x89 && aHeader[1] == 0x50 && aHeader[2] == 0x4E && aHeader[3] == 0x47 &&
					   aHeader[4] == 0x0D && aHeader[5] == 0x0A && aHeader[6] == 0x1A && aHeader[7] == 0x0A)
					{
						// Читаем весь файл в память, чтобы обойти кэш движка
						io_seek(File, 0, (EIoSeekOrigin)0);
						unsigned int FileSize = io_length(File);
						unsigned char *pData = (unsigned char *)malloc(FileSize);
						if(io_read(File, pData, FileSize) == FileSize)
						{
							CImageInfo Image;
							if(Graphics()->LoadPng(Image, pData, FileSize, aPath))
							{
								// Загружаем как Raw-текстуру с флагом NEAREST
								m_DashTrailTexture = Graphics()->LoadTextureRawMove(Image, IGraphics::TEXLOAD_NEAREST_FILTER);
							}
						}
						free(pData);
					}
				}
				io_close(File);
			}

			// Если текстура успешно загрузилась, прекращаем поиск
			if(m_DashTrailTexture.IsValid() && !m_DashTrailTexture.IsNullTexture())
				break;
		}
	}
}

void CTrails::OnRender()
{
	UpdateDashTexture();

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!g_Config.m_ClTrail)
	{
		for(auto &Trail : m_aTrailPoints)
			Trail.clear();
		return;
	}

	const bool DashSpriteReady = g_Config.m_ClTrailDashSprite != 0 && m_DashTrailTexture.IsValid() && !m_DashTrailTexture.IsNullTexture();
	const int64_t Now = time();

	// Alpha: fade_time controls the tail fade.
	// - fade_time <= 0 : no fade, full opacity until pruned (instant disappear)
	// - fade_time = lifetime : linear fade across the whole lifetime
	// - 0 < fade_time < lifetime : full opacity, then fade in the last fade_time ms
	auto ComputeAlpha = [&](int64_t PointTime, int64_t Lifetime, int64_t FadeTime) -> float {
		const int64_t Age = Now - PointTime;
		if(Age >= Lifetime)
			return 0.0f;
		const int64_t Fade = std::min<int64_t>(FadeTime, Lifetime);
		if(Fade <= 0)
			return 1.0f; // no fade: hard cut at prune
		if(Age > Lifetime - Fade)
		{
			const int64_t FadeAge = Age - (Lifetime - Fade);
			return std::clamp(1.0f - (float)FadeAge / (float)Fade, 0.0f, 1.0f);
		}
		return 1.0f;
	};

	auto ComputeWidthFactor = [&](int64_t PointTime, int64_t NarrowTime) -> float {
		if(NarrowTime <= 0)
			return 1.0f;
		const int64_t Age = Now - PointTime;
		if(Age >= NarrowTime)
			return 1.0f;
		if(Age <= 0)
			return 0.05f;
		const float t = (float)Age / (float)NarrowTime;
		return std::clamp(t * t * (3.0f - 2.0f * t), 0.05f, 1.0f);
	};

	// Лямбда для стиля исчезновения (Opacity / Shrink / Ease-Out)
	auto ComputeFadeSizeFactor = [&](int64_t PointTime, int64_t Lifetime, int64_t FadeTime, int FadeStyle) -> float {
		if(FadeStyle == 0) return 1.0f;
		const int64_t Age = Now - PointTime;
		if(Age >= Lifetime) return 0.0f;
		const int64_t Fade = std::min<int64_t>(FadeTime, Lifetime);
		if(Fade <= 0) return 1.0f;
		if(Age > Lifetime - Fade)
		{
			float p = (float)(Age - (Lifetime - Fade)) / (float)Fade;
			if(FadeStyle == 1) // Bounce (эффект отскока)
			{
				const float n1 = 7.5625f;
				const float d1 = 2.75f;
				float b;
				if(p < 1.0f / d1) b = n1 * p * p;
				else if(p < 2.0f / d1) { float t = p - 1.5f / d1; b = n1 * t * t + 0.75f; }
				else if(p < 2.5f / d1) { float t = p - 2.25f / d1; b = n1 * t * t + 0.9375f; }
				else { float t = p - 2.625f / d1; b = n1 * t * t + 0.984375f; }
				return 1.0f - b;
			}
			if(FadeStyle == 2) return 1.0f - (p * p); // Ease-Out
		}
		return 1.0f;
	};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// Blend mode is switched per-client (each group may use a different style).
	int LastBlend = -1; // -1 = unknown, 0 = normal, 1 = additive

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[i];
		const bool Active = pInfo && GameClient()->m_Snap.m_aCharacters[i].m_Active;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;

		// ------------------------------------------------------------------
		// State machine: persistent trail.
		// Points survive when the player becomes inactive or disconnects.
		// They are only cleared on a genuine "new player" or "respawn" event.
		// ------------------------------------------------------------------
		int CurState;
		if(!pInfo)
			CurState = TRAIL_STATE_NONE;
		else if(!Active)
			CurState = TRAIL_STATE_INACTIVE;
		else
			CurState = TRAIL_STATE_ACTIVE;

		// New player connected (or same slot reused): start fresh.
		if(m_aPrevState[i] == TRAIL_STATE_NONE && CurState != TRAIL_STATE_NONE)
		{
			m_aTrailPoints[i].clear();
			m_aLastGroup[i] = -1;
			m_aLastPos[i] = Pos;
			m_aNextPointId[i] = 0;
		}

		// Respawn (inactive -> active): the player is at a new position, so
		// the old trail would create a huge segment. Clear it.
		if(m_aPrevState[i] == TRAIL_STATE_INACTIVE && CurState == TRAIL_STATE_ACTIVE)
		{
			m_aTrailPoints[i].clear();
			m_aLastPos[i] = Pos;
		}

		m_aPrevState[i] = CurState;

		// Update group + sample new points only when the player is in-game.
		if(CurState == TRAIL_STATE_ACTIVE)
		{
			const int NewGroup = GetTrailGroup(GameClient(), i);
			const float Spacing = (float)GetTrailSpacingForGroup(NewGroup); // Берем Spacing для текущей группы
			
			if(m_aLastGroup[i] != NewGroup)
			{
				m_aTrailPoints[i].clear();
				m_aLastGroup[i] = NewGroup;
				m_aLastPos[i] = Pos;
			}
			else if(IsTrailEnabledForGroup(NewGroup))
			{
				const vec2 Delta = Pos - m_aLastPos[i];
				const float DeltaLen = length(Delta);
				
				// Если игрок движется быстро, ставим несколько точек с интервалом Spacing,
				// чтобы трейл не разрывался. Если медленно (меньше Spacing) — точки не спавнятся.
				if(DeltaLen >= Spacing && DeltaLen < 500.0f) // 500.0f — защита от телепортаций
				{
					const vec2 Dir = Delta / DeltaLen;
					float Remaining = DeltaLen;
					
					while(Remaining >= Spacing)
					{
						STrailPoint Point;
						Point.m_Pos = m_aLastPos[i] + Dir * Spacing;
						Point.m_Dir = Dir;
						Point.m_Perp = vec2(-Dir.y, Dir.x);
						Point.m_Time = Now;
						Point.m_Id = m_aNextPointId[i]++;

						// Если включен режим "Cycle", назначаем спрайт по кругу
						if(g_Config.m_ClTrailDashSprite && g_Config.m_ClTrailDashSpriteMode == 1 && !m_vAvailableSprites.empty())
							Point.m_SpriteIndex = Point.m_Id % (int)m_vAvailableSprites.size();
						const int ColorMode = GetTrailColorModeForGroup(NewGroup);
						Point.m_Color = (ColorMode == 1)
										? GenerateRandomColor()
										: color_cast<ColorRGBA>(ColorHSLA(GetTrailColorForGroup(NewGroup)));

						m_aTrailPoints[i].push_back(Point);
						m_aLastPos[i] = Point.m_Pos;
						Remaining -= Spacing;
					}
				}
				else if(DeltaLen >= 500.0f)
				{
					// Телепортация — сбрасываем позицию без спавна кучи точек
					m_aLastPos[i] = Pos;
				}
			}
			else
			{
				// Trail disabled for this group — track position so re-enabling
				// doesn't create a giant segment.
				m_aLastPos[i] = Pos;
			}
		}

		const int Group = m_aLastGroup[i];
		const int64_t Lifetime = (int64_t)GetTrailLifetimeForGroup(Group) * time_freq() / 1000;

		// Prune expired points regardless of player state, so the trail
		// fades out naturally when the player is gone.
		while(!m_aTrailPoints[i].empty() && Now - m_aTrailPoints[i].front().m_Time > Lifetime)
			m_aTrailPoints[i].pop_front();

		auto &Points = m_aTrailPoints[i];
		if(Points.empty())
			continue;
		const int Style = GetTrailStyleForGroup(Group);
		const bool UseRibbon = (Style == TRAIL_STYLE_RIBBON);
		const bool UseNeon = (Style == TRAIL_STYLE_NEON);
		const float Size = (float)GetTrailSizeForGroup(Group);
		const float RibbonHalfWidth = (float)GetTrailRibbonWidthForGroup(Group) / 2.0f;
		const int64_t FadeTime = (int64_t)GetTrailFadeTimeForGroup(Group) * time_freq() / 1000;
		const int64_t NarrowTime = (int64_t)GetTrailNarrowTimeForGroup(Group) * time_freq() / 1000;
		const int FadeStyle = GetTrailFadeStyleForGroup(Group);

		// Switch blend mode only when needed (each switch flushes vertices).
		const int DesiredBlend = UseNeon ? 1 : 0;
		if(DesiredBlend != LastBlend)
		{
			if(DesiredBlend)
				Graphics()->BlendAdditive();
			else
				Graphics()->BlendNormal();
			LastBlend = DesiredBlend;
		}

		if(UseRibbon)
		{
			Graphics()->QuadsSetRotation(0.0f);

			auto DrawSegment = [&](const STrailPoint &P0, float A0, const STrailPoint &P1, float A1) {
				ColorRGBA C0 = P0.m_Color;
				C0.a *= A0;
				ColorRGBA C1 = P1.m_Color;
				C1.a *= A1;
				if(C0.a < 0.005f && C1.a < 0.005f)
					return;

				const float Fade0 = ComputeFadeSizeFactor(P0.m_Time, Lifetime, FadeTime, FadeStyle);
				const float Fade1 = ComputeFadeSizeFactor(P1.m_Time, Lifetime, FadeTime, FadeStyle);
				const float W0 = RibbonHalfWidth * ComputeWidthFactor(P0.m_Time, NarrowTime) * Fade0;
				const float W1 = RibbonHalfWidth * ComputeWidthFactor(P1.m_Time, NarrowTime) * Fade1;

				const vec2 TL = P0.m_Pos + P0.m_Perp * W0;
				const vec2 TR = P0.m_Pos - P0.m_Perp * W0;
				const vec2 BL = P1.m_Pos + P1.m_Perp * W1;
				const vec2 BR = P1.m_Pos - P1.m_Perp * W1;

				Graphics()->SetColor4(C0, C0, C1, C1);
				IGraphics::CFreeformItem Freeform(
					TL.x, TL.y,
					TR.x, TR.y,
					BL.x, BL.y,
					BR.x, BR.y);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			};

			for(size_t j = 1; j < Points.size(); j++)
			{
				const auto &P0 = Points[j - 1];
				const auto &P1 = Points[j];
				DrawSegment(P0, ComputeAlpha(P0.m_Time, Lifetime, FadeTime), P1, ComputeAlpha(P1.m_Time, Lifetime, FadeTime));
			}

			// Head glue: only draw when the tee is actually in the game.
			if(Active)
			{
				const auto &Last = Points.back();
				const vec2 HeadDelta = Pos - Last.m_Pos;
				if(length(HeadDelta) > 0.5f)
				{
					STrailPoint Head;
					Head.m_Pos = Pos;
					Head.m_Dir = normalize(HeadDelta);
					Head.m_Perp = vec2(-Head.m_Dir.y, Head.m_Dir.x);
					Head.m_Color = Last.m_Color;
					Head.m_Time = Now;
					DrawSegment(Last, ComputeAlpha(Last.m_Time, Lifetime, FadeTime), Head, 1.0f);
				}
			}
		}
		else if(UseNeon)
		{
			if(Points.size() < 2)
				continue;

			Graphics()->QuadsSetRotation(0.0f);

			const int StrandCount = 8;
			const float StrandSpacing = (RibbonHalfWidth * 2.0f) / 7.0f;
			const float NeonScale = StrandSpacing / 3.1f;

			const int ColorMode = GetTrailColorModeForGroup(Group);

			const float Freq = (float)time_freq();
			const int N = (int)Points.size();

			std::vector<vec2> aPerp(N);
			std::vector<float> aAlpha(N);
			std::vector<float> aWidth(N);
			for(int j = 0; j < N; j++)
			{
				const auto &Pb = Points[std::max(0, j - 3)];
				const auto &Pa = Points[std::min(N - 1, j + 3)];
				vec2 D = Pa.m_Pos - Pb.m_Pos;
				if(length(D) < 0.0001f)
					D = Points[j].m_Dir;
				D = normalize(D);
				aPerp[j] = vec2(-D.y, D.x);

				const float AgeNorm = (float)(Now - Points[j].m_Time) / (float)Lifetime;
				const float Remaining = std::clamp(1.0f - AgeNorm, 0.0f, 1.0f);
				const float RampUp = std::min(1.0f, (float)(N - 1 - j) / 14.0f + 0.25f);
				aAlpha[j] = powf(Remaining, 1.35f) * RampUp;

				const float FadeSize = ComputeFadeSizeFactor(Points[j].m_Time, Lifetime, FadeTime, FadeStyle);
				aWidth[j] = ComputeWidthFactor(Points[j].m_Time, NarrowTime) * FadeSize;
			}

			auto Hash1 = [](uint32_t n) -> float {
				const float x = sinf((float)n * 127.1f + 311.7f) * 43758.5453f;
				return x - floorf(x);
			};

			auto StrandOffsetRaw = [&](const STrailPoint &P, int S) -> float {
				const float SharedNoise = (Hash1(P.m_Id / 4) - 0.5f) * 3.2f * NeonScale;
				const float StrandNoise = (Hash1((P.m_Id / 3) * 9 + (uint32_t)S) - 0.5f) * 1.1f * NeonScale;
				const float Wave = sinf((float)P.m_Id * 0.045f + (float)S * 0.55f +
								(float)(Now - P.m_Time) / Freq * 1.6f) * 1.3f * NeonScale;
				const float StrandCenter = ((float)S - (StrandCount - 1) * 0.5f) * StrandSpacing;
				return StrandCenter + SharedNoise + StrandNoise + Wave;
			};

			auto GetNeonStrandColor = [&](const STrailPoint &P, int S) -> ColorRGBA {
				if(ColorMode == 1)
				{
					ColorHSLA H = color_cast<ColorHSLA>(P.m_Color);
					const float TimeShift = (float)Now / (float)Freq * 0.25f;
					const float StrandShift = (float)S / (float)StrandCount;
					const float IdJitter = Hash1(P.m_Id) * 0.15f;
					H.h = fmodf(H.h + TimeShift + StrandShift + IdJitter + 100.0f, 1.0f);
					return color_cast<ColorRGBA>(H);
				}
				return color_cast<ColorRGBA>(ColorHSLA(GetTrailColorForGroup(Group)));
			};

			struct SPass { float m_Width; float m_Opacity; };
			static const SPass s_aPasses[3] = {
				{7.5f * NeonScale, 0.10f},
				{3.6f * NeonScale, 0.26f},
				{1.5f * NeonScale, 0.95f},
			};

			auto DrawNeonSegment = [&](const vec2 &Sa, const vec2 &Sb, float Aa, float Ab, float Wa, float Wb, const ColorRGBA &ColA, const ColorRGBA &ColB) {
				vec2 SegDir = Sb - Sa;
				const float SegLen = length(SegDir);
				if(SegLen < 0.01f)
					return;
				SegDir = SegDir / SegLen;
				const vec2 SegPerp = vec2(-SegDir.y, SegDir.x);

				for(const auto &Pass : s_aPasses)
				{
					const float PerpA = Pass.m_Width * 0.5f * Wa;
					const float PerpB = Pass.m_Width * 0.5f * Wb;
					const float AlphaA = std::min(1.0f, Aa * Pass.m_Opacity);
					const float AlphaB = std::min(1.0f, Ab * Pass.m_Opacity);
					if(AlphaA < 0.005f && AlphaB < 0.005f)
						continue;

					ColorRGBA Ca = ColA;
					Ca.a = AlphaA;
					ColorRGBA Cb = ColB;
					Cb.a = AlphaB;

					const vec2 L0 = Sa - SegPerp * PerpA;
					const vec2 R0 = Sa + SegPerp * PerpA;
					const vec2 L1 = Sb - SegPerp * PerpB;
					const vec2 R1 = Sb + SegPerp * PerpB;

					Graphics()->SetColor4(Ca, Ca, Cb, Cb);
					IGraphics::CFreeformItem F(L0.x, L0.y, R0.x, R0.y, L1.x, L1.y, R1.x, R1.y);
					Graphics()->QuadsDrawFreeform(&F, 1);
				}

				const float HA = std::min(1.0f, Aa * 0.22f);
				const float HB = std::min(1.0f, Ab * 0.22f);
				if(HA >= 0.005f || HB >= 0.005f)
				{
					ColorRGBA Ca(1.0f, 0.92f, 1.0f, HA);
					ColorRGBA Cb(1.0f, 0.92f, 1.0f, HB);
					const float PerpA = 0.3f * NeonScale * Wa;
					const float PerpB = 0.3f * NeonScale * Wb;
					const vec2 L0 = Sa - SegPerp * PerpA;
					const vec2 R0 = Sa + SegPerp * PerpA;
					const vec2 L1 = Sb - SegPerp * PerpB;
					const vec2 R1 = Sb + SegPerp * PerpB;
					Graphics()->SetColor4(Ca, Ca, Cb, Cb);
					IGraphics::CFreeformItem F(L0.x, L0.y, R0.x, R0.y, L1.x, L1.y, R1.x, R1.y);
					Graphics()->QuadsDrawFreeform(&F, 1);
				}
			};

			for(int j = 1; j < N; j++)
			{
				const auto &Pa = Points[j - 1];
				const auto &Pb = Points[j];
				const float Aa = aAlpha[j - 1];
				const float Ab = aAlpha[j];
				if(Aa < 0.005f && Ab < 0.005f)
					continue;

				for(int s = 0; s < StrandCount; s++)
				{
					const float OffA = StrandOffsetRaw(Pa, s) * aWidth[j - 1];
					const float OffB = StrandOffsetRaw(Pb, s) * aWidth[j];
					const vec2 Sa = Pa.m_Pos + aPerp[j - 1] * OffA;
					const vec2 Sb = Pb.m_Pos + aPerp[j] * OffB;
					const ColorRGBA ColA = GetNeonStrandColor(Pa, s);
					const ColorRGBA ColB = GetNeonStrandColor(Pb, s);
					DrawNeonSegment(Sa, Sb, Aa, Ab, aWidth[j - 1], aWidth[j], ColA, ColB);
				}
			}

			if(Active)
			{
				const auto &Last = Points.back();
				const vec2 HeadDelta = Pos - Last.m_Pos;
				if(length(HeadDelta) > 0.5f)
				{
					const vec2 HeadDir = normalize(HeadDelta);
					const vec2 HeadPerp = vec2(-HeadDir.y, HeadDir.x);
					const float HeadWidth = ComputeWidthFactor(Now, NarrowTime);
					for(int s = 0; s < StrandCount; s++)
					{
						const float OffRaw = StrandOffsetRaw(Last, s);
						const vec2 Sa = Last.m_Pos + aPerp.back() * (OffRaw * aWidth.back());
						const vec2 Sb = Pos + HeadPerp * (OffRaw * HeadWidth);
						const ColorRGBA ColA = GetNeonStrandColor(Last, s);
						DrawNeonSegment(Sa, Sb, aAlpha.back(), 1.0f, aWidth.back(), HeadWidth, ColA, ColA);
					}
				}
			}
		}
		else
		{
			const float SpriteSizeMult = DashSpriteReady ? (g_Config.m_ClTrailDashSpriteSize / 100.0f) : 1.0f;
			IGraphics::CTextureHandle LastTex = IGraphics::CTextureHandle();

			if(!DashSpriteReady)
				Graphics()->TextureClear();

			for(const auto &Point : Points)
			{
				const float Alpha = ComputeAlpha(Point.m_Time, Lifetime, FadeTime);
				const float FadeSize = ComputeFadeSizeFactor(Point.m_Time, Lifetime, FadeTime, FadeStyle);
				float W = Size * ComputeWidthFactor(Point.m_Time, NarrowTime) * SpriteSizeMult * FadeSize;
				if(W < 0.01f)
					continue;
				ColorRGBA Color = Point.m_Color;
				Color.a = Alpha;
				Graphics()->SetColor(Color);

				float Rot = 0.0f;
				if(!DashSpriteReady || g_Config.m_ClTrailDashRotate)
					Rot = atan2f(Point.m_Dir.y, Point.m_Dir.x);

				if(DashSpriteReady && g_Config.m_ClTrailDashSwing)
				{
					const float Freq = (float)time_freq();
					const float TimeSec = (float)Now / Freq;
					const float Speed = (float)g_Config.m_ClTrailDashSwingSpeed / 100.0f;
					const float MaxAngle = (float)g_Config.m_ClTrailDashSwingAngle * (pi / 180.0f);
					const float Phase = (float)Point.m_Id * 0.5f;
					Rot += sinf(TimeSec * 4.0f * Speed + Phase) * MaxAngle;
				}

				Graphics()->QuadsSetRotation(Rot);

				// Если это режим Cycle и у точки есть индекс — берем её текстуру
				if(DashSpriteReady && Point.m_SpriteIndex >= 0 && Point.m_SpriteIndex < (int)m_vAvailableSprites.size())
				{
					IGraphics::CTextureHandle Tex = GetSpriteTexture(m_vAvailableSprites[Point.m_SpriteIndex].c_str());
					if(Tex.IsValid() && !Tex.IsNullTexture())
					{
						if(!LastTex.IsValid() || LastTex.Id() != Tex.Id())
						{
							Graphics()->QuadsEnd();
							Graphics()->TextureSet(Tex);
							Graphics()->WrapClamp();
							Graphics()->QuadsBegin();
							Graphics()->SetColor(Color); // <-- Восстанавливаем цвет
							Graphics()->QuadsSetRotation(Rot); // <-- Восстанавливаем поворот
							LastTex = Tex;
						}
					}
				}
				else if(DashSpriteReady && (!LastTex.IsValid() || LastTex.Id() != m_DashTrailTexture.Id()))
				{
					Graphics()->QuadsEnd();
					Graphics()->TextureSet(m_DashTrailTexture);
					Graphics()->WrapClamp();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(Color); // <-- Восстанавливаем цвет
					Graphics()->QuadsSetRotation(Rot); // <-- Восстанавливаем поворот
					LastTex = m_DashTrailTexture;
				}

				const float DashLen = DashSpriteReady ? W : W * 2.0f;
				IGraphics::CQuadItem QuadItem(Point.m_Pos.x - DashLen / 2.0f, Point.m_Pos.y - W / 2.0f, DashLen, W);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
			}

			Graphics()->QuadsSetRotation(0.0f);

			if(DashSpriteReady)
			{
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
			}
		}
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Restore normal blending if we finished in additive mode.
	if(LastBlend == 1)
		Graphics()->BlendNormal();
}