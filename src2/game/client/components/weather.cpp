/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "weather.h"
#include <base/color.h>
#include <base/math.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

CWeather::CWeather()
{
	OnReset();
}

void CWeather::OnReset()
{
	m_vSnowflakes.clear();
	m_WasEnabled = false;
}

void CWeather::OnWindowResize()
{
	ResetFlakes();
}

void CWeather::ResetFlakes()
{
	const int Target = g_Config.m_ClWeatherSnowCount;
	m_vSnowflakes.clear();
	if(Target <= 0)
	{
		m_WasEnabled = false;
		return;
	}

	m_vSnowflakes.resize(Target);
	for(auto &Flake : m_vSnowflakes)
		SpawnFlake(Flake, true);

	m_WasEnabled = true;
}

void CWeather::SpawnFlake(SSnowflake &Flake, bool RandomizeY)
{
	const vec2 CameraCenter = GameClient()->m_Camera.m_Center;
	const float CamZoom = std::max(0.25f, GameClient()->m_Camera.m_Zoom);

	// Видимая область в мировых единицах. CamZoom растёт при ОТДАЛЕНИИ,
	// поэтому видимая область = ScreenSize * CamZoom.
	const float ViewWidth = (float)Graphics()->ScreenWidth() * CamZoom;
	const float ViewHeight = (float)Graphics()->ScreenHeight() * CamZoom;

	// Зона спавна = 1.5× видимой области, симметрично по X и Y.
	const float ZoneHalfW = ViewWidth * 0.75f;
	const float ZoneHalfH = ViewHeight * 0.75f;

	const float RandX = ((rand() % 1000) / 500.0f) - 1.0f;
	const float RandY = ((rand() % 1000) / 500.0f) - 1.0f;

	Flake.m_Pos.x = CameraCenter.x + RandX * ZoneHalfW;
	if(RandomizeY)
		Flake.m_Pos.y = CameraCenter.y + RandY * ZoneHalfH;
	else
	{
		// Чуть выше верхнего края видимой области (30–50 world units).
		const float ViewTop = CameraCenter.y - ViewHeight * 0.5f;
		Flake.m_Pos.y = ViewTop - 30.0f - (rand() % 40) * 0.5f;
	}

	const float SpeedScale = g_Config.m_ClWeatherSnowSpeed / 100.0f;
	Flake.m_Vel.x = ((rand() % 200) - 100) / 100.0f * 15.0f * SpeedScale;
	Flake.m_Vel.y = (30.0f + (rand() % 40)) * SpeedScale;

	// Размер в мировых единицах. При рендере умножим на CamZoom.
	Flake.m_Size = (3.0f + (rand() % 30) / 10.0f) * (g_Config.m_ClWeatherSnowSize / 100.0f);

	Flake.m_Phase = (rand() % 628) / 100.0f;
	// Кружение: каждый снег вертится вокруг своей оси с индивидуальной
	// скоростью и направлением (часть по часовой, часть против).
	Flake.m_Rotation = (rand() % 628) / 100.0f;
	Flake.m_RotationSpeed = ((rand() % 200) - 100) / 100.0f * 2.5f; // -2.5..2.5 rad/s
	Flake.m_TargetAlpha = g_Config.m_ClWeatherSnowOpacity / 100.0f;
	Flake.m_Alpha = Flake.m_TargetAlpha;
	Flake.m_InteractsWithTiles = (rand() % 100) < g_Config.m_ClWeatherSnowTileCollideChance;
	// Если спавн попал в тайл (например, потолок над камерой) — снежинка
	// должна пролететь сквозь него, пока не выйдет из тайлов. Иначе она
	// «сядет» сразу в потолке.
	Flake.m_IgnoreCollisionUntilFree = Flake.m_InteractsWithTiles && GameClient()->Collision()->CheckPoint(Flake.m_Pos);
	Flake.m_Landed = false;
	Flake.m_LandTime = 0.0f;
	Flake.m_MaxLandTime = 0.4f + (rand() % 30) / 100.0f;
}

