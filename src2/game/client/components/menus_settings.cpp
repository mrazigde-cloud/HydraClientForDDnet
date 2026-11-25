/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/client/ui_scrollregion.h>

#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>
#include <base/net.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/client/ui_listbox.h>

#include <base/io.h>
#include <engine/storage.h>

#include <cstdlib>
static CScrollRegion s_ForceSkinPickerScroll;
float CMenus::EaseTabSlide(float t, int Mode)
{
	t = std::clamp(t, 0.0f, 1.0f);
	if(Mode == 1) // ease-out cubic
	{
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
	if(Mode == 2) // ease-in-out cubic
	{
		if(t < 0.5f)
			return 4.0f * t * t * t;
		const float u = -2.0f * t + 2.0f;
		return 1.0f - (u * u * u) * 0.5f;
	}
	if(Mode == 3) // ease-out bounce (Robert Penner)
	{
		constexpr float n1 = 7.5625f;
		constexpr float d1 = 2.75f;
		float x = t;
		if(x < 1.0f / d1)
			return n1 * x * x;
		if(x < 2.0f / d1)
		{
			x -= 1.5f / d1;
			return n1 * x * x + 0.75f;
		}
		if(x < 2.5f / d1)
		{
			x -= 2.25f / d1;
			return n1 * x * x + 0.9375f;
		}
		x -= 2.625f / d1;
		return n1 * x * x + 0.984375f;
	}
	return t; // linear
}

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::SetMenuBackgroundForPage(int Page)
{
	int Pos = CMenuBackground::POS_SETTINGS_GENERAL;
	switch(Page)
	{
	case SETTINGS_LANGUAGE: Pos = CMenuBackground::POS_SETTINGS_LANGUAGE; break;
	case SETTINGS_GENERAL: Pos = CMenuBackground::POS_SETTINGS_GENERAL; break;
	case SETTINGS_PLAYER: Pos = CMenuBackground::POS_SETTINGS_PLAYER; break;
	case SETTINGS_TEE: Pos = CMenuBackground::POS_SETTINGS_TEE; break;
	case SETTINGS_APPEARANCE: Pos = CMenuBackground::POS_SETTINGS_APPEARANCE; break;
	case SETTINGS_CONTROLS: Pos = CMenuBackground::POS_SETTINGS_CONTROLS; break;
	case SETTINGS_GRAPHICS: Pos = CMenuBackground::POS_SETTINGS_GRAPHICS; break;
	case SETTINGS_SOUND: Pos = CMenuBackground::POS_SETTINGS_SOUND; break;
	case SETTINGS_DDNET: Pos = CMenuBackground::POS_SETTINGS_DDNET; break;
	case SETTINGS_ASSETS: Pos = CMenuBackground::POS_SETTINGS_ASSETS; break;
	case SETTINGS_TESTCLIENT: Pos = CMenuBackground::POS_SETTINGS_ASSETS; break;
	case SETTINGS_PROFILES: Pos = CMenuBackground::POS_SETTINGS_TEE; break;
	case SETTINGS_ANIMATIONS: Pos = CMenuBackground::POS_SETTINGS_ASSETS; break;
	}
	GameClient()->m_MenuBackground.ChangePosition(Pos);
}

void CMenus::RenderSettingsTabContent(int Page, CUIRect View)
{
	if(Page == SETTINGS_LANGUAGE)        RenderLanguageSettings(View);
	else if(Page == SETTINGS_GENERAL)    RenderSettingsGeneral(View);
	else if(Page == SETTINGS_PLAYER)     RenderSettingsPlayer(View);
	else if(Page == SETTINGS_TEE)        Client()->IsSixup() ? RenderSettingsTee7(View) : RenderSettingsTee(View);
	else if(Page == SETTINGS_APPEARANCE) RenderSettingsAppearance(View);
	else if(Page == SETTINGS_CONTROLS)   m_MenusSettingsControls.Render(View);
	else if(Page == SETTINGS_GRAPHICS)   RenderSettingsGraphics(View);
	else if(Page == SETTINGS_SOUND)      RenderSettingsSound(View);
	else if(Page == SETTINGS_DDNET)      RenderSettingsDDNet(View);
	else if(Page == SETTINGS_ASSETS)     RenderSettingsAssets(View);
	else if(Page == SETTINGS_TESTCLIENT) RenderSettingsTestClient(View);
	else if(Page == SETTINGS_PROFILES)   RenderSettingsProfiles(View);
	else if(Page == SETTINGS_ANIMATIONS) RenderSettingsAnimations(View);
}

void CMenus::RenderSettings(CUIRect MainView)
{
	if(g_Config.m_UiSettingsPage >= SETTINGS_LENGTH)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	CUIRect Button, TabBar, RestartBar;
	MainView.VSplitRight(120.0f, &MainView, &TabBar);
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);

	const char *apTabs[SETTINGS_LENGTH] = {
		Localize("Language"),
		Localize("General"),
		Localize("Player"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		Localize("Hydra Client"),
		Localize("Profiles"),
		Localize("Animations")};
	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			g_Config.m_UiSettingsPage = i;
	}

	static int s_LastPage = -1;
	if(s_LastPage < 0)
		s_LastPage = g_Config.m_UiSettingsPage;

	if(g_Config.m_UiSettingsPage != s_LastPage)
	{
		if(g_Config.m_ClSettingsTabAnimation)
		{
			const int FromPage = m_SettingsPageAnim.m_Active ? m_SettingsPageAnim.m_ToPage : s_LastPage;
			m_SettingsPageAnim.m_FromPage = FromPage;
			m_SettingsPageAnim.m_ToPage = g_Config.m_UiSettingsPage;
			m_SettingsPageAnim.m_StartTime = time();
			m_SettingsPageAnim.m_Active = true;
		}
		s_LastPage = g_Config.m_UiSettingsPage;
	}

	SetMenuBackgroundForPage(m_SettingsPageAnim.m_Active ? m_SettingsPageAnim.m_ToPage : g_Config.m_UiSettingsPage);

	if(m_SettingsPageAnim.m_Active)
	{
		const float DurationSec = g_Config.m_ClSettingsTabAnimationDuration / 1000.0f;
		float t = (time() - m_SettingsPageAnim.m_StartTime) / (float)time_freq() / DurationSec;

		if(t >= 1.0f)
		{
			m_SettingsPageAnim.m_Active = false;
			RenderSettingsTabContent(g_Config.m_UiSettingsPage, MainView);
		}
		else
		{
			const float Eased = EaseTabSlide(t, g_Config.m_ClSettingsTabAnimationEasing);
			const int Dir = (m_SettingsPageAnim.m_ToPage > m_SettingsPageAnim.m_FromPage) ? 1 : -1;
			const float W = MainView.w;

			{
				CUIRect OldView = MainView;
				OldView.x -= Dir * Eased * W;

				const float X0 = std::max(MainView.x, OldView.x);
				const float X1 = std::min(MainView.x + MainView.w, OldView.x + OldView.w);
				if(X1 > X0)
				{
					CUIRect ClipRect = MainView;
					ClipRect.x = X0;
					ClipRect.w = X1 - X0;
					Ui()->ClipEnable(&ClipRect);
					Ui()->SetEnabled(false);
					RenderSettingsTabContent(m_SettingsPageAnim.m_FromPage, OldView);
					Ui()->SetEnabled(true);
					Ui()->ClipDisable();
				}
			}

			{
				CUIRect NewView = MainView;
				NewView.x += Dir * (1.0f - Eased) * W;

				const float X0 = std::max(MainView.x, NewView.x);
				const float X1 = std::min(MainView.x + MainView.w, NewView.x + NewView.w);
				if(X1 > X0)
				{
					CUIRect ClipRect = MainView;
					ClipRect.x = X0;
					ClipRect.w = X1 - X0;

					Ui()->ClipEnable(&ClipRect);
					RenderSettingsTabContent(m_SettingsPageAnim.m_ToPage, NewView);
					Ui()->ClipDisable();
				}
			}
		}
	}
	else
	{
		RenderSettingsTabContent(g_Config.m_UiSettingsPage, MainView);
	}

	if(NeedRestart)
	{
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
	}
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 0, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 1, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}
void CMenus::ResetTestClientSettings()
{
	// ---- Name gradient (legacy, общий) ----
	g_Config.m_ClNameGradient = 0;
	g_Config.m_ClNameGradientOthers = 0;
	g_Config.m_ClNameGradientSpeed = 30;
	g_Config.m_ClNameGradientMode = 1;
	g_Config.m_ClNameGradientWave = 1;
	g_Config.m_ClNameGradientColor1 = 65408;
	g_Config.m_ClNameGradientColor2 = 5635968;
	g_Config.m_ClNameGradientColor3 = 11206528;

	// ---- Name gradient: Own ----
	g_Config.m_ClNameGradientOwn = 0;
	g_Config.m_ClNameGradientOwnSpeed = 30;
	g_Config.m_ClNameGradientOwnMode = 1;
	g_Config.m_ClNameGradientOwnWave = 1;
	g_Config.m_ClNameGradientOwnColor1 = 65408;
	g_Config.m_ClNameGradientOwnColor2 = 5635968;
	g_Config.m_ClNameGradientOwnColor3 = 11206528;

	// ---- Name gradient: Friend ----
	g_Config.m_ClNameGradientFriend = 0;
	g_Config.m_ClNameGradientFriendSpeed = 30;
	g_Config.m_ClNameGradientFriendMode = 1;
	g_Config.m_ClNameGradientFriendWave = 1;
	g_Config.m_ClNameGradientFriendColor1 = 15335296;
	g_Config.m_ClNameGradientFriendColor2 = 1376128;
	g_Config.m_ClNameGradientFriendColor3 = 2686848;

	// ---- Name gradient: Other ----
	g_Config.m_ClNameGradientOther = 0;
	g_Config.m_ClNameGradientOtherSpeed = 30;
	g_Config.m_ClNameGradientOtherMode = 1;
	g_Config.m_ClNameGradientOtherWave = 1;
	g_Config.m_ClNameGradientOtherColor1 = 8454016;
	g_Config.m_ClNameGradientOtherColor2 = 13172608;
	g_Config.m_ClNameGradientOtherColor3 = 11206528;

	// ---- Trail: general ----
	g_Config.m_ClTrailOwnFadeStyle = 0;
	g_Config.m_ClTrailFriendFadeStyle = 0;
	g_Config.m_ClTrailOtherFadeStyle = 0;
	g_Config.m_ClTrail = 0;
	g_Config.m_ClTrailDashSprite = 0;
	str_copy(g_Config.m_ClTrailDashSpriteName, "trail_dash", sizeof(g_Config.m_ClTrailDashSpriteName));
	g_Config.m_ClTrailDashRotate = 1;
	g_Config.m_ClTrailDashSwing = 0;
	g_Config.m_ClTrailDashSwingAngle = 45;
	g_Config.m_ClTrailDashSwingSpeed = 100;

	// ---- Trail: Own ----
	g_Config.m_ClTrailOwn = 1;
	g_Config.m_ClTrailOwnStyle = 0;
	g_Config.m_ClTrailOwnSize = 8;
	g_Config.m_ClTrailOwnRibbonWidth = 14;
	g_Config.m_ClTrailOwnLifetime = 1000;
	g_Config.m_ClTrailOwnFadeTime = 500;
	g_Config.m_ClTrailOwnNarrowTime = 0;
	g_Config.m_ClTrailOwnColorMode = 0;
	g_Config.m_ClTrailOwnColor = 65408;
	g_Config.m_ClTrailOwnSpacing = 15;

	// ---- Trail: Friend ----
	g_Config.m_ClTrailFriend = 1;
	g_Config.m_ClTrailFriendStyle = 0;
	g_Config.m_ClTrailFriendSize = 8;
	g_Config.m_ClTrailFriendRibbonWidth = 14;
	g_Config.m_ClTrailFriendLifetime = 1000;
	g_Config.m_ClTrailFriendFadeTime = 500;
	g_Config.m_ClTrailFriendFadeStyle = 0;
	g_Config.m_ClTrailFriendNarrowTime = 0;
	g_Config.m_ClTrailFriendColorMode = 0;
	g_Config.m_ClTrailFriendColor = 16711808;

	// ---- Trail: Other ----
	g_Config.m_ClTrailOther = 1;
	g_Config.m_ClTrailOtherStyle = 0;
	g_Config.m_ClTrailOtherSize = 8;
	g_Config.m_ClTrailOtherRibbonWidth = 14;
	g_Config.m_ClTrailOtherLifetime = 1000;
	g_Config.m_ClTrailOtherFadeTime = 500;
	g_Config.m_ClTrailOtherFadeStyle = 0;
	g_Config.m_ClTrailOtherNarrowTime = 0;
	g_Config.m_ClTrailOtherColorMode = 0;
	g_Config.m_ClTrailOtherColor = 11206528;

	// ---- Weather ----
	g_Config.m_ClWeather = 0;
	g_Config.m_ClWeatherSnowCount = 300;
	g_Config.m_ClWeatherSnowSpeed = 100;
	g_Config.m_ClWeatherSnowSize = 100;
	g_Config.m_ClWeatherSnowOpacity = 80;
	g_Config.m_ClWeatherSnowTileCollideChance = 85;
	g_Config.m_ClWeatherSnowColor = 0xFFFFFFFF;

	// ---- Test client: emote cycle ----
	g_Config.m_ClTestClientEmoteCycle = 0;
	g_Config.m_ClTestClientEmoteDelay = 100;
	g_Config.m_ClTestClientEmoteSet = 0;
	g_Config.m_ClTestClientEmoteCount = 3;
	g_Config.m_ClTestClientEmoteCustom1 = 5;
	g_Config.m_ClTestClientEmoteCustom2 = 2;
	g_Config.m_ClTestClientEmoteCustom3 = 14;
	g_Config.m_ClTestClientEmoteCustom4 = 0;
	g_Config.m_ClTestClientEmoteCustom5 = 0;
	g_Config.m_ClTestClientEmoteCustom6 = 0;
	g_Config.m_ClTestClientEmoteCustom7 = 0;
	g_Config.m_ClTestClientEmoteCustom8 = 0;
	// ---- Scroll ----
	g_Config.m_ClTouchScrollSmooth = 0;
	g_Config.m_ClScrollInertiaStrength = 100;
	g_Config.m_ClScrollInertiaDecay = 100;
	g_Config.m_ClScrollInertiaSpeed = 100;
	g_Config.m_ClScrollMouseThreshold = 50;
	g_Config.m_ClScrollDragThreshold = 10;
	g_Config.m_ClWheelScrollAmount = 60;
	// ---- Extra ----
	g_Config.m_ClExtraAutoSwitch = 1;
	// ---- Notifications ----
	g_Config.m_ClNotificationDuration = 6;
	// ---- Tee rotation ----
	g_Config.m_ClTeeRotation = 0;
	g_Config.m_ClTeeRotationMode = 0;
	g_Config.m_ClTeeRotationSpeed = 100;
	g_Config.m_ClTeeRotationSwingAngle = 45;
	g_Config.m_ClTeeRotationOwn = 1;
	g_Config.m_ClTeeRotationDummy = 1;
	g_Config.m_ClTeeRotationExtra = 1;
	g_Config.m_ClTeeRotationOthers = 1;

	// ---- Direction rotation ----
	g_Config.m_ClDirRotation = 0;
	g_Config.m_ClDirRotationMode = 0;
	g_Config.m_ClDirRotationSpeed = 100;
	g_Config.m_ClDirRotationSwingAngle = 45;
	g_Config.m_ClDirRotationStepAngle = 45;

	// ---- Force skin ----
	g_Config.m_ClForceSkinRandomColors = 0;
	g_Config.m_ClForceSkinRandomColorSeed = 0;
	g_Config.m_ClForceSkinMode = 0;
	str_copy(g_Config.m_ClForceSkinName, "default", sizeof(g_Config.m_ClForceSkinName));
	g_Config.m_ClForceSkinRandomSeed = 0;
	g_Config.m_ClForceSkinExcludeLocal = 1;
	g_Config.m_ClForceSkinUseMyColors = 0;
	g_Config.m_ClDirRotationOwn = 1;
	g_Config.m_ClDirRotationDummy = 1;
	g_Config.m_ClDirRotationExtra = 1;
	g_Config.m_ClDirRotationOthers = 1;
}

