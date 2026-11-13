/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/client/ui_scrollregion.h>

#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>
#include <base/net.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/client/ui_listbox.h>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::RenderSettings(CUIRect MainView)
{
	// render background
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
		"Hydra Client",
		Localize("Credits")};
	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		TabBar.HSplitTop(10.0f, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			g_Config.m_UiSettingsPage = i;
	}

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
		RenderLanguageSettings(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsGeneral(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
		RenderSettingsPlayer(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(MainView);
		else
			RenderSettingsTee(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderSettingsAppearance(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderSettingsGraphics(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderSettingsSound(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsDDNet(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsAssets(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TESTCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsTestClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CREDITS);
		RenderSettingsCredits(MainView);
	}
	else
	{
		dbg_assert_failed("ui_settings_page invalid");
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
void CMenus::RenderSettingsTestClient(CUIRect MainView)
{
	CUIRect Label, Button, Left, Right, Middle;
	// ***** Sub tabs ***** //
	enum
	{
		TESTCLIENT_TAB_GENERAL = 0,
		TESTCLIENT_TAB_NAME_GRADIENT = 1,
		TESTCLIENT_TAB_TRAIL = 2,
		NUMBER_OF_TESTCLIENT_TABS = 3,
	};
	static int s_CurTab = TESTCLIENT_TAB_GENERAL;
	{
		CUIRect TabBar;
		MainView.HSplitTop(20.0f, &TabBar, &MainView);
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		const float TabWidth = TabBar.w / (float)NUMBER_OF_TESTCLIENT_TABS;
		static CButtonContainer s_aTabs[NUMBER_OF_TESTCLIENT_TABS] = {};
		const char *apTabNames[NUMBER_OF_TESTCLIENT_TABS] = {
			Localize("General"),
			Localize("Name Gradient"),
			Localize("Trail")};
		for(int Tab = 0; Tab < NUMBER_OF_TESTCLIENT_TABS; ++Tab)
		{
			TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
			const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == NUMBER_OF_TESTCLIENT_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_CurTab = Tab;
		}
	}

	// ***** Name Gradient tab ***** //
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

		auto DoGroup = [&](int Index, CUIRect View, const char *pTitle, int *pEnable, int *pSpeed, int *pMode, int *pWave,
					unsigned int *pColor1, unsigned int *pColor2, unsigned int *pColor3,
					ColorRGBA Default1, ColorRGBA Default2, ColorRGBA Default3) {
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
		};

		CUIRect OwnView, FriendView, OtherView, Rest;
		const float ColumnGap = 15.0f;
		const float ColumnWidth = (MainView.w - 2.0f * ColumnGap) / 3.0f;
		MainView.VSplitLeft(ColumnWidth, &OwnView, &Rest);
		Rest.VSplitLeft(ColumnGap, nullptr, &Rest);
		Rest.VSplitLeft(ColumnWidth, &FriendView, &Rest);
		Rest.VSplitLeft(ColumnGap, nullptr, &OtherView);

		DoGroup(0, OwnView, Localize("Me (main, dummy, extra)"),
			&g_Config.m_ClNameGradientOwn, &g_Config.m_ClNameGradientOwnSpeed, &g_Config.m_ClNameGradientOwnMode, &g_Config.m_ClNameGradientOwnWave,
			&g_Config.m_ClNameGradientOwnColor1, &g_Config.m_ClNameGradientOwnColor2, &g_Config.m_ClNameGradientOwnColor3,
			ColorRGBA(1.0f, 0.0f, 0.0f), ColorRGBA(0.0f, 1.0f, 0.0f), ColorRGBA(0.0f, 0.0f, 1.0f));
		DoGroup(1, FriendView, Localize("Friends"),
			&g_Config.m_ClNameGradientFriend, &g_Config.m_ClNameGradientFriendSpeed, &g_Config.m_ClNameGradientFriendMode, &g_Config.m_ClNameGradientFriendWave,
			&g_Config.m_ClNameGradientFriendColor1, &g_Config.m_ClNameGradientFriendColor2, &g_Config.m_ClNameGradientFriendColor3,
			ColorRGBA(1.0f, 0.4f, 0.7f), ColorRGBA(1.0f, 0.6f, 0.2f), ColorRGBA(1.0f, 0.9f, 0.2f));
		DoGroup(2, OtherView, Localize("Others"),
			&g_Config.m_ClNameGradientOther, &g_Config.m_ClNameGradientOtherSpeed, &g_Config.m_ClNameGradientOtherMode, &g_Config.m_ClNameGradientOtherWave,
			&g_Config.m_ClNameGradientOtherColor1, &g_Config.m_ClNameGradientOtherColor2, &g_Config.m_ClNameGradientOtherColor3,
			ColorRGBA(0.0f, 1.0f, 1.0f), ColorRGBA(0.8f, 0.0f, 1.0f), ColorRGBA(0.0f, 0.0f, 1.0f));

		return;
	}
	// ***** Trail tab ***** //
	if(s_CurTab == TESTCLIENT_TAB_TRAIL)
	{
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

		CUIRect Left, Right, Button, Row;

		MainView.VSplitMid(&Left, &Right, 20.0f);

		// ---- Global settings (left) ----
		Ui()->DoLabel_AutoLineSize(Localize("Trail"), HeadlineFontSize, TEXTALIGN_ML, &Left, HeadlineHeight);
		Left.HSplitTop(MarginSmall, nullptr, &Left);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTrail, Localize("Enable trails"), &g_Config.m_ClTrail, &Left, LineSize);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClTrailSize, &g_Config.m_ClTrailSize, &Button, Localize("Segment size"), 1, 50);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClTrailLifetime, &g_Config.m_ClTrailLifetime, &Button, Localize("Lifetime (ms)"), 100, 5000);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClTrailFadeTime, &g_Config.m_ClTrailFadeTime, &Button, Localize("Fade time (ms)"), 0, 5000);

		Left.HSplitTop(LineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_ClTrailSpacing, &g_Config.m_ClTrailSpacing, &Button, Localize("Spacing"), 1, 100);

		// ---- Per-group colors (right) ----
		Ui()->DoLabel_AutoLineSize(Localize("Colors per group"), HeadlineFontSize, TEXTALIGN_ML, &Right, HeadlineHeight);
		Right.HSplitTop(MarginSmall, nullptr, &Right);

		struct SGroup
		{
			const char *m_pTitle;
			int *m_pEnable;
			int *m_pColorMode;
			unsigned int *m_pColor;
		};
		const SGroup aGroups[3] = {
			{Localize("Own"), &g_Config.m_ClTrailOwn, &g_Config.m_ClTrailOwnColorMode, &g_Config.m_ClTrailOwnColor},
			{Localize("Friends"), &g_Config.m_ClTrailFriend, &g_Config.m_ClTrailFriendColorMode, &g_Config.m_ClTrailFriendColor},
			{Localize("Others"), &g_Config.m_ClTrailOther, &g_Config.m_ClTrailOtherColorMode, &g_Config.m_ClTrailOtherColor},
		};

		for(int Group = 0; Group < 3; ++Group)
		{
			Right.HSplitTop(MarginSmall, nullptr, &Right);
			DoButton_CheckBoxAutoVMarginAndSet(&s_aTrailEnableCheckbox[Group], aGroups[Group].m_pTitle, aGroups[Group].m_pEnable, &Right, LineSize);

			if(!*aGroups[Group].m_pEnable)
				continue;

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
		}

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