void CWeather::UpdateFlake(SSnowflake &Flake, float Dt)
{
	const vec2 CameraCenter = GameClient()->m_Camera.m_Center;

	// Та же зона, что и в SpawnFlake, привязана к зуму.
	const float CamZoom = std::max(0.25f, GameClient()->m_Camera.m_Zoom);
	const float ViewWidth = (float)Graphics()->ScreenWidth() * CamZoom;
	const float ViewHeight = (float)Graphics()->ScreenHeight() * CamZoom;
	const float ZoneHalfW = ViewWidth * 0.75f;
	const float ZoneHalfH = ViewHeight * 0.75f;
	// НЕ рециклим по «выше камеры»: снежинка высоко над камерой должна
	// спокойно падать вниз, как и «другие игроки на карте».
	// Рецикл срабатывает, если:
	//   - снежинка упала ниже экрана на 2 высоты (смылась вниз)
	//   - снежинка уехала вбок дальше 3 ширин (защита от телепортов/длинных карт)
	const float RecycleBelow = CameraCenter.y + ViewHeight * 2.0f;
	const float RecycleSide = ViewWidth * 3.0f;

	auto ShouldRecycle = [&]() {
		return Flake.m_Pos.y > RecycleBelow ||
		       std::abs(Flake.m_Pos.x - CameraCenter.x) > RecycleSide;
	};

	if(Flake.m_Landed)
	{
		Flake.m_LandTime += Dt;
		const float T = std::clamp(Flake.m_LandTime / Flake.m_MaxLandTime, 0.0f, 1.0f);
		Flake.m_Alpha = Flake.m_TargetAlpha * (1.0f - T);

		if(Flake.m_LandTime >= Flake.m_MaxLandTime)
		{
			SpawnFlake(Flake, false);
			return;
		}

		// Landed-снежинка не двигается, только ждёт растворения.
		if(std::abs(Flake.m_Pos.x - CameraCenter.x) > ZoneHalfW ||
		   std::abs(Flake.m_Pos.y - CameraCenter.y) > ZoneHalfH)
			SpawnFlake(Flake, false);
		return;
	}

	Flake.m_Phase += Dt * 1.5f;
	const float Drift = std::sin(Flake.m_Phase) * 10.0f;

	// Вращение спрайта вокруг центра. Держим угол в [-pi, pi], чтобы
	// не терять точность float на длинных сессиях.
	Flake.m_Rotation += Flake.m_RotationSpeed * Dt;
	if(Flake.m_Rotation > pi)
		Flake.m_Rotation -= 2.0f * pi;
	else if(Flake.m_Rotation < -pi)
		Flake.m_Rotation += 2.0f * pi;

	// Sub-stepping: если снежинка летит быстро, один шаг может проскочить
	// целый тайл. Разбиваем шаг на несколько подшагов по ~4 unit.
	const float MoveY = Flake.m_Vel.y * Dt;
	const int Steps = std::max(1, (int)(std::abs(MoveY) / 4.0f) + 1);
	const float StepDt = Dt / Steps;

	bool LandedThisFrame = false;
	for(int s = 0; s < Steps; ++s)
	{
		Flake.m_Pos.x += (Flake.m_Vel.x + Drift) * StepDt;
		Flake.m_Pos.y += Flake.m_Vel.y * StepDt;

		if(Flake.m_InteractsWithTiles)
		{
			if(Flake.m_IgnoreCollisionUntilFree)
			{
				// Пока снежинка внутри тайлов (например, спавн под потолком) —
				// не садим её, даём пролететь сквозь. Как только вышли в
				// свободное пространство — начинаем проверять коллизию как обычно.
				if(!GameClient()->Collision()->CheckPoint(Flake.m_Pos))
					Flake.m_IgnoreCollisionUntilFree = false;
			}
			else if(GameClient()->Collision()->CheckPoint(Flake.m_Pos))
			{
				// Откатываем на предыдущий шаг, чтобы снежинка лежала на поверхности.
				Flake.m_Pos.y -= Flake.m_Vel.y * StepDt;
				Flake.m_Landed = true;
				Flake.m_LandTime = 0.0f;
				LandedThisFrame = true;
				break;
			}
		}
	}

	if(LandedThisFrame)
		return;

	if(ShouldRecycle())
		SpawnFlake(Flake, false);
}