void CMenus::DrawNameGradientPreview(CUIRect Rect, unsigned int Color1, unsigned int Color2, unsigned int Color3, bool Wave)
{
	Rect.Draw(ColorRGBA(0.08f, 0.08f, 0.12f, 0.7f), IGraphics::CORNER_ALL, 3.0f);
	Rect.Margin(3.0f, &Rect);

	static const int64_t s_StartTime = time();
	const float Freq = (float)time_freq();
	const float T = (float)(time() - s_StartTime) / Freq;

	ColorHSLA H1 = ColorHSLA(Color1);
	ColorHSLA H2 = ColorHSLA(Color2);
	ColorHSLA H3 = ColorHSLA(Color3);
	if(Wave)
	{
		// Rotate hue palette continuously; all three shift together so
		// the gradient keeps its shape but slides through the color wheel.
		const float Shift = T * 0.15f;
		H1.h = fmodf(H1.h + Shift + 1.0f, 1.0f);
		H2.h = fmodf(H2.h + Shift + 1.0f, 1.0f);
		H3.h = fmodf(H3.h + Shift + 1.0f, 1.0f);
	}
	const ColorRGBA C1 = color_cast<ColorRGBA>(H1);
	const ColorRGBA C2 = color_cast<ColorRGBA>(H2);
	const ColorRGBA C3 = color_cast<ColorRGBA>(H3);

	Graphics()->TextureClear();
	Graphics()->BlendNormal();
	Graphics()->QuadsBegin();

	// Left half: C1 -> C2
	Graphics()->SetColor4(C1, C2, C2, C1);
	{
		IGraphics::CFreeformItem F(
			Rect.x, Rect.y,
			Rect.x + Rect.w * 0.5f, Rect.y,
			Rect.x, Rect.y + Rect.h,
			Rect.x + Rect.w * 0.5f, Rect.y + Rect.h);
		Graphics()->QuadsDrawFreeform(&F, 1);
	}

	// Right half: C2 -> C3
	Graphics()->SetColor4(C2, C3, C3, C2);
	{
		IGraphics::CFreeformItem F(
			Rect.x + Rect.w * 0.5f, Rect.y,
			Rect.x + Rect.w, Rect.y,
			Rect.x + Rect.w * 0.5f, Rect.y + Rect.h,
			Rect.x + Rect.w, Rect.y + Rect.h);
		Graphics()->QuadsDrawFreeform(&F, 1);
	}

	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	(void)Wave; // будущее: анимация волны
}