void CWeather::OnRender()
{
	if(g_Config.m_ClWeather == 0)
	{
		if(!m_vSnowflakes.empty())
			m_vSnowflakes.clear();
		m_WasEnabled = false;
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!m_WasEnabled || (int)m_vSnowflakes.size() != g_Config.m_ClWeatherSnowCount)
		ResetFlakes();

	if(m_vSnowflakes.empty())
		return;

	const float Dt = Client()->RenderFrameTime();
	if(Dt <= 0.0f || Dt > 0.5f)
		return;

	for(auto &Flake : m_vSnowflakes)
		UpdateFlake(Flake, Dt);

	const CScreenRect ScreenRect = Graphics()->GetScreen();
	const float CamZoom = std::max(0.25f, GameClient()->m_Camera.m_Zoom);

	// Проекция как была: 1 мировая единица = 1 interface-пиксель.
	Graphics()->MapScreenToInterface(
		GameClient()->m_Camera.m_Center.x,
		GameClient()->m_Camera.m_Center.y);

	// Центр экрана и центр камеры в interface-координатах.
	const float ScreenCenterX = (ScreenRect.m_TopLeft.x + ScreenRect.m_BottomRight.x) * 0.5f;
	const float ScreenCenterY = (ScreenRect.m_TopLeft.y + ScreenRect.m_BottomRight.y) * 0.5f;
	const float CamCenterX = GameClient()->m_Camera.m_Center.x;
	const float CamCenterY = GameClient()->m_Camera.m_Center.y;

	// Текстура снежинки из extras.png. Если пользователь не загрузил
	// extras-скин — фолбэк на белый квадрат.
	const bool HasSnowSprite = GameClient()->m_ExtrasSkin.m_Loaded &&
		GameClient()->m_ExtrasSkin.m_SpriteParticleSnowflake.IsValid();

	if(HasSnowSprite)
	{
		Graphics()->TextureSet(GameClient()->m_ExtrasSkin.m_SpriteParticleSnowflake);
		Graphics()->WrapClamp();
	}
	else
	{
		Graphics()->TextureClear();
	}

	Graphics()->QuadsBegin();
	if(HasSnowSprite)
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);

	for(const auto &Flake : m_vSnowflakes)
	{
		if(Flake.m_Alpha <= 0.01f)
			continue;

		// Цвет снежинки из конфига. Альфа конфига умножается на Flake.m_Alpha
		// (для fade-out при приземлении).
		const ColorRGBA SnowColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClWeatherSnowColor, true));
		Graphics()->SetColor(SnowColor.r, SnowColor.g, SnowColor.b, SnowColor.a * Flake.m_Alpha);

		// В этой ветке DDNet m_Camera.m_Zoom растёт при ОТДАЛЕНИИ камеры
		// (обратная семантика по сравнению с апстримом). Поэтому используем
		// 1.0 / CamZoom: при отдалении — снежинки мельче и ближе, при
		// приближении — крупнее и дальше. Позиция и размер согласованы.
		const float ZoomScale = 1.0f / CamZoom;

		const float DrawX = ScreenCenterX + (Flake.m_Pos.x - CamCenterX) * ZoomScale;
		const float DrawY = ScreenCenterY + (Flake.m_Pos.y - CamCenterY) * ZoomScale;

		const float SizePixels = std::max(Flake.m_Size * ZoomScale, 2.0f);
		const float HalfSize = SizePixels * 0.5f;

		// Вращение вокруг центра квада. Угол задаётся per-quad state,
		// поэтому ставим его перед каждым DrawTL и обнуляем после цикла.
		Graphics()->QuadsSetRotation(Flake.m_Rotation);
		IGraphics::CQuadItem QuadItem(DrawX - HalfSize, DrawY - HalfSize, SizePixels, SizePixels);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
	}

	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->WrapNormal();
	Graphics()->MapScreen(ScreenRect);
}