void CMenus::DrawTrailPreview(CUIRect Rect, int Group)
{
	Rect.Draw(ColorRGBA(0.08f, 0.08f, 0.12f, 0.7f), IGraphics::CORNER_ALL, 3.0f);
	Rect.Margin(6.0f, &Rect);

	int Style, Size, RibbonWidth, ColorMode, NarrowTime, Spacing, Lifetime, FadeTime, FadeStyle;
	unsigned int TrailColor;
	if (Group == 0)
	{
		Style = g_Config.m_ClTrailOwnStyle;
		Size = g_Config.m_ClTrailOwnSize;
		RibbonWidth = g_Config.m_ClTrailOwnRibbonWidth;
		ColorMode = g_Config.m_ClTrailOwnColorMode;
		TrailColor = g_Config.m_ClTrailOwnColor;
		NarrowTime = g_Config.m_ClTrailOwnNarrowTime;
		Spacing = g_Config.m_ClTrailOwnSpacing;
		Lifetime = g_Config.m_ClTrailOwnLifetime;
		FadeTime = g_Config.m_ClTrailOwnFadeTime;
		FadeStyle = g_Config.m_ClTrailOwnFadeStyle;
	}
	else if (Group == 1)
	{
		Style = g_Config.m_ClTrailFriendStyle;
		Size = g_Config.m_ClTrailFriendSize;
		RibbonWidth = g_Config.m_ClTrailFriendRibbonWidth;
		ColorMode = g_Config.m_ClTrailFriendColorMode;
		TrailColor = g_Config.m_ClTrailFriendColor;
		NarrowTime = g_Config.m_ClTrailFriendNarrowTime;
		Spacing = g_Config.m_ClTrailFriendSpacing;
		Lifetime = g_Config.m_ClTrailFriendLifetime;
		FadeTime = g_Config.m_ClTrailFriendFadeTime;
		FadeStyle = g_Config.m_ClTrailFriendFadeStyle;
	}
	else
	{
		Style = g_Config.m_ClTrailOtherStyle;
		Size = g_Config.m_ClTrailOtherSize;
		RibbonWidth = g_Config.m_ClTrailOtherRibbonWidth;
		ColorMode = g_Config.m_ClTrailOtherColorMode;
		TrailColor = g_Config.m_ClTrailOtherColor;
		NarrowTime = g_Config.m_ClTrailOtherNarrowTime;
		Spacing = g_Config.m_ClTrailOtherSpacing;
		Lifetime = g_Config.m_ClTrailOtherLifetime;
		FadeTime = g_Config.m_ClTrailOtherFadeTime;
		FadeStyle = g_Config.m_ClTrailOtherFadeStyle;
	}

	if (Spacing < 1) Spacing = 1;
	if (Lifetime < 1) Lifetime = 1;

	// Точная копия логики ComputeAlpha из trails.cpp
	auto ComputeAlphaMenu = [&](float Age) -> float {
		if(Age >= Lifetime) return 0.0f;
		float Fade = (float)std::min(FadeTime, Lifetime);
		if(Fade <= 0) return 1.0f;
		if(Age > Lifetime - Fade) {
			return std::clamp(1.0f - (Age - (Lifetime - Fade)) / Fade, 0.0f, 1.0f);
		}
		return 1.0f;
	};

	// Точная копия ComputeWidthFactor из trails.cpp
	auto ComputeWidthFactorMenu = [&](float Age) -> float {
		if(NarrowTime <= 0) return 1.0f;
		if(Age >= NarrowTime) return 1.0f;
		if(Age <= 0) return 0.05f;
		float t = Age / (float)NarrowTime;
		return std::clamp(t * t * (3.0f - 2.0f * t), 0.05f, 1.0f);
	};

	// Точная копия ComputeFadeSizeFactor из trails.cpp
	auto ComputeFadeSizeFactorMenu = [&](float Age) -> float {
		if(FadeStyle == 0) return 1.0f;
		if(Age >= Lifetime) return 0.0f;
		float Fade = (float)std::min(FadeTime, Lifetime);
		if(Fade <= 0) return 1.0f;
		if(Age > Lifetime - Fade)
		{
			float p = (Age - (Lifetime - Fade)) / Fade;
			if(FadeStyle == 1) return 1.0f - p;
			if(FadeStyle == 2) return 1.0f - (p * p);
		}
		return 1.0f;
	};

	// Вписываем большие размеры в высоту прямоугольника
	const float MaxFitWidth = Rect.h * 0.45f;
	float DrawScale = 1.0f;
	if(Style == 0 && Size > 0)
		DrawScale = std::min(1.0f, MaxFitWidth / (float)Size);
	else if((Style == 1 || Style == 2) && RibbonWidth > 0)
		DrawScale = std::min(1.0f, MaxFitWidth / (float)RibbonWidth);

	float DrawnHalf = 0.0f;
	if(Style == 0)
		DrawnHalf = (float)Size * DrawScale * 0.5f;
	else if(Style == 1)
		DrawnHalf = (float)RibbonWidth * DrawScale * 0.5f;
	else
		DrawnHalf = ((float)RibbonWidth / 4.0f + 3.5f) * DrawScale;

	const float AvailableHalf = Rect.h * 0.5f - DrawnHalf - 1.5f;
	const float SineAmp = std::min(Rect.h * 0.3f, std::max(0.0f, AvailableHalf));
	
	// Анимация волны (двигается слева направо)
	static const int64_t s_PreviewStartTime = time();
	const float Freq = (float)time_freq();
	const float T = (float)(time() - s_PreviewStartTime) / Freq;
	
	float WavePhase = T * 1.8f;

	auto GetPathPos = [&](float t) {
		float x = Rect.x + 4.0f + t * (Rect.w - 8.0f);
		float y = Rect.y + Rect.h * 0.5f + sinf(t * 6.28318f - WavePhase) * SineAmp;
		return vec2(x, y);
	};

	struct PreviewPoint {
		vec2 m_Pos;
		vec2 m_Dir;
		float m_Age;
		int m_Id;
	};
	std::vector<PreviewPoint> vPoints;

	const float PathLen = Rect.w - 8.0f;
	// Шаг по оси X жестко привязан к настройке Spacing
	const float dT = std::max(0.005f, (float)Spacing / PathLen);
	int IdCounter = 0;

	// Идем от головы (t=1.0) к хвосту (t=0.0)
	for(float t = 1.0f; t >= -0.001f; t -= dT)
	{
		vec2 P = GetPathPos(t);
		
		// Направление движения (вектор от хвоста к голове, т.е. слева направо)
		vec2 NextP = GetPathPos(t + 0.01f);
		vec2 PrevP = GetPathPos(t - 0.01f);
		vec2 Dir = normalize(NextP - PrevP);
		
		// Возраст точки зависит от того, насколько она далеко от головы
		float Age = (1.0f - t) * (float)Lifetime;
		
		vPoints.push_back({P, Dir, Age, IdCounter++});
	}

	Graphics()->TextureClear();
	Graphics()->QuadsSetRotation(0.0f);

	auto GetColor = [&](int Id) -> ColorRGBA {
		if(ColorMode == 1) {
			float Hue = fmodf((float)Id * 0.17f + 0.13f, 1.0f);
			return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, 0.5f, 1.0f));
		}
		return color_cast<ColorRGBA>(ColorHSLA(TrailColor));
	};

	if(Style == 0) // Dashes
	{
		const auto &vSprites = GameClient()->m_Trails.GetAvailableSprites();
		const bool HasSprite = g_Config.m_ClTrailDashSprite != 0 && !vSprites.empty();
		const bool IsCycle = HasSprite && g_Config.m_ClTrailDashSpriteMode == 1;
		const float SpriteSizeMult = HasSprite ? (g_Config.m_ClTrailDashSpriteSize / 100.0f) : 1.0f;
		
		Graphics()->BlendNormal();
		if(!HasSprite)
			Graphics()->TextureClear();
		Graphics()->QuadsBegin();

		IGraphics::CTextureHandle LastTex = IGraphics::CTextureHandle();

		for(const auto &pt : vPoints)
		{
			float Alpha = ComputeAlphaMenu(pt.m_Age);
			if(Alpha < 0.01f) continue;
			
			float WFactor = ComputeWidthFactorMenu(pt.m_Age) * ComputeFadeSizeFactorMenu(pt.m_Age);
			float W = (float)Size * DrawScale * WFactor * SpriteSizeMult;
			
			ColorRGBA Color = GetColor(pt.m_Id);
			Color.a = Alpha;
			Graphics()->SetColor(Color);
			
			float Rot = 0.0f;
			if(!HasSprite || g_Config.m_ClTrailDashRotate)
				Rot = atan2f(pt.m_Dir.y, pt.m_Dir.x);

			// Swing в превью
			if(HasSprite && g_Config.m_ClTrailDashSwing)
			{
				const float Speed = (float)g_Config.m_ClTrailDashSwingSpeed / 100.0f;
				const float MaxAngle = (float)g_Config.m_ClTrailDashSwingAngle * (pi / 180.0f);
				const float Phase = (float)pt.m_Id * 0.5f;
				Rot += sinf(T * 4.0f * Speed + Phase) * MaxAngle;
			}

			Graphics()->QuadsSetRotation(Rot);

			if(HasSprite)
			{
				IGraphics::CTextureHandle Tex;
				if(IsCycle)
				{
					int SpriteIdx = pt.m_Id % (int)vSprites.size();
					Tex = GameClient()->m_Trails.GetSpriteTexture(vSprites[SpriteIdx].c_str());
				}
				else
				{
					Tex = GameClient()->m_Trails.GetSpriteTexture(g_Config.m_ClTrailDashSpriteName);
				}

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

			const float DashLen = HasSprite ? W : W * 2.0f;
			IGraphics::CQuadItem Item(pt.m_Pos.x - DashLen * 0.5f, pt.m_Pos.y - W * 0.5f, DashLen, W);
			Graphics()->QuadsDrawTL(&Item, 1);
		}
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->QuadsEnd();
		
		if(HasSprite)
		{
			Graphics()->TextureClear();
			Graphics()->WrapNormal();
		}
	}
	else if(Style == 1) // Ribbon
	{
		Graphics()->BlendNormal();
		Graphics()->QuadsBegin();
		const float HalfW = (float)RibbonWidth * 0.5f * DrawScale;
		
		for(size_t i = 1; i < vPoints.size(); i++)
		{
			const auto &p0 = vPoints[i - 1];
			const auto &p1 = vPoints[i];
			
			float A0 = ComputeAlphaMenu(p0.m_Age);
			float A1 = ComputeAlphaMenu(p1.m_Age);
			if(A0 < 0.01f && A1 < 0.01f) continue;
			
			ColorRGBA C0 = GetColor(p0.m_Id); C0.a = A0;
			ColorRGBA C1 = GetColor(p1.m_Id); C1.a = A1;
			
			float W0 = ComputeWidthFactorMenu(p0.m_Age) * ComputeFadeSizeFactorMenu(p0.m_Age);
			float W1 = ComputeWidthFactorMenu(p1.m_Age) * ComputeFadeSizeFactorMenu(p1.m_Age);
			
			vec2 Perp0(-p0.m_Dir.y, p0.m_Dir.x);
			vec2 Perp1(-p1.m_Dir.y, p1.m_Dir.x);
			
			vec2 TL = p0.m_Pos + Perp0 * (HalfW * W0);
			vec2 TR = p0.m_Pos - Perp0 * (HalfW * W0);
			vec2 BL = p1.m_Pos + Perp1 * (HalfW * W1);
			vec2 BR = p1.m_Pos - Perp1 * (HalfW * W1);
			
			Graphics()->SetColor4(C0, C0, C1, C1);
			IGraphics::CFreeformItem F(TL.x, TL.y, TR.x, TR.y, BL.x, BL.y, BR.x, BR.y);
			Graphics()->QuadsDrawFreeform(&F, 1);
		}
		Graphics()->QuadsEnd();
	}
	else // Neon
	{
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();
		const int Strands = 3;
		const float SpacingStrand = (float)RibbonWidth / (float)(Strands + 1) * DrawScale;
		const float GlowW = 3.5f * DrawScale;
		const float CoreW = 1.0f * DrawScale;

		for(size_t i = 1; i < vPoints.size(); i++)
		{
			const auto &p0 = vPoints[i - 1];
			const auto &p1 = vPoints[i];
			
			float A0 = ComputeAlphaMenu(p0.m_Age);
			float A1 = ComputeAlphaMenu(p1.m_Age);
			if(A0 < 0.01f && A1 < 0.01f) continue;
			
			float W0 = ComputeWidthFactorMenu(p0.m_Age) * ComputeFadeSizeFactorMenu(p0.m_Age);
			float W1 = ComputeWidthFactorMenu(p1.m_Age) * ComputeFadeSizeFactorMenu(p1.m_Age);

			vec2 D = p1.m_Pos - p0.m_Pos;
			if(length(D) < 0.01f) continue;
			D = normalize(D);
			vec2 Perp(-D.y, D.x);

			for(int s = 0; s < Strands; s++)
			{
				const float Tw = 0.82f + 0.18f * sinf(T * 2.5f + (float)s * 1.7f);
				const float Off = ((float)s - (Strands - 1) * 0.5f) * SpacingStrand;
				
				vec2 P0 = p0.m_Pos + vec2(-p0.m_Dir.y, p0.m_Dir.x) * (Off * W0);
				vec2 P1 = p1.m_Pos + vec2(-p1.m_Dir.y, p1.m_Dir.x) * (Off * W1);

				ColorRGBA C0 = GetColor(p0.m_Id); C0.a = A0 * Tw;
				ColorRGBA C1 = GetColor(p1.m_Id); C1.a = A1 * Tw;

				ColorRGBA C0Glow = C0; C0Glow.a *= 0.18f;
				ColorRGBA C1Glow = C1; C1Glow.a *= 0.18f;
				Graphics()->SetColor4(C0Glow, C0Glow, C1Glow, C1Glow);
				{
					float W0g = GlowW * W0;
					float W1g = GlowW * W1;
					IGraphics::CFreeformItem F(
						P0.x - Perp.x * W0g, P0.y - Perp.y * W0g,
						P0.x + Perp.x * W0g, P0.y + Perp.y * W0g,
						P1.x - Perp.x * W1g, P1.y - Perp.y * W1g,
						P1.x + Perp.x * W1g, P1.y + Perp.y * W1g);
					Graphics()->QuadsDrawFreeform(&F, 1);
				}
				
				ColorRGBA C0Core = C0; C0Core.a *= 0.95f;
				ColorRGBA C1Core = C1; C1Core.a *= 0.95f;
				Graphics()->SetColor4(C0Core, C0Core, C1Core, C1Core);
				{
					float W0c = CoreW * W0;
					float W1c = CoreW * W1;
					IGraphics::CFreeformItem F(
						P0.x - Perp.x * W0c, P0.y - Perp.y * W0c,
						P0.x + Perp.x * W0c, P0.y + Perp.y * W0c,
						P1.x - Perp.x * W1c, P1.y - Perp.y * W1c,
						P1.x + Perp.x * W1c, P1.y + Perp.y * W1c);
					Graphics()->QuadsDrawFreeform(&F, 1);
				}
			}
		}
		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
void CMenus::DoForceSkinPicker(CUIRect View)
{
	auto &SkinList = GameClient()->m_Skins.SkinList();
	if(SkinList.Skins().empty())
		SkinList.ForceRefresh();

	// Search row (fixed, outside scroll)
	CUIRect SearchLabel, SearchRow;
	View.HSplitTop(22.0f, &SearchRow, &View);
	View.HSplitTop(8.0f, nullptr, &View);
	static char s_aSearch[64] = "";
	static CLineInput s_Search(s_aSearch, sizeof(s_aSearch));
	s_Search.SetEmptyText(Localize("Search skins..."));
	SearchRow.VSplitLeft(60.0f, &SearchLabel, &SearchRow);
	Ui()->DoLabel(&SearchLabel, Localize("Search:"), 12.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_Search, &SearchRow, 12.0f);

	// Filter
	std::vector<CSkins::CSkinListEntry *> vFiltered;
	vFiltered.reserve(SkinList.Skins().size());
	for(auto &Entry : SkinList.Skins())
	{
		const CSkins::CSkinContainer *pC = Entry.SkinContainer();
		if(pC == nullptr)
			continue;
		if(s_aSearch[0] != '\0' && str_find_nocase(pC->Name(), s_aSearch) == nullptr)
			continue;
		vFiltered.push_back(&Entry);
	}

	// Background for the grid area only (search stays above, outside scroll)
	View.Draw(ColorRGBA(0.05f, 0.05f, 0.05f, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	View.Margin(4.0f, &View);

	// Native CScrollRegion; но колесо гейтится — регион реагирует,
	// только если мышь над ним. Иначе колесо уходит внешнему скроллу.
	CScrollRegion &s_PickerScroll = s_ForceSkinPickerScroll;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	s_PickerScroll.Begin(&View, &ScrollParams);

	const float CellW = 80.0f;
	const float CellH = 104.0f;
	const float CellGap = 4.0f;
	const int PerRow = std::max(1, (int)((View.w + CellGap) / (CellW + CellGap)));
	const int NumRows = ((int)vFiltered.size() + PerRow - 1) / PerRow;

	CUIRect Content = View;
	Content.h = std::max(View.h, NumRows * (CellH + CellGap));
	s_PickerScroll.AddRect(Content);

	for(size_t idx = 0; idx < vFiltered.size(); ++idx)
	{
		CSkins::CSkinListEntry *pEntry = vFiltered[idx];
		const CSkins::CSkinContainer *pC = pEntry->SkinContainer();
		const char *pName = pC->Name();

		const int Col = (int)idx % PerRow;
		const int Row = (int)idx / PerRow;

		CUIRect Cell;
		Cell.x = Content.x + Col * (CellW + CellGap);
		Cell.y = Content.y + Row * (CellH + CellGap);
		Cell.w = CellW;
		Cell.h = CellH;

		const bool Active = str_comp(g_Config.m_ClForceSkinName, pName) == 0;
		pEntry->RequestLoad();

		Cell.Draw(Active ? ColorRGBA(0.20f, 0.45f, 0.85f, 0.75f) : ColorRGBA(0.12f, 0.12f, 0.12f, 0.75f),
			IGraphics::CORNER_ALL, 4.0f);

		CUIRect Preview = Cell;
		Preview.HSplitTop(CellH - 18.0f, &Preview, nullptr);
		Preview.Margin(6.0f, &Preview);

		const std::unique_ptr<CSkin> &pSkinPtr = pC->Skin();
		if(pSkinPtr)
		{
			CTeeRenderInfo Info;
			Info.m_aSixup[g_Config.m_ClDummy].Reset();
			Info.Apply(pSkinPtr.get());
			Info.m_Size = Preview.h;

			if(g_Config.m_ClForceSkinUseMyColors)
			{
				constexpr float DarkestLight = 0.5f;
				ColorHSLA BodyHsl = ColorHSLA(g_Config.m_ClPlayerColorBody == 0 ? 65408u : (unsigned int)g_Config.m_ClPlayerColorBody);
				ColorHSLA FeetHsl = ColorHSLA(g_Config.m_ClPlayerColorFeet == 0 ? 65408u : (unsigned int)g_Config.m_ClPlayerColorFeet);
				BodyHsl = BodyHsl.UnclampLighting(DarkestLight);
				FeetHsl = FeetHsl.UnclampLighting(DarkestLight);
				BodyHsl.a = 1.0f;
				FeetHsl.a = 1.0f;
				Info.m_CustomColoredSkin = true;
				Info.m_ColorBody = color_cast<ColorRGBA>(BodyHsl);
				Info.m_ColorFeet = color_cast<ColorRGBA>(FeetHsl);
			}
			else if(g_Config.m_ClForceSkinRandomColors)
			{
				constexpr float DarkestLight = 0.5f;
				const uint32_t ColorSeed = (uint32_t)g_Config.m_ClForceSkinRandomColorSeed;
				const uint32_t ColorHash = ColorSeed * 2246822519u + 0x9E3779B9u;
				const float Hue = (float)((ColorHash >> 16) & 0xFFFF) / 65535.0f;
				const float Sat = 0.65f + ((ColorHash >> 8) & 0xFF) / 255.0f * 0.35f;
				const float LhtRaw = 0.35f + (ColorHash & 0xFF) / 255.0f * 0.35f;
				const float FeetHue = fmodf(Hue + 0.5f, 1.0f);
				ColorHSLA BodyHsl(Hue, Sat, LhtRaw);
				ColorHSLA FeetHsl(FeetHue, Sat, LhtRaw);
				BodyHsl = BodyHsl.UnclampLighting(DarkestLight);
				FeetHsl = FeetHsl.UnclampLighting(DarkestLight);
				BodyHsl.a = 1.0f;
				FeetHsl.a = 1.0f;
				Info.m_CustomColoredSkin = true;
				Info.m_ColorBody = color_cast<ColorRGBA>(BodyHsl);
				Info.m_ColorFeet = color_cast<ColorRGBA>(FeetHsl);
			}

			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Preview.Center());
		}
		else
		{
			Ui()->DoLabel(&Preview, "...", 20.0f, TEXTALIGN_MC);
		}

		CUIRect Name = Cell;
		Name.HSplitBottom(18.0f, nullptr, &Name);
		TextRender()->TextColor(Active ? ColorRGBA(0.5f, 0.85f, 1.0f, 1.0f) : TextRender()->DefaultTextColor());
		Ui()->DoLabel(&Name, pName, 10.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const bool Hovered = Ui()->MouseInside(&Cell);
		if(Hovered && Ui()->MouseButtonClicked(0))
			str_copy(g_Config.m_ClForceSkinName, pName);
	}

	if(vFiltered.empty())
	{
		CUIRect Center = View;
		Ui()->DoLabel(&Center, Localize("No matching skins"), 12.0f, TEXTALIGN_MC);
	}

	s_PickerScroll.End();
}
void CMenus::RenderSettingsTestClient(CUIRect MainView)
{
	CUIRect Label, Button, Left, Right, Middle;
	// ***** Sub tabs ***** //
	enum
	{
		TESTCLIENT_TAB_GENERAL = 0,
		TESTCLIENT_TAB_NAME_GRADIENT = 1,
		TESTCLIENT_TAB_TRAIL = 2,
		TESTCLIENT_TAB_WEATHER = 3,
		TESTCLIENT_TAB_TEE_ROTATION = 4,
		TESTCLIENT_TAB_DIRECTION_ROTATION = 5,
		TESTCLIENT_TAB_FORCE_SKIN = 6,
		NUMBER_OF_TESTCLIENT_TABS = 7,
	};
	static int s_CurTab = TESTCLIENT_TAB_GENERAL;

	// Reset button row (always visible, above the tabs).
	{
		CUIRect ResetRow;
		MainView.HSplitTop(22.0f, &ResetRow, &MainView);
		MainView.HSplitTop(6.0f, nullptr, &MainView);

		CUIRect ResetButton;
		ResetRow.VSplitRight(220.0f, nullptr, &ResetButton);
		static CButtonContainer s_ResetButton;
		if(DoButton_Menu(&s_ResetButton, Localize("Reset Hydra settings to defaults"), 0, &ResetButton))
		{
			PopupConfirm(
				Localize("Reset Hydra settings"),
				Localize("Are you sure you want to reset all Hydra settings to defaults? This cannot be undone."),
				Localize("Reset"),
				Localize("Cancel"),
				&CMenus::ResetTestClientSettings);
		}
	}

	{
		CUIRect TabBar;
		MainView.HSplitTop(20.0f, &TabBar, &MainView);
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		const float TabWidth = TabBar.w / (float)NUMBER_OF_TESTCLIENT_TABS;
		static CButtonContainer s_aTabs[NUMBER_OF_TESTCLIENT_TABS] = {};
		const char *apTabNames[NUMBER_OF_TESTCLIENT_TABS] = {
			Localize("General"),
			Localize("Name Gradient"),
			Localize("Trail"),
			Localize("Weather"),
			Localize("Tee Rotation"),
			Localize("Direction Rotation"),
			Localize("Force Skin")};
		static_assert(NUMBER_OF_TESTCLIENT_TABS == 7, "apTabNames must have 7 entries");
		for(int Tab = 0; Tab < NUMBER_OF_TESTCLIENT_TABS; ++Tab)
		{
			TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
			const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == NUMBER_OF_TESTCLIENT_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_CurTab = Tab;
		}
	}

	// ***** Name Gradient tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_NAME_GRADIENT)
	{
		const float LineSize = 20.0f;
		const float ColorPickerLineSize = 25.0f;
		const float ColorPickerLabelSize = 13.0f;
		const float ColorPickerLineSpacing = 5.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		static int s_aModeButtons[3][2];
		static CButtonContainer s_aColorButtons[3][3];

		static CScrollRegion s_NameGradientScrollRegion;
		CScrollRegionParams NGScrollParams;
		NGScrollParams.m_ScrollUnit = 60.0f;
		s_NameGradientScrollRegion.Begin(&MainView, &NGScrollParams);
		const float StartY = MainView.y;

		// Returns the total vertical height consumed by this group.
		auto DoGroup = [&](int Index, CUIRect View, const char *pTitle, int *pEnable, int *pSpeed, int *pMode, int *pWave,
					unsigned int *pColor1, unsigned int *pColor2, unsigned int *pColor3,
					ColorRGBA Default1, ColorRGBA Default2, ColorRGBA Default3) -> float {
			const float GroupStartY = View.y;
			CUIRect Row, ModeConstant, ModeLinear;

			Ui()->DoLabel_AutoLineSize(pTitle, HeadlineFontSize, TEXTALIGN_ML, &View, HeadlineHeight);
			View.HSplitTop(MarginSmall, nullptr, &View);

			DoButton_CheckBoxAutoVMarginAndSet(pEnable, Localize("Enable"), pEnable, &View, LineSize);

			View.HSplitTop(LineSize, &Row, &View);
			Ui()->DoScrollbarOption(pSpeed, pSpeed, &Row, Localize("Speed"), 1, 100);

			View.HSplitTop(LineSize, &Row, &View);
			Row.VSplitMid(&ModeConstant, &ModeLinear);
			if(DoButton_CheckBox(&s_aModeButtons[Index][0], Localize("Constant"), *pMode == 0, &ModeConstant))
				*pMode = 0;
			if(DoButton_CheckBox(&s_aModeButtons[Index][1], Localize("Linear"), *pMode == 1, &ModeLinear))
				*pMode = 1;
			DoButton_CheckBoxAutoVMarginAndSet(pWave, Localize("Wave over the whole name"), pWave, &View, LineSize);

			DoLine_ColorPicker(&s_aColorButtons[Index][0], ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &View, Localize("Color 1"), pColor1, Default1, false);
			DoLine_ColorPicker(&s_aColorButtons[Index][1], ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &View, Localize("Color 2"), pColor2, Default2, false);
			DoLine_ColorPicker(&s_aColorButtons[Index][2], ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &View, Localize("Color 3"), pColor3, Default3, false);

			// Preview strip
			View.HSplitTop(MarginSmall, nullptr, &View);
			CUIRect PreviewRect;
			View.HSplitTop(36.0f, &PreviewRect, &View);
			DrawNameGradientPreview(PreviewRect, *pColor1, *pColor2, *pColor3, *pWave != 0);

			return View.y - GroupStartY;
		};

		CUIRect OwnView, FriendView, OtherView, Rest;
		const float ColumnGap = 15.0f;
		const float ColumnWidth = (MainView.w - 2.0f * ColumnGap) / 3.0f;
		MainView.VSplitLeft(ColumnWidth, &OwnView, &Rest);
		Rest.VSplitLeft(ColumnGap, nullptr, &Rest);
		Rest.VSplitLeft(ColumnWidth, &FriendView, &Rest);
		Rest.VSplitLeft(ColumnGap, nullptr, &OtherView);

		const float H0 = DoGroup(0, OwnView, Localize("Me (main, dummy, extra)"),
			&g_Config.m_ClNameGradientOwn, &g_Config.m_ClNameGradientOwnSpeed, &g_Config.m_ClNameGradientOwnMode, &g_Config.m_ClNameGradientOwnWave,
			&g_Config.m_ClNameGradientOwnColor1, &g_Config.m_ClNameGradientOwnColor2, &g_Config.m_ClNameGradientOwnColor3,
			ColorRGBA(1.0f, 0.0f, 0.0f), ColorRGBA(0.0f, 1.0f, 0.0f), ColorRGBA(0.0f, 0.0f, 1.0f));
		const float H1 = DoGroup(1, FriendView, Localize("Friends"),
			&g_Config.m_ClNameGradientFriend, &g_Config.m_ClNameGradientFriendSpeed, &g_Config.m_ClNameGradientFriendMode, &g_Config.m_ClNameGradientFriendWave,
			&g_Config.m_ClNameGradientFriendColor1, &g_Config.m_ClNameGradientFriendColor2, &g_Config.m_ClNameGradientFriendColor3,
			ColorRGBA(1.0f, 0.4f, 0.7f), ColorRGBA(1.0f, 0.6f, 0.2f), ColorRGBA(1.0f, 0.9f, 0.2f));
		const float H2 = DoGroup(2, OtherView, Localize("Others"),
			&g_Config.m_ClNameGradientOther, &g_Config.m_ClNameGradientOtherSpeed, &g_Config.m_ClNameGradientOtherMode, &g_Config.m_ClNameGradientOtherWave,
			&g_Config.m_ClNameGradientOtherColor1, &g_Config.m_ClNameGradientOtherColor2, &g_Config.m_ClNameGradientOtherColor3,
			ColorRGBA(0.0f, 1.0f, 1.0f), ColorRGBA(0.8f, 0.0f, 1.0f), ColorRGBA(0.0f, 0.0f, 1.0f));

		CUIRect UsedRect = MainView;
		UsedRect.y = StartY;
		UsedRect.h = std::max({H0, H1, H2}) + 10.0f;
		s_NameGradientScrollRegion.AddRect(UsedRect);

		s_NameGradientScrollRegion.End();
		return;
	}
	// ***** Trail tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_TRAIL)
	{
		// Обновляем список спрайтов, когда заходим в меню
		static bool s_SpritesLoaded = false;
		if(!s_SpritesLoaded)
		{
			GameClient()->m_Trails.RefreshAvailableSprites();
			s_SpritesLoaded = true;
		}

		const float LineSize = 20.0f;
		const float ColorPickerLineSize = 25.0f;
		const float ColorPickerLabelSize = 13.0f;
		const float ColorPickerLineSpacing = 5.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		static CButtonContainer s_aTrailEnableCheckbox[3];
		static CButtonContainer s_aTrailModeSolid[3];
		static CButtonContainer s_aTrailModeRandom[3];
		static CButtonContainer s_aTrailColorPicker[3];

		static CScrollRegion s_TrailScrollRegion;
		CScrollRegionParams TrailScrollParams;
		TrailScrollParams.m_ScrollUnit = 60.0f;
		s_TrailScrollRegion.Begin(&MainView, &TrailScrollParams);
		const float StartY = MainView.y;

		CUIRect Left, Right, Button, Row;

		MainView.VSplitMid(&Left, &Right, 20.0f);

		// ---- Global settings (left) ----
		Ui()->DoLabel_AutoLineSize(Localize("Trail"), HeadlineFontSize, TEXTALIGN_ML, &Left, HeadlineHeight);
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTrail, Localize("Enable trails"), &g_Config.m_ClTrail, &Left, LineSize);

		// ---- Per-group colors (right) ----
		Ui()->DoLabel_AutoLineSize(Localize("Colors per group"), HeadlineFontSize, TEXTALIGN_ML, &Right, HeadlineHeight);
		Right.HSplitTop(MarginSmall, nullptr, &Right);
		auto DrawGroupBox = [&](CUIRect Box) {
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.3f, 0.3f, 0.3f, 0.5f);
			float t = 1.0f;
			IGraphics::CQuadItem QTop(Box.x, Box.y, Box.w, t);
			IGraphics::CQuadItem QBot(Box.x, Box.y + Box.h - t, Box.w, t);
			IGraphics::CQuadItem QLeft(Box.x, Box.y, t, Box.h);
			IGraphics::CQuadItem QRight(Box.x + Box.w - t, Box.y, t, Box.h);
			Graphics()->QuadsDrawTL(&QTop, 1);
			Graphics()->QuadsDrawTL(&QBot, 1);
			Graphics()->QuadsDrawTL(&QLeft, 1);
			Graphics()->QuadsDrawTL(&QRight, 1);
			Graphics()->QuadsEnd();
		};

		struct SGroup
		{
			const char *m_pTitle;
			int *m_pEnable;
			int *m_pStyle;
			int *m_pSize;
			int *m_pRibbonWidth;
			int *m_pColorMode;
			unsigned int *m_pColor;
			int *m_pSpacing;
			int *m_pLifetime;
			int *m_pFadeTime;
			int *m_pFadeStyle;
			int *m_pNarrowTime;
		};
		const SGroup aGroups[3] = {
			{Localize("Own"), &g_Config.m_ClTrailOwn, &g_Config.m_ClTrailOwnStyle, &g_Config.m_ClTrailOwnSize, &g_Config.m_ClTrailOwnRibbonWidth,
				&g_Config.m_ClTrailOwnColorMode, &g_Config.m_ClTrailOwnColor, &g_Config.m_ClTrailOwnSpacing,
				&g_Config.m_ClTrailOwnLifetime, &g_Config.m_ClTrailOwnFadeTime, &g_Config.m_ClTrailOwnFadeStyle, &g_Config.m_ClTrailOwnNarrowTime},
			{Localize("Friends"), &g_Config.m_ClTrailFriend, &g_Config.m_ClTrailFriendStyle, &g_Config.m_ClTrailFriendSize, &g_Config.m_ClTrailFriendRibbonWidth,
				&g_Config.m_ClTrailFriendColorMode, &g_Config.m_ClTrailFriendColor, &g_Config.m_ClTrailFriendSpacing,
				&g_Config.m_ClTrailFriendLifetime, &g_Config.m_ClTrailFriendFadeTime, &g_Config.m_ClTrailFriendFadeStyle, &g_Config.m_ClTrailFriendNarrowTime},
			{Localize("Others"), &g_Config.m_ClTrailOther, &g_Config.m_ClTrailOtherStyle, &g_Config.m_ClTrailOtherSize, &g_Config.m_ClTrailOtherRibbonWidth,
				&g_Config.m_ClTrailOtherColorMode, &g_Config.m_ClTrailOtherColor, &g_Config.m_ClTrailOtherSpacing,
				&g_Config.m_ClTrailOtherLifetime, &g_Config.m_ClTrailOtherFadeTime, &g_Config.m_ClTrailOtherFadeStyle, &g_Config.m_ClTrailOtherNarrowTime},
		};

		static CButtonContainer s_aTrailStyleButtons[3][3];

		for(int Group = 0; Group < 3; ++Group)
		{
			// Отступ между группами
			Right.HSplitTop(30.0f, nullptr, &Right);

			// Запоминаем начало группы для отрисовки рамки
			CUIRect GroupRect = Right;
			float GroupStartY = Right.y;

			// Заголовок группы
			CUIRect HeaderRect;
			Right.HSplitTop(24.0f, &HeaderRect, &Right);
			HeaderRect.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 0.8f), IGraphics::CORNER_ALL, 5.0f);
			
			TextRender()->TextColor(0.2f, 0.8f, 1.0f, 1.0f); // Синий цвет заголовка
			Ui()->DoLabel(&HeaderRect, aGroups[Group].m_pTitle, 16.0f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor()); // Сброс цвета

			Right.HSplitTop(5.0f, nullptr, &Right);

			// Чекбокс включения
			DoButton_CheckBoxAutoVMarginAndSet(&s_aTrailEnableCheckbox[Group], Localize("Enable"), aGroups[Group].m_pEnable, &Right, LineSize);

			if(!*aGroups[Group].m_pEnable)
			{
				// Если группа выключена, рисуем рамку и идем дальше
				GroupRect.h = Right.y - GroupStartY;
				DrawGroupBox(GroupRect);
				continue;
			}

			// Style row: Dashes / Ribbon / Neon
			Right.HSplitTop(LineSize, &Row, &Right);
			CUIRect StyleDash, StyleRibbon, StyleNeon;
			Row.VSplitLeft(Row.w / 3.0f, &StyleDash, &Row);
			Row.VSplitLeft(Row.w / 2.0f, &StyleRibbon, &StyleNeon);
			if(DoButton_CheckBox(&s_aTrailStyleButtons[Group][0], Localize("Dashes"), *aGroups[Group].m_pStyle == 0, &StyleDash))
				*aGroups[Group].m_pStyle = 0;
			if(DoButton_CheckBox(&s_aTrailStyleButtons[Group][1], Localize("Ribbon"), *aGroups[Group].m_pStyle == 1, &StyleRibbon))
				*aGroups[Group].m_pStyle = 1;
			if(DoButton_CheckBox(&s_aTrailStyleButtons[Group][2], Localize("Neon"), *aGroups[Group].m_pStyle == 2, &StyleNeon))
				*aGroups[Group].m_pStyle = 2;

			// Size row — depends on style (dash: segment size, ribbon/neon: ribbon width)
			if(*aGroups[Group].m_pStyle == 0)
			{
				Right.HSplitTop(LineSize, &Row, &Right);
				Ui()->DoScrollbarOption(aGroups[Group].m_pSize, aGroups[Group].m_pSize, &Row, Localize("Dash size"), 1, 50);

				// Dash sprite toggle (global for all groups).
				static CButtonContainer s_aDashSpriteToggle[3];
				Right.HSplitTop(LineSize, &Row, &Right);
				if(DoButton_CheckBox(&s_aDashSpriteToggle[Group], Localize("Use sprite"), g_Config.m_ClTrailDashSprite, &Row))
					g_Config.m_ClTrailDashSprite ^= 1;

				if(g_Config.m_ClTrailDashSprite)
				{
					// Переключатель режима
					static CButtonContainer s_aSpriteModeSelected[3];
					static CButtonContainer s_aSpriteModeCycle[3];
					Right.HSplitTop(LineSize, &Row, &Right);
					CUIRect ModeSel, ModeCyc;
					Row.VSplitMid(&ModeSel, &ModeCyc);
					if(DoButton_CheckBox(&s_aSpriteModeSelected[Group], Localize("Selected sprite"), g_Config.m_ClTrailDashSpriteMode == 0, &ModeSel))
						g_Config.m_ClTrailDashSpriteMode = 0;
					if(DoButton_CheckBox(&s_aSpriteModeCycle[Group], Localize("Cycle from folder"), g_Config.m_ClTrailDashSpriteMode == 1, &ModeCyc))
						g_Config.m_ClTrailDashSpriteMode = 1;

					// Header row: Label and Refresh button
					Right.HSplitTop(LineSize, &Row, &Right);
					CUIRect LabelRect, RefreshBtn;
					Row.VSplitRight(80.0f, &LabelRect, &RefreshBtn);
					
					const auto &vSprites = GameClient()->m_Trails.GetAvailableSprites();
					if(g_Config.m_ClTrailDashSpriteMode == 0)
					{
						char aBuf[128];
						str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Selected"), g_Config.m_ClTrailDashSpriteName);
						Ui()->DoLabel(&LabelRect, aBuf, 14.0f, TEXTALIGN_ML);
					}
					else
					{
						Ui()->DoLabel(&LabelRect, Localize("Available sprites:"), 14.0f, TEXTALIGN_ML);
					}
					
					static CButtonContainer s_aRefreshSprites[3];
					if(DoButton_Menu(&s_aRefreshSprites[Group], Localize("Refresh"), 0, &RefreshBtn))
						GameClient()->m_Trails.RefreshAvailableSprites();

					// Sprite buttons grid (только в режиме Selected)
					if(g_Config.m_ClTrailDashSpriteMode == 0)
					{
						if(!vSprites.empty())
						{
							static CButtonContainer s_aSpriteButtons[3][32];
							const int SpritesPerRow = 3;
							int Col = 0;
							for(size_t s = 0; s < vSprites.size() && s < 32; ++s)
							{
								if(Col == 0)
									Right.HSplitTop(LineSize, &Row, &Right);
								
								CUIRect Btn;
								Row.VSplitLeft(Row.w / SpritesPerRow, &Btn, &Row);
								
								const bool Active = str_comp(g_Config.m_ClTrailDashSpriteName, vSprites[s].c_str()) == 0;
								if(DoButton_Menu(&s_aSpriteButtons[Group][s], vSprites[s].c_str(), Active, &Btn))
									str_copy(g_Config.m_ClTrailDashSpriteName, vSprites[s].c_str(), sizeof(g_Config.m_ClTrailDashSpriteName));
								
								Col = (Col + 1) % SpritesPerRow;
							}
						}
						else
						{
							Right.HSplitTop(LineSize, &Row, &Right);
							Ui()->DoLabel(&Row, Localize("No PNG files found in trail/"), 14.0f, TEXTALIGN_ML);
						}
					}

					// Sprite Size slider
					Right.HSplitTop(LineSize, &Row, &Right);
					Ui()->DoScrollbarOption(&g_Config.m_ClTrailDashSpriteSize, &g_Config.m_ClTrailDashSpriteSize, &Row, Localize("Sprite size (%)"), 10, 500);

					// Rotate toggle
					static CButtonContainer s_aDashRotateToggle[3];
					Right.HSplitTop(LineSize, &Row, &Right);
					if(DoButton_CheckBox(&s_aDashRotateToggle[Group], Localize("Rotate along direction"), g_Config.m_ClTrailDashRotate, &Row))
						g_Config.m_ClTrailDashRotate ^= 1;

					// Swing toggle
					static CButtonContainer s_aDashSwingToggle[3];
					Right.HSplitTop(LineSize, &Row, &Right);
					if(DoButton_CheckBox(&s_aDashSwingToggle[Group], Localize("Swing rotation"), g_Config.m_ClTrailDashSwing, &Row))
						g_Config.m_ClTrailDashSwing ^= 1;

					if(g_Config.m_ClTrailDashSwing)
					{
						Right.HSplitTop(LineSize, &Row, &Right);
						Ui()->DoScrollbarOption(&g_Config.m_ClTrailDashSwingAngle, &g_Config.m_ClTrailDashSwingAngle, &Row, Localize("Swing angle (°)"), 0, 180);

						Right.HSplitTop(LineSize, &Row, &Right);
						Ui()->DoScrollbarOption(&g_Config.m_ClTrailDashSwingSpeed, &g_Config.m_ClTrailDashSwingSpeed, &Row, Localize("Swing speed (%)"), 10, 500);
					}
				}
			}
			else
			{
				Right.HSplitTop(LineSize, &Row, &Right);
				Ui()->DoScrollbarOption(aGroups[Group].m_pRibbonWidth, aGroups[Group].m_pRibbonWidth, &Row, Localize("Ribbon width"), 1, 100);
			}

			// Color mode row: Solid / Random
			Right.HSplitTop(LineSize, &Row, &Right);
			CUIRect ModeSolid, ModeRandom;
			Row.VSplitMid(&ModeSolid, &ModeRandom);
			if(DoButton_CheckBox(&s_aTrailModeSolid[Group], Localize("Solid"), *aGroups[Group].m_pColorMode == 0, &ModeSolid))
				*aGroups[Group].m_pColorMode = 0;
			if(DoButton_CheckBox(&s_aTrailModeRandom[Group], Localize("Random"), *aGroups[Group].m_pColorMode == 1, &ModeRandom))
				*aGroups[Group].m_pColorMode = 1;

			if(*aGroups[Group].m_pColorMode == 0)
			{
				DoLine_ColorPicker(&s_aTrailColorPicker[Group], ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Right,
					Localize("Color"), aGroups[Group].m_pColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			}
			else
			{
				Right.HSplitTop(ColorPickerLineSize, nullptr, &Right);
			}

			// Spacing (per-group)
			Right.HSplitTop(LineSize, &Row, &Right);
			Ui()->DoScrollbarOption(aGroups[Group].m_pSpacing, aGroups[Group].m_pSpacing, &Row, Localize("Spacing"), 1, 100);

			// Timing (per-group)
			Right.HSplitTop(LineSize, &Row, &Right);
			Ui()->DoScrollbarOption(aGroups[Group].m_pLifetime, aGroups[Group].m_pLifetime, &Row, Localize("Lifetime (ms)"), 100, 5000);

			Right.HSplitTop(LineSize, &Row, &Right);
			Ui()->DoScrollbarOption(aGroups[Group].m_pFadeTime, aGroups[Group].m_pFadeTime, &Row, Localize("Fade (ms, 0=off)"), 0, 5000);

			// Fade Style
			static CButtonContainer s_aFadeStyleBtn[3][3];
			Right.HSplitTop(LineSize, &Row, &Right);
			CUIRect StyleOpacity, StyleShrink, StyleEase;
			Row.VSplitLeft(Row.w / 3.0f, &StyleOpacity, &Row);
			Row.VSplitLeft(Row.w / 2.0f, &StyleShrink, &StyleEase);
			if(DoButton_CheckBox(&s_aFadeStyleBtn[Group][0], Localize("Opacity"), *aGroups[Group].m_pFadeStyle == 0, &StyleOpacity)) *aGroups[Group].m_pFadeStyle = 0;
			if(DoButton_CheckBox(&s_aFadeStyleBtn[Group][1], Localize("Bounce"), *aGroups[Group].m_pFadeStyle == 1, &StyleShrink)) *aGroups[Group].m_pFadeStyle = 1;
			if(DoButton_CheckBox(&s_aFadeStyleBtn[Group][2], Localize("Ease-Out"), *aGroups[Group].m_pFadeStyle == 2, &StyleEase)) *aGroups[Group].m_pFadeStyle = 2;

			Right.HSplitTop(LineSize, &Row, &Right);
			Ui()->DoScrollbarOption(aGroups[Group].m_pNarrowTime, aGroups[Group].m_pNarrowTime, &Row, Localize("Narrow (ms)"), 0, 2000);

			// Preview strip
			Right.HSplitTop(MarginSmall, nullptr, &Right);
			CUIRect PreviewRect;
			Right.HSplitTop(100.0f, &PreviewRect, &Right);
			DrawTrailPreview(PreviewRect, Group);

			// Отрисовываем рамку вокруг всей группы
			GroupRect.h = Right.y - GroupStartY;
			DrawGroupBox(GroupRect);
		}

		CUIRect UsedRect = MainView;
		UsedRect.y = StartY;
		UsedRect.h = std::max(Left.y, Right.y) - StartY + 10.0f;
		s_TrailScrollRegion.AddRect(UsedRect);

		s_TrailScrollRegion.End();
		return;
	}
	// ***** Weather tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_WEATHER)
	{
		const float LineSize = 20.0f;
		const float ColorPickerLineSize = 25.0f;
		const float ColorPickerLabelSize = 13.0f;
		const float ColorPickerLineSpacing = 5.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		static CScrollRegion s_WeatherScrollRegion;
		CScrollRegionParams WeatherScrollParams;
		WeatherScrollParams.m_ScrollUnit = 60.0f;
		s_WeatherScrollRegion.Begin(&MainView, &WeatherScrollParams);
		const float StartY = MainView.y;

		CUIRect Left, Button;
		MainView.VSplitMid(&Left, nullptr, 20.0f);

		Ui()->DoLabel_AutoLineSize(Localize("Weather"), HeadlineFontSize, TEXTALIGN_ML, &Left, HeadlineHeight);
		Left.HSplitTop(MarginSmall, nullptr, &Left);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClWeather, Localize("Enable weather effects"), &g_Config.m_ClWeather, &Left, LineSize);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClWeatherSnowCount, &g_Config.m_ClWeatherSnowCount, &Button, Localize("Snowflakes count"), 50, 2000);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClWeatherSnowSpeed, &g_Config.m_ClWeatherSnowSpeed, &Button, Localize("Snow speed"), 10, 500, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClWeatherSnowSize, &g_Config.m_ClWeatherSnowSize, &Button, Localize("Snowflake size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClWeatherSnowOpacity, &g_Config.m_ClWeatherSnowOpacity, &Button, Localize("Snow opacity"), 10, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClWeatherSnowTileCollideChance, &g_Config.m_ClWeatherSnowTileCollideChance, &Button, Localize("Chance of landing on tiles"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		// Цвет снежинок (с альфой). Переменная CFGFLAG_COLALPHA,
		// поэтому последний аргумент DoLine_ColorPicker = true.
		static CButtonContainer s_SnowColorPicker;
		DoLine_ColorPicker(&s_SnowColorPicker, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing,
			&Left, Localize("Snowflake color"), &g_Config.m_ClWeatherSnowColor,
			ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, true);

		CUIRect UsedRect = MainView;
		UsedRect.y = StartY;
		UsedRect.h = Left.y - StartY + 10.0f;
		s_WeatherScrollRegion.AddRect(UsedRect);

		s_WeatherScrollRegion.End();
		return;
	}
	// ***** Tee Rotation tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_TEE_ROTATION)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		static CScrollRegion s_TeeRotationScrollRegion;
		CScrollRegionParams TeeRotationScrollParams;
		TeeRotationScrollParams.m_ScrollUnit = 60.0f;
		s_TeeRotationScrollRegion.Begin(&MainView, &TeeRotationScrollParams);
		const float StartY = MainView.y;

		CUIRect Left, Row;
		MainView.VSplitMid(&Left, nullptr, 20.0f);

		Ui()->DoLabel_AutoLineSize(Localize("Tee Rotation"), HeadlineFontSize, TEXTALIGN_ML, &Left, HeadlineHeight);
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTeeRotation, Localize("Enable tee rotation"), &g_Config.m_ClTeeRotation, &Left, LineSize);

		Left.HSplitTop(LineSize, &Row, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClTeeRotationSpeed, &g_Config.m_ClTeeRotationSpeed, &Row,
			Localize("Speed"), 1, 500, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");

		static CButtonContainer s_aModeButtons[2];
		Left.HSplitTop(LineSize, &Row, &Left);
		{
			CUIRect ModeLinear, ModeSwing;
			Row.VSplitMid(&ModeLinear, &ModeSwing);
			if(DoButton_CheckBox(&s_aModeButtons[0], Localize("Linear"), g_Config.m_ClTeeRotationMode == 0, &ModeLinear))
				g_Config.m_ClTeeRotationMode = 0;
			if(DoButton_CheckBox(&s_aModeButtons[1], Localize("Swing"), g_Config.m_ClTeeRotationMode == 1, &ModeSwing))
				g_Config.m_ClTeeRotationMode = 1;
		}

		if(g_Config.m_ClTeeRotationMode == 1)
		{
			Left.HSplitTop(LineSize, &Row, &Left);
			Ui()->DoScrollbarOption(&g_Config.m_ClTeeRotationSwingAngle, &g_Config.m_ClTeeRotationSwingAngle,
				&Row, Localize("Swing angle"), 0, 180, &CUi::ms_LinearScrollbarScale, 0u, "°");
		}

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		Left.HSplitTop(20.0f, &Row, &Left);
		Ui()->DoLabel(&Row, Localize("Apply to"), 14.0f, TEXTALIGN_ML);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTeeRotationOwn,   Localize("Own tee (main)"), &g_Config.m_ClTeeRotationOwn,   &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTeeRotationDummy, Localize("Dummy tee"),      &g_Config.m_ClTeeRotationDummy, &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTeeRotationExtra, Localize("Extra tees"),     &g_Config.m_ClTeeRotationExtra, &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTeeRotationOthers, Localize("Other players"), &g_Config.m_ClTeeRotationOthers, &Left, LineSize);

		CUIRect UsedRect = MainView;
		UsedRect.y = StartY;
		UsedRect.h = Left.y - StartY + 10.0f;
		s_TeeRotationScrollRegion.AddRect(UsedRect);

		s_TeeRotationScrollRegion.End();
		return;
	}
	// ***** Direction Rotation tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_DIRECTION_ROTATION)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		static CScrollRegion s_DirRotationScrollRegion;
		CScrollRegionParams DirRotationScrollParams;
		DirRotationScrollParams.m_ScrollUnit = 60.0f;
		s_DirRotationScrollRegion.Begin(&MainView, &DirRotationScrollParams);
		const float StartY = MainView.y;

		CUIRect Left, Row;
		MainView.VSplitMid(&Left, nullptr, 20.0f);

		Ui()->DoLabel_AutoLineSize(Localize("Direction Rotation"), HeadlineFontSize, TEXTALIGN_ML, &Left, HeadlineHeight);
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDirRotation, Localize("Enable direction rotation"), &g_Config.m_ClDirRotation, &Left, LineSize);

		Left.HSplitTop(LineSize, &Row, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClDirRotationSpeed, &g_Config.m_ClDirRotationSpeed, &Row,
			Localize("Speed"), 1, 1000, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
		static CButtonContainer s_aDirModeButtons[4];
		Left.HSplitTop(LineSize, &Row, &Left);
		{
			CUIRect ModeLinear, ModeSwing, ModeLoading, ModeStep;
			Row.VSplitLeft(Row.w / 4.0f, &ModeLinear, &Row);
			Row.VSplitLeft(Row.w / 3.0f, &ModeSwing, &Row);
			Row.VSplitMid(&ModeLoading, &ModeStep);
			if(DoButton_CheckBox(&s_aDirModeButtons[0], Localize("Linear"), g_Config.m_ClDirRotationMode == 0, &ModeLinear))
				g_Config.m_ClDirRotationMode = 0;
			if(DoButton_CheckBox(&s_aDirModeButtons[1], Localize("Swing"), g_Config.m_ClDirRotationMode == 1, &ModeSwing))
				g_Config.m_ClDirRotationMode = 1;
			if(DoButton_CheckBox(&s_aDirModeButtons[2], Localize("Loading"), g_Config.m_ClDirRotationMode == 2, &ModeLoading))
				g_Config.m_ClDirRotationMode = 2;
			if(DoButton_CheckBox(&s_aDirModeButtons[3], Localize("Step"), g_Config.m_ClDirRotationMode == 3, &ModeStep))
				g_Config.m_ClDirRotationMode = 3;
		}

		if(g_Config.m_ClDirRotationMode == 1)
		{
			Left.HSplitTop(LineSize, &Row, &Left);
			Ui()->DoScrollbarOption(&g_Config.m_ClDirRotationSwingAngle, &g_Config.m_ClDirRotationSwingAngle,
				&Row, Localize("Swing angle"), 0, 1800, &CUi::ms_LinearScrollbarScale, 0u, "°");
		}
		else if(g_Config.m_ClDirRotationMode == 3)
		{
			Left.HSplitTop(LineSize, &Row, &Left);
			Ui()->DoScrollbarOption(&g_Config.m_ClDirRotationStepAngle, &g_Config.m_ClDirRotationStepAngle,
				&Row, Localize("Step angle"), 1, 360, &CUi::ms_LinearScrollbarScale, 0u, "°");
		}

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		Left.HSplitTop(20.0f, &Row, &Left);
		Ui()->DoLabel(&Row, Localize("Apply to"), 14.0f, TEXTALIGN_ML);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDirRotationOwn,   Localize("Own direction (main)"), &g_Config.m_ClDirRotationOwn,   &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDirRotationDummy, Localize("Dummy direction"),      &g_Config.m_ClDirRotationDummy, &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDirRotationExtra, Localize("Extra directions"),     &g_Config.m_ClDirRotationExtra, &Left, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDirRotationOthers, Localize("Other players"),       &g_Config.m_ClDirRotationOthers, &Left, LineSize);

		CUIRect UsedRect = MainView;
		UsedRect.y = StartY;
		UsedRect.h = Left.y - StartY + 10.0f;
		s_DirRotationScrollRegion.AddRect(UsedRect);

		s_DirRotationScrollRegion.End();
		return;
	}

	// ***** Force Skin tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_FORCE_SKIN)
	{
		const float LineSize = 20.0f;
		const float HeadlineFontSize = 20.0f;
		const float HeadlineHeight = 30.0f;
		const float MarginSmall = 5.0f;

		// Прямоугольник пикера от прошлого кадра — для гейта колеса.
		static CUIRect s_LastPickerRect = {0, 0, 0, 0};
		static bool s_LastPickerActive = false;
		const bool WheelOverPicker = s_LastPickerActive && Ui()->MouseInside(&s_LastPickerRect);

		// OuterClip = до Begin (Begin сдвинет MainView.y на -ScrollY).
		// Нужен, чтобы восстановить клип после вложенного CScrollRegion пикера.
		const CUIRect OuterClip = MainView;

		static CScrollRegion s_ForceSkinScrollRegion;
		CScrollRegionParams ForceSkinScrollParams;
		// 0 → регион не реагирует на колесо, когда мышь над пикером.
		ForceSkinScrollParams.m_ScrollUnit = WheelOverPicker ? 0.0f : 60.0f;
		s_ForceSkinScrollRegion.Begin(&MainView, &ForceSkinScrollParams);
		const float StartY = MainView.y;

		CUIRect Left, Row;
		MainView.VSplitMid(&Left, nullptr, 20.0f);

		s_LastPickerActive = false;

		{
			CUIRect TitleRect;
			Left.HSplitTop(HeadlineHeight, &TitleRect, &Left);
			Ui()->DoLabel(&TitleRect, Localize("Force Skin"), HeadlineFontSize, TEXTALIGN_ML);
		}
		Left.HSplitTop(10.0f, nullptr, &Left);

		// Mode
		static CButtonContainer s_aModeButtons[4];
		Left.HSplitTop(LineSize, &Row, &Left);
		{
			CUIRect ModeOff, ModeMy, ModeCustom, ModeRandom;
			Row.VSplitLeft(Row.w / 4.0f, &ModeOff, &Row);
			Row.VSplitLeft(Row.w / 3.0f, &ModeMy, &Row);
			Row.VSplitMid(&ModeCustom, &ModeRandom);
			if(DoButton_CheckBox(&s_aModeButtons[0], Localize("Off"), g_Config.m_ClForceSkinMode == 0, &ModeOff))
				g_Config.m_ClForceSkinMode = 0;
			if(DoButton_CheckBox(&s_aModeButtons[1], Localize("My Skin"), g_Config.m_ClForceSkinMode == 1, &ModeMy))
				g_Config.m_ClForceSkinMode = 1;
			if(DoButton_CheckBox(&s_aModeButtons[2], Localize("Custom"), g_Config.m_ClForceSkinMode == 2, &ModeCustom))
				g_Config.m_ClForceSkinMode = 2;
			if(DoButton_CheckBox(&s_aModeButtons[3], Localize("Random"), g_Config.m_ClForceSkinMode == 3, &ModeRandom))
				g_Config.m_ClForceSkinMode = 3;
		}

		// Custom: skin name + пикер (фикс 280px)
		if(g_Config.m_ClForceSkinMode == 2)
		{
			Left.HSplitTop(10.0f, nullptr, &Left);
			Left.HSplitTop(LineSize, &Row, &Left);
			CUIRect Label, Input, UseMineBtn;
			Row.VSplitLeft(110.0f, &Label, &Input);
			Input.VSplitRight(180.0f, &Input, &UseMineBtn);
			Input.VSplitRight(6.0f, &Input, nullptr);
			Ui()->DoLabel(&Label, Localize("Skin name:"), 14.0f, TEXTALIGN_ML);
			static CLineInput s_SkinNameInput(g_Config.m_ClForceSkinName, sizeof(g_Config.m_ClForceSkinName));
			s_SkinNameInput.SetEmptyText("default");
			Ui()->DoEditBox(&s_SkinNameInput, &Input, 12.0f);

			static CButtonContainer s_UseMineBtn;
			if(DoButton_Menu(&s_UseMineBtn, Localize("Use my current skin"), 0, &UseMineBtn))
				str_copy(g_Config.m_ClForceSkinName, g_Config.m_ClPlayerSkin);

			Left.HSplitTop(10.0f, nullptr, &Left);
			CUIRect Picker;
			Left.HSplitTop(280.0f, &Picker, &Left);   // фиксированная высота
			DoForceSkinPicker(Picker);

			// Запомнить rect для гейта колеса на следующем кадре.
			s_LastPickerRect = Picker;
			s_LastPickerActive = true;

			// CScrollRegion::End() пикера вызвал ClipDisable() — вернуть клип таба,
			// иначе элементы ниже рисуются за пределами MainView.
			Ui()->ClipEnable(&OuterClip);

			Left.HSplitTop(10.0f, nullptr, &Left);
		}

		// Random: seed + reroll
		if(g_Config.m_ClForceSkinMode == 3)
		{
			Left.HSplitTop(MarginSmall, nullptr, &Left);
			Left.HSplitTop(LineSize, &Row, &Left);
			Ui()->DoScrollbarOption(&g_Config.m_ClForceSkinRandomSeed, &g_Config.m_ClForceSkinRandomSeed, &Row,
				Localize("Random seed"), 0, 1000000, &CUi::ms_LinearScrollbarScale, 0u);

			Left.HSplitTop(16.0f, &Row, &Left);
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
			Ui()->DoLabel(&Row, Localize("0 = stable base distribution (click Reroll to shuffle)"), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			Left.HSplitTop(LineSize, &Row, &Left);
			static CButtonContainer s_RerollBtn;
			if(DoButton_Menu(&s_RerollBtn, Localize("Reroll (new random skins)"), 0, &Row))
				g_Config.m_ClForceSkinRandomSeed = rand() % 1000000 + 1;

			Left.HSplitTop(MarginSmall, nullptr, &Left);
			Left.HSplitTop(16.0f, &Row, &Left);
			Ui()->DoLabel(&Row, Localize("Each player gets a different skin based on their ID + seed."), 12.0f, TEXTALIGN_ML);
		}

		// Exclude own tee
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClForceSkinExcludeLocal, Localize("Do not affect my own tee"), &g_Config.m_ClForceSkinExcludeLocal, &Left, LineSize);

		// Color mode
		if(g_Config.m_ClForceSkinMode != 0)
		{
			Left.HSplitTop(MarginSmall, nullptr, &Left);
			Left.HSplitTop(LineSize, &Row, &Left);
			CUIRect ModeSkin, ModeMy, ModeRandom;
			Row.VSplitLeft(Row.w / 3.0f, &ModeSkin, &Row);
			Row.VSplitMid(&ModeMy, &ModeRandom);
			static CButtonContainer s_aColorModeButtons[3];

			if(DoButton_CheckBox(&s_aColorModeButtons[0], Localize("Skin's own color"),
					!g_Config.m_ClForceSkinUseMyColors && !g_Config.m_ClForceSkinRandomColors, &ModeSkin))
			{
				g_Config.m_ClForceSkinUseMyColors = 0;
				g_Config.m_ClForceSkinRandomColors = 0;
			}
			if(DoButton_CheckBox(&s_aColorModeButtons[1], Localize("My colors"),
					g_Config.m_ClForceSkinUseMyColors, &ModeMy))
			{
				g_Config.m_ClForceSkinUseMyColors = 1;
				g_Config.m_ClForceSkinRandomColors = 0;
			}
			if(DoButton_CheckBox(&s_aColorModeButtons[2], Localize("Random"),
					g_Config.m_ClForceSkinRandomColors, &ModeRandom))
			{
				g_Config.m_ClForceSkinUseMyColors = 0;
				g_Config.m_ClForceSkinRandomColors = 1;
			}

			{
				CUIRect DescRect;
				Left.HSplitTop(LineSize, &DescRect, &Left);
				TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
				Ui()->DoLabel(&DescRect, Localize("Color is applied per-part (body / feet) for all forced skins"), 11.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			if(g_Config.m_ClForceSkinUseMyColors &&
				g_Config.m_ClPlayerColorBody == 0 && g_Config.m_ClPlayerColorFeet == 0)
			{
				Left.HSplitTop(LineSize, &Row, &Left);
				TextRender()->TextColor(1.0f, 0.6f, 0.4f, 1.0f);
				Ui()->DoLabel(&Row, Localize("Set your body/feet colors in the Tee tab first!"), 12.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			if(g_Config.m_ClForceSkinRandomColors)
			{
				Left.HSplitTop(MarginSmall, nullptr, &Left);
				Left.HSplitTop(LineSize, &Row, &Left);
				Ui()->DoScrollbarOption(&g_Config.m_ClForceSkinRandomColorSeed, &g_Config.m_ClForceSkinRandomColorSeed, &Row,
					Localize("Color seed"), 0, 1000000, &CUi::ms_LinearScrollbarScale, 0u);

				Left.HSplitTop(LineSize, &Row, &Left);
				static CButtonContainer s_RerollColorsBtn;
				if(DoButton_Menu(&s_RerollColorsBtn, Localize("Reroll colors"), 0, &Row))
					g_Config.m_ClForceSkinRandomColorSeed = rand() % 1000000 + 1;

				Left.HSplitTop(16.0f, &Row, &Left);
				TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
				Ui()->DoLabel(&Row, Localize("0 = stable base palette, other = shuffled palette"), 11.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}

		// Регистрируем полную высоту контента для CScrollRegion.
		// AddRect можно вызывать один раз — скроллбар покажется со следующего кадра,
		// это невидимо глазу (первый кадр static-высоты = 0).
		CUIRect UsedRect;
		UsedRect.x = MainView.x;
		UsedRect.w = MainView.w;
		UsedRect.y = StartY;
		UsedRect.h = Left.y - StartY + 10.0f;
		s_ForceSkinScrollRegion.AddRect(UsedRect);

		s_ForceSkinScrollRegion.End();

		if(s_LastPickerActive && Ui()->MouseInside(&s_LastPickerRect))
			Ui()->SetHotScrollRegion(&s_ForceSkinPickerScroll);

		return;
	}
	// Создаем область прокрутки для всей страницы
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f; // Скорость прокрутки колесиком
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	// MainView уже автоматически сдвинут вниз на нужную позицию!

	// Отступ сверху
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// === ГАЛОЧКА АВТО-ПЕРЕКЛЮЧЕНИЯ ===
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button); // Сообщаем скроллу о кнопке
	static CButtonContainer s_ExtraAutoSwitchCheckbox;
	if(DoButton_CheckBox(&s_ExtraAutoSwitchCheckbox, Localize("Auto switch to Dummy/Extra on connect"), g_Config.m_ClExtraAutoSwitch, &Button))
		g_Config.m_ClExtraAutoSwitch ^= 1;
	Ui()->DoToolTip(&g_Config.m_ClExtraAutoSwitch, &Button, Localize("Automatically switch camera to Dummy or Extra connections when they join"));
	// =====================================
	// === Рандомизация ника + скинов ===
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	static CButtonContainer s_RandomizeNickSkinCheckbox;
	if(DoButton_CheckBox(&s_RandomizeNickSkinCheckbox, Localize("Randomize nick + skins continuously"), g_Config.m_ClRandomizeNickSkin, &Button))
		g_Config.m_ClRandomizeNickSkin ^= 1;
	Ui()->DoToolTip(&s_RandomizeNickSkinCheckbox, &Button, Localize("While online, periodically change nick (from other players) and skins"));

	if(g_Config.m_ClRandomizeNickSkin)
	{
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		s_ScrollRegion.AddRect(Button);
		Ui()->DoScrollbarOption(&g_Config.m_ClRandomizeNickSkinInterval, &g_Config.m_ClRandomizeNickSkinInterval, &Button,
			Localize("Randomize interval"), 5, 120, &CUi::ms_LinearScrollbarScale, 0u, " s");
	}
	// =====================================
		// Notification duration (Hydra)
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClNotificationDuration, &g_Config.m_ClNotificationDuration, &Button,
		Localize("Notification duration"), 1, 60, &CUi::ms_LinearScrollbarScale, 0u, " s");
	Ui()->DoToolTip(&g_Config.m_ClNotificationDuration, &Button, Localize("How long popup notifications stay on screen (seconds)"));
	// Scroll inertia strength
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClScrollInertiaStrength, &g_Config.m_ClScrollInertiaStrength, &Button, Localize("Scroll inertia"), 10, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

	// Scroll inertia decay
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClScrollInertiaDecay, &g_Config.m_ClScrollInertiaDecay, &Button, Localize("Scroll stop speed"), 10, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

	// Scroll inertia speed
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClScrollInertiaSpeed, &g_Config.m_ClScrollInertiaSpeed, &Button, Localize("Scroll flight speed"), 10, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");

	// Mouse threshold
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClScrollMouseThreshold, &g_Config.m_ClScrollMouseThreshold, &Button, Localize("Min flick speed"), 0, 500, &CUi::ms_LinearScrollbarScale, 0u, " px/s");

	// Drag threshold
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClScrollDragThreshold, &g_Config.m_ClScrollDragThreshold, &Button, Localize("Drag start distance"), 1, 100, &CUi::ms_LinearScrollbarScale, 0u, " px");
	// === Smooth touch scrolling ===
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	static CButtonContainer s_TouchScrollSmoothCheckbox;
	if(DoButton_CheckBox(&s_TouchScrollSmoothCheckbox, Localize("Smooth touch scrolling"), g_Config.m_ClTouchScrollSmooth != 0, &Button))
		g_Config.m_ClTouchScrollSmooth = (g_Config.m_ClTouchScrollSmooth == 0) ? 2 : 0;
	Ui()->DoToolTip(&g_Config.m_ClTouchScrollSmooth, &Button, Localize("Scrolling with momentum like in a browser"));
	// ==============================
	// Enable toggle (Emote cycle)
	MainView.HSplitTop(20.0f, &Button, &MainView);
	static CButtonContainer s_EmoteCycleCheckbox;
	if(DoButton_CheckBox(&s_EmoteCycleCheckbox, Localize("Emote cycle"), g_Config.m_ClTestClientEmoteCycle, &Button))
		g_Config.m_ClTestClientEmoteCycle ^= 1;

	// Description
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Label, &MainView);
	s_ScrollRegion.AddRect(Label);
	TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	Ui()->DoLabel(&Label, Localize("Sends the same emote to all connections simultaneously"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// Слайдер задержки (появляется только если цикл включен)
	if(g_Config.m_ClTestClientEmoteCycle)
	{
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		s_ScrollRegion.AddRect(Button);
		Ui()->DoScrollbarOption(&g_Config.m_ClTestClientEmoteDelay, &g_Config.m_ClTestClientEmoteDelay, &Button, Localize("Delay between emotes"), 5, 10000, &CUi::ms_LinearScrollbarScale, 0u, " ms");
	}

	// Copy moves toggle (имеет смысл только при >= 2 соединениях)
	const bool CopyMovesHasTargets =
		Client()->DummyConnected() ||
		Client()->IsExtraConnOnline(IClient::CONN_EXTRA1) ||
		Client()->IsExtraConnOnline(IClient::CONN_EXTRA2);

	MainView.HSplitTop(12.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);
	static CButtonContainer s_CopyAllCheckbox;
	if(!CopyMovesHasTargets)
	{
		// Рисуем «заблокированный» чекбокс: значение 0, клик не срабатывает.
		DoButton_CheckBox(&s_CopyAllCheckbox, Localize("All connections copy moves"), false, &Button);
		TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
		Ui()->DoToolTip(&s_CopyAllCheckbox, &Button, Localize("Connect dummy or extra first"));
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	else
	{
		if(DoButton_CheckBox(&s_CopyAllCheckbox, Localize("All connections copy moves"), g_Config.m_ClDummyCopyMoves, &Button))
			g_Config.m_ClDummyCopyMoves ^= 1;
	}

	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Label, &MainView);
	s_ScrollRegion.AddRect(Label);
	TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	Ui()->DoLabel(&Label, Localize("All characters (main, dummy, extra1, extra2) receive the same input simultaneously"), 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// Connections section
	MainView.HSplitTop(20.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Label, &MainView);
	s_ScrollRegion.AddRect(Label);
	Ui()->DoLabel(&Label, Localize("Connections"), 14.0f, TEXTALIGN_ML);

	static CButtonContainer s_aConnButtons[3];

	const bool MainOnline = Client()->State() == IClient::STATE_ONLINE;
	const bool aConnected[3] = {
		Client()->DummyConnected(),
		Client()->IsExtraConnOnline(IClient::ExtraIndexToConn(0)),
		Client()->IsExtraConnOnline(IClient::ExtraIndexToConn(1)),
	};
	const char *apConnNames[3] = {
		Localize("Dummy"),
		Localize("Extra 1"),
		Localize("Extra 2"),
	};

	for(int i = 0; i < 3; i++)
	{
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		MainView.HSplitTop(24.0f, &Button, &MainView);
		s_ScrollRegion.AddRect(Button);

		CUIRect StatusRect;
		Button.VSplitRight(140.0f, &StatusRect, &Button);
		Button.VSplitLeft(6.0f, nullptr, &Button);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %s", apConnNames[i], aConnected[i] ? Localize("Online") : Localize("Offline"));
		if(aConnected[i])
			TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);
		else if(!MainOnline)
			TextRender()->TextColor(0.5f, 0.5f, 0.5f, 1.0f);
		else
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
		Ui()->DoLabel(&StatusRect, aBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const char *pBtnText = aConnected[i] ? Localize("Disconnect") : Localize("Connect");
		const bool CanClick = aConnected[i] || MainOnline;

		if(!CanClick)
			TextRender()->TextColor(0.5f, 0.5f, 0.5f, 1.0f);
		const bool Clicked = DoButton_Menu(&s_aConnButtons[i], pBtnText, 0, &Button) != 0;
		if(!CanClick)
			TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(!Clicked || !CanClick)
			continue;

		if(aConnected[i])
		{
			if(i == 0)
				Client()->DummyDisconnect(nullptr);
			else
				Client()->ExtraDisconnect(i - 1);
		}
		else
		{
			if(i == 0)
			{
				Client()->DummyConnect();
			}
			else
			{
				char aAddr[NETADDR_MAXSTRSIZE];
				net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
				Client()->ExtraConnect(i - 1, aAddr);
			}
		}
	}

	// Set selection row
	MainView.HSplitTop(20.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Label, &MainView);
	s_ScrollRegion.AddRect(Label);
	Ui()->DoLabel(&Label, Localize("Emote set"), 14.0f, TEXTALIGN_ML);

	static CButtonContainer s_SetButtons[5];

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Button, &MainView);
	s_ScrollRegion.AddRect(Button);

	float ButtonWidth = (Button.w - 4 * 4.0f) / 5.0f;
	for(int i = 0; i < 5; i++)
	{
		CUIRect Btn;
		Button.VSplitLeft(ButtonWidth, &Btn, &Button);
		if(i < 4) Button.VSplitLeft(4.0f, nullptr, &Button);

		const char *pName = "";
		if(i == 0) pName = Localize("Positive");
		else if(i == 1) pName = Localize("Angry");
		else if(i == 2) pName = Localize("Boring");
		else if(i == 3) pName = Localize("All");
		else if(i == 4) pName = Localize("Custom");

		if(DoButton_Menu(&s_SetButtons[i], pName, g_Config.m_ClTestClientEmoteSet == i, &Btn))
			g_Config.m_ClTestClientEmoteSet = i;
	}

	const int SetIdx = std::clamp(g_Config.m_ClTestClientEmoteSet, 0, 4);

	// Кнопки для добавления/удаления эмоций (только в режиме Custom)
	if(SetIdx == 4)
	{
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(24.0f, &Button, &MainView);
		s_ScrollRegion.AddRect(Button);

		CUIRect MinusBtn, PlusBtn, LabelRect;
		Button.VSplitLeft(24.0f, &MinusBtn, &Button);
		Button.VSplitRight(24.0f, &PlusBtn, &LabelRect);

		static CButtonContainer s_MinusBtn;
		static CButtonContainer s_PlusBtn;

		if(DoButton_Menu(&s_MinusBtn, "-", 0, &MinusBtn) && g_Config.m_ClTestClientEmoteCount > 1)
			g_Config.m_ClTestClientEmoteCount--;
		if(DoButton_Menu(&s_PlusBtn, "+", 0, &PlusBtn) && g_Config.m_ClTestClientEmoteCount < 8)
			g_Config.m_ClTestClientEmoteCount++;
	}

	// Preview / custom pickers
	MainView.HSplitTop(20.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Label, &MainView);
	s_ScrollRegion.AddRect(Label);
	Ui()->DoLabel(&Label, g_Config.m_ClTestClientEmoteSet == 2 ? Localize("Custom emotes") : Localize("Preview"), 14.0f, TEXTALIGN_ML);

	static const int s_aEmoteSets[3][3] = {
		{5, 2, 14},
		{9, 10, 11},
		{3, 4, 12},
	};
	static const int s_aAllEmotes[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

	int aCustomEmotes[8] = {
		std::clamp(g_Config.m_ClTestClientEmoteCustom1, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom2, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom3, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom4, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom5, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom6, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom7, 0, 15),
		std::clamp(g_Config.m_ClTestClientEmoteCustom8, 0, 15),
	};

	const int *pEmotes = nullptr;
	int NumEmotesToShow = 3;

	if(SetIdx <= 2)
	{
		pEmotes = s_aEmoteSets[SetIdx];
	}
	else if(SetIdx == 3)
	{
		pEmotes = s_aAllEmotes;
		NumEmotesToShow = 16;
	}
	else if(SetIdx == 4)
	{
		pEmotes = aCustomEmotes;
		NumEmotesToShow = g_Config.m_ClTestClientEmoteCount;
	}

	int *apCustomVars[8] = {
		&g_Config.m_ClTestClientEmoteCustom1,
		&g_Config.m_ClTestClientEmoteCustom2,
		&g_Config.m_ClTestClientEmoteCustom3,
		&g_Config.m_ClTestClientEmoteCustom4,
		&g_Config.m_ClTestClientEmoteCustom5,
		&g_Config.m_ClTestClientEmoteCustom6,
		&g_Config.m_ClTestClientEmoteCustom7,
		&g_Config.m_ClTestClientEmoteCustom8,
	};

	// Список эмоций напрямую (без CListBox)
	for(int i = 0; i < NumEmotesToShow; i++)
	{
		CUIRect Row;
		MainView.HSplitTop(38.0f, &Row, &MainView);
		s_ScrollRegion.AddRect(Row); // Сообщаем скроллу о строке

		Row.HSplitTop(32.0f, &Button, nullptr);

		CUIRect Icon, Slider;
		Button.VSplitLeft(32.0f, &Icon, &Slider);
		Slider.VSplitLeft(10.0f, nullptr, &Slider);

		const bool Interactive = SetIdx == 4;

		const float IconSize = 24.0f;
		Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[pEmotes[i]]);
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->SetColor(1, 1, 1, Interactive ? 1.0f : 0.55f);
		IGraphics::CQuadItem QuadItem(Icon.x + Icon.w / 2.0f, Icon.y + Icon.h / 2.0f, IconSize, IconSize);
		Graphics()->QuadsDraw(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->SetColor(1, 1, 1, 1);
		Graphics()->WrapNormal();

		if(Interactive)
		{
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "Emote %d", i + 1);
			// Лимит 0-15
			Ui()->DoScrollbarOption(apCustomVars[i], apCustomVars[i], &Slider, aBuf, 0, 15, &CUi::ms_LinearScrollbarScale, 0u);
		}
		else
		{
			char aBuf[16];
			str_format(aBuf, sizeof(aBuf), "#%d", pEmotes[i]);
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
			Ui()->DoLabel(&Slider, aBuf, 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	// Добавляем пустой прямоугольник в конце, чтобы скролл знал полную высоту
	CUIRect EndRect;
	MainView.HSplitTop(10.0f, &EndRect, &MainView);
	s_ScrollRegion.AddRect(EndRect);

	// Завершаем работу скролла
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsAnimations(CUIRect MainView)
{
	static int s_SubTab = 0;
	{
		CUIRect TabBar, TabTransitions, TabButtons;
		MainView.HSplitTop(22.0f, &TabBar, &MainView);
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		TabBar.VSplitMid(&TabTransitions, &TabButtons);

		static CButtonContainer s_TabTransitionsButton, s_TabButtonsButton;
		if(DoButton_MenuTab(&s_TabTransitionsButton, Localize("Transitions"), s_SubTab == 0, &TabTransitions, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_SubTab = 0;
		if(DoButton_MenuTab(&s_TabButtonsButton, Localize("Buttons"), s_SubTab == 1, &TabButtons, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_SubTab = 1;
	}
	if(s_SubTab == 1)
	{
		RenderSettingsButtons(MainView);
		return;
	}

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	const float StartY = MainView.y;

	const float LineSize = 20.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = 30.0f;
	const float MarginSmall = 5.0f;

	CUIRect Left, Row;
	MainView.VSplitMid(&Left, nullptr, 20.0f);

	// Easing buttons row (Linear / Ease-Out / Ease-In-Out / Bounce)
	auto RenderEasing = [&](int *pEasing, CButtonContainer *pButtons) {
		Left.HSplitTop(LineSize, &Row, &Left);
		CUIRect ELinear, EOut, EInOut, EBounce;
		Row.VSplitLeft(Row.w / 4.0f, &ELinear, &Row);
		Row.VSplitLeft(Row.w / 3.0f, &EOut, &Row);
		Row.VSplitMid(&EInOut, &EBounce);

		if(DoButton_CheckBox(&pButtons[0], Localize("Linear"), *pEasing == 0, &ELinear))
			*pEasing = 0;
		if(DoButton_CheckBox(&pButtons[1], Localize("Ease-Out"), *pEasing == 1, &EOut))
			*pEasing = 1;
		if(DoButton_CheckBox(&pButtons[2], Localize("Ease-In-Out"), *pEasing == 2, &EInOut))
			*pEasing = 2;
		if(DoButton_CheckBox(&pButtons[3], Localize("Bounce"), *pEasing == 3, &EBounce))
			*pEasing = 3;
	};

	auto RenderPreview = [&](bool Vertical, int DurationMs, int Easing) {
		CUIRect PreviewRect;
		Left.HSplitTop(110.0f, &PreviewRect, &Left);
		PreviewRect.Draw(ColorRGBA(0.08f, 0.08f, 0.10f, 0.9f), IGraphics::CORNER_ALL, 5.0f);

		CUIRect Inner = PreviewRect;
		Inner.Margin(10.0f, &Inner);
		const float DurationSec = std::max(0.05f, DurationMs / 1000.0f);
		const float LoopDur = DurationSec * 2.0f;
		static const int64_t s_Start = time();
		const float TT = fmodf((time() - s_Start) / (float)time_freq(), LoopDur);
		const bool Forward = TT < DurationSec;
		float Progress = Forward ? TT / DurationSec : (TT - DurationSec) / DurationSec;
		Progress = std::clamp(Progress, 0.0f, 1.0f);
		const float Eased = EaseTabSlide(Progress, Easing);
		const float Distance = Vertical ? Inner.h : Inner.w;
		const float Offset = Forward ? 1.0f : -1.0f;

		CUIRect OldBox = Inner;
		CUIRect NewBox = Inner;
		if(Vertical)
		{
			OldBox.y -= Offset * Eased * Distance;
			NewBox.y += Offset * (1.0f - Eased) * Distance;
		}
		else
		{
			OldBox.x -= Offset * Eased * Distance;
			NewBox.x += Offset * (1.0f - Eased) * Distance;
		}

		Ui()->ClipEnable(&Inner);
		OldBox.Draw(ColorRGBA(0.30f, 0.50f, 0.85f, 0.85f), IGraphics::CORNER_ALL, 6.0f);
		NewBox.Draw(ColorRGBA(0.85f, 0.50f, 0.30f, 0.85f), IGraphics::CORNER_ALL, 6.0f);
		Ui()->ClipDisable();
	};

	// ---------- Start Menu Transition ----------
	{
		CUIRect Title;
		Left.HSplitTop(HeadlineHeight, &Title, &Left);
		Ui()->DoLabel(&Title, Localize("Start Menu Transition"), HeadlineFontSize, TEXTALIGN_ML);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClStartMenuAnimation,
		Localize("Enable start menu transition"), &g_Config.m_ClStartMenuAnimation, &Left, LineSize);

	if(g_Config.m_ClStartMenuAnimation)
	{
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		RenderPreview(true, g_Config.m_ClStartMenuAnimationDuration, g_Config.m_ClStartMenuAnimationEasing);

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		Left.HSplitTop(LineSize, &Row, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClStartMenuAnimationDuration,
			&g_Config.m_ClStartMenuAnimationDuration, &Row, Localize("Duration (ms)"), 100, 1500);

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		static CButtonContainer s_aStartMenuEasing[4];
		RenderEasing(&g_Config.m_ClStartMenuAnimationEasing, s_aStartMenuEasing);
	}

	// ---------- Page Transitions ----------
	Left.HSplitTop(20.0f, nullptr, &Left);
	{
		CUIRect Title;
		Left.HSplitTop(HeadlineHeight, &Title, &Left);
		Ui()->DoLabel(&Title, Localize("Page Transitions"), HeadlineFontSize, TEXTALIGN_ML);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSettingsTabAnimation,
		Localize("Enable page transitions"), &g_Config.m_ClSettingsTabAnimation, &Left, LineSize);

	if(g_Config.m_ClSettingsTabAnimation)
	{
		Left.HSplitTop(MarginSmall, nullptr, &Left);
		RenderPreview(false, g_Config.m_ClSettingsTabAnimationDuration, g_Config.m_ClSettingsTabAnimationEasing);

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		Left.HSplitTop(LineSize, &Row, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClSettingsTabAnimationDuration,
			&g_Config.m_ClSettingsTabAnimationDuration, &Row, Localize("Duration (ms)"), 50, 3000);

		Left.HSplitTop(MarginSmall, nullptr, &Left);
		static CButtonContainer s_aPageEasing[4];
		RenderEasing(&g_Config.m_ClSettingsTabAnimationEasing, s_aPageEasing);
	}
	// Регистрируем высоту всего контента для скролла
	CUIRect UsedRect = MainView;
	UsedRect.y = StartY;
	UsedRect.h = Left.y - StartY + 10.0f;
	s_ScrollRegion.AddRect(UsedRect);
	s_ScrollRegion.End();
}
void CMenus::RenderSettingsButtons(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	const float StartY = MainView.y;

	CUIRect Left;
	MainView.VSplitLeft(std::min(MainView.w, 520.0f), &Left, nullptr);

	// Заголовок
	{
		CUIRect Title;
		Left.HSplitTop(30.0f, &Title, &Left);
		Left.HSplitTop(5.0f, nullptr, &Left);
		Ui()->DoLabel(&Title, Localize("Button outline"), 20.0f, TEXTALIGN_ML);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClButtonOutline, Localize("Enable outline"), &g_Config.m_ClButtonOutline, &Left, 20.0f);
	Left.HSplitTop(10.0f, nullptr, &Left);

	// Строка: подпись слева, элементы справа (высота элементов 24, строка 30)
	auto DoRow = [&](const char *pText, CUIRect &Controls) {
		CUIRect Row, Label;
		Left.HSplitTop(30.0f, &Row, &Left);
		Left.HSplitTop(8.0f, nullptr, &Left);
		Row.VSplitLeft(Row.w * 0.38f, &Label, &Controls);
		Controls.HMargin(3.0f, &Controls);
		Ui()->DoLabel(&Label, pText, 14.0f, TEXTALIGN_ML);
	};

	// Ряд отдельных кнопок с одинаковыми зазорами
	auto DoSegments = [&](CButtonContainer *pButtons, const char *const *ppLabels, const int *pValues, int Num, int *pValue, CUIRect Controls) {
		const float Gap = 6.0f;
		const float ButtonWidth = (Controls.w - Gap * (Num - 1)) / Num;
		for(int i = 0; i < Num; i++)
		{
			CUIRect Button;
			Controls.VSplitLeft(ButtonWidth, &Button, &Controls);
			Controls.VSplitLeft(Gap, nullptr, &Controls);
			if(DoButton_Menu(&pButtons[i], ppLabels[i], *pValue == pValues[i], &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.15f))
				*pValue = pValues[i];
		}
	};

	// Толщина
	{
		CUIRect Controls;
		DoRow(Localize("Outline width"), Controls);
		static CButtonContainer s_aWidthButtons[4];
		const char *apLabels[] = {Localize("Thin"), Localize("Normal"), Localize("Thick"), Localize("Extra thick")};
		const int aValues[] = {2, 4, 8, 12};
		DoSegments(s_aWidthButtons, apLabels, aValues, 4, &g_Config.m_ClButtonOutlineWidth, Controls);
	}

	// Цвета
	static CButtonContainer s_Color1Reset, s_Color2Reset;
	DoLine_ColorPicker(&s_Color1Reset, 30.0f, 14.0f, 8.0f, &Left, Localize("Color 1"),
		(unsigned int *)&g_Config.m_ClButtonOutlineColor1, ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f), false, nullptr, false);
	DoLine_ColorPicker(&s_Color2Reset, 30.0f, 14.0f, 8.0f, &Left, Localize("Color 2"),
		(unsigned int *)&g_Config.m_ClButtonOutlineColor2, ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f), false, nullptr, false);

	// Режим
	{
		CUIRect Controls;
		DoRow(Localize("Mode"), Controls);
		static CButtonContainer s_aModeButtons[2];
		const char *apLabels[] = {Localize("Constant"), Localize("Linear")};
		const int aValues[] = {0, 1};
		DoSegments(s_aModeButtons, apLabels, aValues, 2, &g_Config.m_ClButtonOutlineMode, Controls);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClButtonOutlineAnimate, Localize("Animate outline"), &g_Config.m_ClButtonOutlineAnimate, &Left, 20.0f);
	Left.HSplitTop(8.0f, nullptr, &Left);

	// Скорость мерцания
	if(g_Config.m_ClButtonOutlineAnimate)
	{
		CUIRect Row;
		Left.HSplitTop(20.0f, &Row, &Left);
		Left.HSplitTop(10.0f, nullptr, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClButtonOutlineSpeed, &g_Config.m_ClButtonOutlineSpeed, &Row, Localize("Shimmer speed"), 1, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");
	}

	// Предпросмотр
	{
		Left.HSplitTop(10.0f, nullptr, &Left);
		CUIRect Label, Preview;
		Left.HSplitTop(22.0f, &Label, &Left);
		Ui()->DoLabel(&Label, Localize("Preview"), 14.0f, TEXTALIGN_ML);
		Left.HSplitTop(40.0f, &Preview, &Left);
		Left.HSplitTop(10.0f, nullptr, &Left);
		Preview.VSplitLeft(std::min(Preview.w, 220.0f), &Preview, nullptr);
		static CButtonContainer s_PreviewButton;
		DoButton_Menu(&s_PreviewButton, Localize("Button"), 0, &Preview, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 10.0f, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));
	}

	CUIRect UsedRect = MainView;
	UsedRect.y = StartY;
	UsedRect.h = Left.y - StartY + 10.0f;
	s_ScrollRegion.AddRect(UsedRect);
	s_ScrollRegion.End();
}
// ===================== Profiles =====================
static CScrollRegion s_ProfileListScroll;
static CScrollRegion s_ProfileEditScroll;
static CScrollRegion s_ProfileSkinScroll;

static const char *PROFILES_FILE = "hydra_profiles.txt";
static const int MAX_PROFILES = 64;

static void ProfileSanitize(char *pStr)
{
	for(; *pStr; pStr++)
	{
		if(*pStr == '\t' || *pStr == '\n' || *pStr == '\r')
			*pStr = ' ';
	}
}

void CMenus::ProfileFromCurrent(SProfile &Profile)
{
	str_copy(Profile.m_aPlayerName, g_Config.m_PlayerName);
	str_copy(Profile.m_aClan, g_Config.m_PlayerClan);
	str_copy(Profile.m_aSkin, g_Config.m_ClPlayerSkin);
	Profile.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor;
	Profile.m_ColorBody = g_Config.m_ClPlayerColorBody;
	Profile.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
}

void CMenus::ProfileApply(const SProfile &Profile)
{
	str_copy(g_Config.m_PlayerName, Profile.m_aPlayerName);
	str_copy(g_Config.m_PlayerClan, Profile.m_aClan);
	str_copy(g_Config.m_ClPlayerSkin, Profile.m_aSkin);
	g_Config.m_ClPlayerUseCustomColor = Profile.m_UseCustomColor;
	g_Config.m_ClPlayerColorBody = Profile.m_ColorBody;
	g_Config.m_ClPlayerColorFeet = Profile.m_ColorFeet;

	GameClient()->m_Skins.SkinList().ForceRefresh();
	if(Client()->State() == IClient::STATE_ONLINE)
		GameClient()->SendInfo(false);
}

void CMenus::ProfilesLoad()
{
	m_vProfiles.clear();
	IOHANDLE File = Storage()->OpenFile(PROFILES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char *pData = io_read_all_str(File);
	io_close(File);
	if(!pData)
		return;

	const char *pLine = pData;
	char aLine[512];
	while((pLine = str_next_token(pLine, "\n", aLine, sizeof(aLine))))
	{
		SProfile Profile{};
		const char *pField = aLine;
		char aField[64];
		int Index = 0;
		while((pField = str_next_token(pField, "\t", aField, sizeof(aField))))
		{
			// строковые поля начинаются с '~', чтобы пустые значения не терялись
			const char *pValue = aField[0] == '~' ? aField + 1 : aField;
			switch(Index)
			{
			case 0: str_copy(Profile.m_aName, pValue); break;
			case 1: str_copy(Profile.m_aPlayerName, pValue); break;
			case 2: str_copy(Profile.m_aClan, pValue); break;
			case 3: str_copy(Profile.m_aSkin, pValue); break;
			case 4: Profile.m_UseCustomColor = str_toint(pValue) != 0; break;
			case 5: Profile.m_ColorBody = (unsigned int)str_toint(pValue); break;
			case 6: Profile.m_ColorFeet = (unsigned int)str_toint(pValue); break;
			}
			Index++;
		}
		if(Index >= 7 && (int)m_vProfiles.size() < MAX_PROFILES)
			m_vProfiles.push_back(Profile);
	}
	free(pData);
}

void CMenus::ProfilesSave()
{
	IOHANDLE File = Storage()->OpenFile(PROFILES_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	for(SProfile &Profile : m_vProfiles)
	{
		ProfileSanitize(Profile.m_aName);
		ProfileSanitize(Profile.m_aPlayerName);
		ProfileSanitize(Profile.m_aClan);
		ProfileSanitize(Profile.m_aSkin);

		char aLine[512];
		str_format(aLine, sizeof(aLine), "~%s\t~%s\t~%s\t~%s\t%d\t%u\t%u",
			Profile.m_aName, Profile.m_aPlayerName, Profile.m_aClan, Profile.m_aSkin,
			Profile.m_UseCustomColor, Profile.m_ColorBody, Profile.m_ColorFeet);
		io_write(File, aLine, str_length(aLine));
		io_write_newline(File);
	}
	io_close(File);
}

bool CMenus::DoProfileSkinPicker(CUIRect View, SProfile &Profile)
{
	bool Changed = false;
	auto &SkinList = GameClient()->m_Skins.SkinList();
	if(SkinList.Skins().empty())
		SkinList.ForceRefresh();

	CUIRect SearchRow;
	View.HSplitTop(22.0f, &SearchRow, &View);
	View.HSplitTop(6.0f, nullptr, &View);
	static char s_aSearch[64] = "";
	static CLineInput s_Search(s_aSearch, sizeof(s_aSearch));
	s_Search.SetEmptyText(Localize("Search skins..."));
	Ui()->DoEditBox(&s_Search, &SearchRow, 12.0f);

	std::vector<CSkins::CSkinListEntry *> vFiltered;
	vFiltered.reserve(SkinList.Skins().size());
	for(auto &Entry : SkinList.Skins())
	{
		const CSkins::CSkinContainer *pC = Entry.SkinContainer();
		if(pC == nullptr)
			continue;
		if(s_aSearch[0] != '\0' && str_find_nocase(pC->Name(), s_aSearch) == nullptr)
			continue;
		vFiltered.push_back(&Entry);
	}

	View.Draw(ColorRGBA(0.05f, 0.05f, 0.05f, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	View.Margin(4.0f, &View);

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	ScrollParams.m_ScrollbarThickness = 12.0f;
	ScrollParams.m_ScrollbarMargin = 2.0f;
	s_ProfileSkinScroll.Begin(&View, &ScrollParams);

	const float CellW = 70.0f;
	const float CellH = 92.0f;
	const float CellGap = 4.0f;
	const int PerRow = std::max(1, (int)((View.w + CellGap) / (CellW + CellGap)));

	for(size_t idx = 0; idx < vFiltered.size(); ++idx)
	{
		CSkins::CSkinListEntry *pEntry = vFiltered[idx];
		const CSkins::CSkinContainer *pC = pEntry->SkinContainer();
		const char *pName = pC->Name();

		CUIRect Cell;
		Cell.x = View.x + ((int)idx % PerRow) * (CellW + CellGap);
		Cell.y = View.y + ((int)idx / PerRow) * (CellH + CellGap);
		Cell.w = CellW;
		Cell.h = CellH;

		// AddRect для всех (нужна полная высота), рисуем только видимые
		if(!s_ProfileSkinScroll.AddRect(Cell))
			continue;

		pEntry->RequestLoad();
		const bool Active = str_comp(Profile.m_aSkin, pName) == 0;
		Cell.Draw(Active ? ColorRGBA(0.20f, 0.45f, 0.85f, 0.75f) : ColorRGBA(0.12f, 0.12f, 0.12f, 0.75f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect Preview = Cell;
		Preview.HSplitTop(CellH - 18.0f, &Preview, nullptr);
		Preview.Margin(6.0f, &Preview);

		const std::unique_ptr<CSkin> &pSkinPtr = pC->Skin();
		if(pSkinPtr)
		{
			CTeeRenderInfo Info;
			Info.m_aSixup[g_Config.m_ClDummy].Reset();
			Info.Apply(pSkinPtr.get());
			Info.ApplyColors(Profile.m_UseCustomColor, Profile.m_ColorBody, Profile.m_ColorFeet);
			Info.m_Size = Preview.h;
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Preview.Center());
		}
		else
		{
			Ui()->DoLabel(&Preview, "...", 20.0f, TEXTALIGN_MC);
		}

		CUIRect NameRect = Cell;
		NameRect.HSplitBottom(18.0f, nullptr, &NameRect);
		SLabelProperties Props;
		Props.m_MaxWidth = NameRect.w;
		Ui()->DoLabel(&NameRect, pName, 10.0f, TEXTALIGN_MC, Props);

		if(Ui()->MouseInside(&Cell) && Ui()->MouseButtonClicked(0))
		{
			str_copy(Profile.m_aSkin, pName);
			Changed = true;
		}
	}

	if(vFiltered.empty())
		Ui()->DoLabel(&View, Localize("No matching skins"), 12.0f, TEXTALIGN_MC);

	s_ProfileSkinScroll.End();
	return Changed;
}

void CMenus::RenderSettingsProfiles(CUIRect MainView)
{
	if(!m_ProfilesLoaded)
	{
		ProfilesLoad();
		m_ProfilesLoaded = true;
	}
	if(m_ProfilesDirty && time_get() - m_ProfilesLastChange > time_freq() / 2)
	{
		ProfilesSave();
		m_ProfilesDirty = false;
	}
	auto MarkDirty = [&]() {
		m_ProfilesDirty = true;
		m_ProfilesLastChange = time_get();
	};

	if(m_ProfileSelected >= (int)m_vProfiles.size())
		m_ProfileSelected = (int)m_vProfiles.size() - 1;
	const bool HasSelection = m_ProfileSelected >= 0;

	const float ColumnGap = 15.0f;
	CUIRect ListCol, EditCol, SkinCol, Rest;
	MainView.VSplitLeft(MainView.w * 0.25f, &ListCol, &Rest);
	Rest.VSplitLeft(ColumnGap, nullptr, &Rest);
	const float EditWidth = Rest.w * 0.58f;
	Rest.VSplitLeft(EditWidth, &EditCol, &Rest);
	Rest.VSplitLeft(ColumnGap, nullptr, &SkinCol);

	// ---------- Список профилей ----------
	{
		CUIRect Header, AddButton;
		ListCol.HSplitTop(24.0f, &Header, &ListCol);
		ListCol.HSplitTop(6.0f, nullptr, &ListCol);
		Header.VSplitRight(30.0f, &Header, &AddButton);
		Ui()->DoLabel(&Header, Localize("Profiles"), 18.0f, TEXTALIGN_ML);

		static CButtonContainer s_AddButton;
		if(DoButton_Menu(&s_AddButton, "+", 0, &AddButton) && (int)m_vProfiles.size() < MAX_PROFILES)
		{
			SProfile Profile{};
			str_format(Profile.m_aName, sizeof(Profile.m_aName), "Profile %d", (int)m_vProfiles.size() + 1);
			ProfileFromCurrent(Profile);
			m_vProfiles.push_back(Profile);
			m_ProfileSelected = (int)m_vProfiles.size() - 1;
			MarkDirty();
		}

		if(m_vProfiles.empty())
		{
			Ui()->DoLabel(&ListCol, Localize("Press + to create a profile"), 12.0f, TEXTALIGN_TC, {.m_MaxWidth = ListCol.w});
		}
		else
		{
			static CButtonContainer s_aItemButtons[MAX_PROFILES];
			CScrollRegionParams ListParams;
			ListParams.m_ScrollbarThickness = 12.0f;
			ListParams.m_ScrollbarMargin = 2.0f;
			ListParams.m_ScrollUnit = 40.0f;
			s_ProfileListScroll.Begin(&ListCol, &ListParams);
			for(int i = 0; i < (int)m_vProfiles.size(); i++)
			{
				CUIRect Item;
				ListCol.HSplitTop(30.0f, &Item, &ListCol);
				ListCol.HSplitTop(10.0f, nullptr, &ListCol);
				if(!s_ProfileListScroll.AddRect(Item))
					continue;
				if(DoButton_Menu(&s_aItemButtons[i], m_vProfiles[i].m_aName, i == m_ProfileSelected, &Item, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.3f))
					m_ProfileSelected = i;
			}
			s_ProfileListScroll.End();
		}
	}

	if(!HasSelection)
	{
		Ui()->DoLabel(&EditCol, Localize("Create or select a profile"), 14.0f, TEXTALIGN_MC);
		return;
	}

	SProfile &Profile = m_vProfiles[m_ProfileSelected];
	bool DoApply = false;
	bool DoDelete = false;
	bool DoFromCurrent = false;
	static int64_t s_AppliedTime = 0;

	// ---------- Редактор ----------
	{
		CScrollRegionParams EditParams;
		EditParams.m_ScrollbarThickness = 12.0f;
		EditParams.m_ScrollbarMargin = 2.0f;
		EditParams.m_ScrollUnit = 60.0f;
		s_ProfileEditScroll.Begin(&EditCol, &EditParams);
		const CUIRect EditArea = EditCol;
		const float StartY = EditCol.y;

		static CLineInput s_NameInput, s_NickInput, s_ClanInput, s_SkinInput;
		auto DoTextRow = [&](CLineInput &Input, const char *pLabel, char *pBuf, size_t Size) {
			CUIRect Row, Label, Edit;
			EditCol.HSplitTop(22.0f, &Row, &EditCol);
			EditCol.HSplitTop(5.0f, nullptr, &EditCol);
			Row.VSplitLeft(90.0f, &Label, &Edit);
			Ui()->DoLabel(&Label, pLabel, 14.0f, TEXTALIGN_ML);
			Input.SetBuffer(pBuf, Size);
			if(Ui()->DoClearableEditBox(&Input, &Edit, 13.0f))
				MarkDirty();
		};
		DoTextRow(s_NameInput, Localize("Profile"), Profile.m_aName, sizeof(Profile.m_aName));
		DoTextRow(s_NickInput, Localize("Nickname"), Profile.m_aPlayerName, sizeof(Profile.m_aPlayerName));
		DoTextRow(s_ClanInput, Localize("Clan"), Profile.m_aClan, sizeof(Profile.m_aClan));
		DoTextRow(s_SkinInput, Localize("Skin"), Profile.m_aSkin, sizeof(Profile.m_aSkin));

		// Предпросмотр
		{
			CUIRect Preview;
			EditCol.HSplitTop(64.0f, &Preview, &EditCol);
			EditCol.HSplitTop(5.0f, nullptr, &EditCol);
			const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
			const CSkins::CSkinContainer *pC = GameClient()->m_Skins.FindContainerOrNullptr(Profile.m_aSkin[0] == '\0' ? "default" : Profile.m_aSkin);
			CTeeRenderInfo Info;
			Info.Apply(pC == nullptr || pC->Skin() == nullptr ? pDefaultSkin : pC->Skin().get());
			Info.ApplyColors(Profile.m_UseCustomColor, Profile.m_ColorBody, Profile.m_ColorFeet);
			Info.m_Size = 50.0f;
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f),
				vec2(Preview.x + Preview.w / 2.0f, Preview.y + Preview.h / 2.0f + OffsetToMid.y));
		}

		// Цвета
		CUIRect CheckRow;
		EditCol.HSplitTop(20.0f, &CheckRow, &EditCol);
		EditCol.HSplitTop(5.0f, nullptr, &EditCol);
		if(DoButton_CheckBox(&Profile.m_UseCustomColor, Localize("Custom colors"), Profile.m_UseCustomColor, &CheckRow))
		{
			Profile.m_UseCustomColor ^= 1;
			MarkDirty();
		}

		if(Profile.m_UseCustomColor)
		{
			unsigned int *apColors[] = {&Profile.m_ColorBody, &Profile.m_ColorFeet};
			const char *apParts[] = {Localize("Body"), Localize("Feet")};
			for(int i = 0; i < 2; i++)
			{
				CUIRect Part, Label;
				EditCol.HSplitTop(95.0f, &Part, &EditCol);
				EditCol.HSplitTop(8.0f, nullptr, &EditCol);
				Part.HSplitTop(20.0f, &Label, &Part);
				Ui()->DoLabel(&Label, apParts[i], 14.0f, TEXTALIGN_ML);
				if(RenderHslaScrollbars(&Part, apColors[i], false, ColorHSLA::DARKEST_LGT))
					MarkDirty();
			}
		}

		// Кнопки
		{
			CUIRect Row1, Row2, ApplyBtn, CurrentBtn, DeleteBtn;
			EditCol.HSplitTop(30.0f, &Row1, &EditCol);
			EditCol.HSplitTop(10.0f, nullptr, &EditCol);
			EditCol.HSplitTop(30.0f, &Row2, &EditCol);
			EditCol.HSplitTop(10.0f, nullptr, &EditCol);
			Row1.VSplitMid(&ApplyBtn, &CurrentBtn, 10.0f);
			DeleteBtn = Row2;

			static CButtonContainer s_ApplyButton, s_CurrentButton, s_DeleteButton;
			if(DoButton_Menu(&s_ApplyButton, Localize("Apply"), 0, &ApplyBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.3f, ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f)))
				DoApply = true;
			if(DoButton_Menu(&s_CurrentButton, Localize("From current"), 0, &CurrentBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.3f))
				DoFromCurrent = true;
			if(DoButton_Menu(&s_DeleteButton, Localize("Delete"), 0, &DeleteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.3f, ColorRGBA(1.0f, 0.0f, 0.0f, 0.25f)))
				DoDelete = true;

			if(s_AppliedTime != 0 && time_get() - s_AppliedTime < time_freq() * 2)
			{
				CUIRect Info;
				EditCol.HSplitTop(18.0f, &Info, &EditCol);
				TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);
				Ui()->DoLabel(&Info, Localize("Profile applied"), 12.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}

		CUIRect Used = EditArea;
		Used.y = StartY;
		Used.h = EditCol.y - StartY + 10.0f;
		s_ProfileEditScroll.AddRect(Used);
		s_ProfileEditScroll.End();
	}

	// ---------- Выбор скина ----------
	{
		CUIRect Title;
		SkinCol.HSplitTop(24.0f, &Title, &SkinCol);
		SkinCol.HSplitTop(6.0f, nullptr, &SkinCol);
		Ui()->DoLabel(&Title, Localize("Choose skin"), 18.0f, TEXTALIGN_ML);
		if(DoProfileSkinPicker(SkinCol, Profile))
			MarkDirty();
	}

	// ---------- Действия (после всех ссылок на Profile) ----------
	if(DoFromCurrent)
	{
		ProfileFromCurrent(Profile);
		MarkDirty();
	}
	if(DoApply)
	{
		ProfileApply(Profile);
		s_AppliedTime = time_get();
	}
	if(DoDelete)
	{
		m_vProfiles.erase(m_vProfiles.begin() + m_ProfileSelected);
		if(m_ProfileSelected >= (int)m_vProfiles.size())
			m_ProfileSelected = (int)m_vProfiles.size() - 1;
		MarkDirty();
	}
}