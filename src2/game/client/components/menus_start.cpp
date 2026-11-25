/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/time.h>

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	enum
	{
		ANIM_DISCORD = 0,
		ANIM_LEARN,
		ANIM_TUTORIAL,
		ANIM_WEBSITE,
		ANIM_NEWS,
		ANIM_QUIT,
		ANIM_SETTINGS,
		ANIM_LOCAL_SERVER,
		ANIM_LOCAL_SERVER_STOP,
		ANIM_EDITOR,
		ANIM_DEMOS,
		ANIM_PLAY,
		ANIM_CONSOLE,
		ANIM_UPDATE,
		ANIM_BELL,
		NUM_ANIMATORS
	};
	static SUIAnimator s_aAnimators[NUM_ANIMATORS];
	static bool s_AnimatorsInit = false;
	if(!s_AnimatorsInit)
	{
		for(auto &Anim : s_aAnimators)
		{
			Anim.m_WOffset = 12.0f;   // +12px по ширине (по 6px с каждой стороны)
			Anim.m_HOffset = 6.0f;    // +6px по высоте (по 3px сверху/снизу)
		}
		s_AnimatorsInit = true;
	}

	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	// render logo (floating up and down)
	if(!GameClient()->m_Menus.m_HideStartMenuLogo)
	{
		const float AnimTime = (float)time() / (float)time_freq();
		const float BannerY = MainView.y + 60.0f + sinf(AnimTime * 1.5f) * 6.0f;
		Graphics()->TextureSet(GameClient()->m_Menus.m_HydraLogoTexture.IsValid() ? GameClient()->m_Menus.m_HydraLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		const CMenus &Menus = GameClient()->m_Menus;
		const float BannerH = 103.0f;
		const float BannerW = Menus.m_HydraLogoLoaded ? BannerH * Menus.m_HydraLogoAspect : 360.0f;
		IGraphics::CQuadItem QuadItem(MainView.w / 2 + 10.0f - BannerW / 2.0f, BannerY, BannerW, BannerH);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_DISCORD]))
	{
		Client()->ViewLink(Localize("https://ddnet.org/discord"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_LearnButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_LEARN]))
	{
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TutorialButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_TUTORIAL]))
	{
		GameClient()->m_Menus.JoinTutorial();
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_WebsiteButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_WEBSITE]))
	{
		Client()->ViewLink("https://ddnet.org/");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_NewsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_NEWS]) || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	Menu.HSplitBottom(25.0f, &Menu, nullptr);

	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_QuitButton;
	bool UsedEscape = false;
	if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_QUIT]) || CheckHotKey(KEY_Q))
	{
		if(GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			GameClient()->m_Menus.ShowQuitPopup();
		}
		else
		{
			Client()->Quit();
		}
	}

	Menu.HSplitBottom(100.0f, &Menu, nullptr);
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_SettingsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_SETTINGS]) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_LocalServerButton;

	const char *pLocalServerImage = g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(LocalServerRunning)
	{
		// Split in the middle of the label area, the image is rendered on the right
		CUIRect JoinButton, StopButton;
		const float ImageWidth = pLocalServerImage == nullptr ? 0.0f : Button.h * CMenus::BUTTON_IMAGE_WIDTH_FACTOR;
		Button.VSplitLeft((Button.w - ImageWidth) / 2.0f, &JoinButton, &StopButton);
		if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, Localize("Join"), 0, &JoinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, Rounding, 0.5f, ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_LOCAL_SERVER]) || CheckHotKey(KEY_J))
		{
			GameClient()->m_LocalServer.Connect();
		}
		static CButtonContainer s_StopServerButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_StopServerButton, Localize("Stop server"), 0, &StopButton, BUTTONFLAG_LEFT, pLocalServerImage, IGraphics::CORNER_R, Rounding, 0.5f, ColorRGBA(1.0f, 0.0f, 0.0f, 0.25f), &s_LocalServerButton, &s_aAnimators[ANIM_LOCAL_SERVER_STOP]) || CheckHotKey(KEY_R))
		{
			GameClient()->m_LocalServer.KillServer();
		}
	}
	else if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, Localize("Run server"), 0, &Button, BUTTONFLAG_LEFT, pLocalServerImage, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_LOCAL_SERVER]) || CheckHotKey(KEY_R))
	{
		GameClient()->m_LocalServer.RunServer({});
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_MapEditorButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_EDITOR]) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_DemoButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_DEMOS]) || CheckHotKey(KEY_D))
	{
		NewPage = CMenus::PAGE_DEMOS;
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_PlayButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_PLAY]) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), nullptr, &s_aAnimators[ANIM_CONSOLE]))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}

	// Колокольчик уведомлений (слева от кнопки консоли)
	CUIRect BellButton = ConsoleButton;
	BellButton.x -= ConsoleButton.w + 15.0f;
	static CButtonContainer s_BellButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_BellButton, FontIcon::NEWSPAPER, 0, &BellButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), nullptr, &s_aAnimators[ANIM_BELL]))
	{
		GameClient()->m_Menus.OpenNotifications();
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	// Красный бейдж с числом непрочитанных
	const int UnreadCount = GameClient()->m_EventsManager.UnreadCount();
	if(UnreadCount > 0)
	{
		CUIRect Badge;
		Badge.x = BellButton.x + BellButton.w - 12.0f;
		Badge.y = BellButton.y - 5.0f;
		Badge.w = 16.0f;
		Badge.h = 16.0f;
		Badge.Draw(ColorRGBA(1.0f, 0.1f, 0.1f, 1.0f), IGraphics::CORNER_ALL, 8.0f);

		char aCount[8];
		str_format(aCount, sizeof(aCount), "%d", UnreadCount > 9 ? 9 : UnreadCount);
		Ui()->DoLabel(&Badge, aCount, 10.0f, TEXTALIGN_MC);
	}

	CUIRect VersionUpdate;
	MainView.HSplitBottom(20.0f, nullptr, &VersionUpdate);
	VersionUpdate.VMargin(VMargin, &VersionUpdate);
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateButton;
	VersionUpdate.VSplitRight(100.0f, &VersionUpdate, &UpdateButton);
	VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

	char aBuf[128];
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	const bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");

	if(State == IUpdater::CLEAN && NeedUpdate)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Update now"), 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_UPDATE]))
		{
			Updater()->InitiateUpdate();
		}
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Restart"), 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_UPDATE]))
		{
			Client()->Restart();
		}
	}
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
	}

	if(State == IUpdater::CLEAN && NeedUpdate)
	{
		str_format(aBuf, sizeof(aBuf), Localize("HydraClient %s is out!"), Client()->LatestVersion());
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	else if(State == IUpdater::CLEAN)
	{
		aBuf[0] = '\0';
	}
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		char aCurrentFile[64];
		Updater()->GetCurrentFile(aCurrentFile, sizeof(aCurrentFile));
		str_format(aBuf, sizeof(aBuf), Localize("Downloading %s:"), aCurrentFile);
	}
	else if(State == IUpdater::FAIL)
	{
		str_copy(aBuf, Localize("Update failed! Check log…"));
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		str_copy(aBuf, Localize("DDNet Client updated!"));
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
#elif defined(CONF_INFORM_UPDATE)
	if(str_comp(Client()->LatestVersion(), "0") != 0)
	{
		CUIRect DownloadButton;
		VersionUpdate.VSplitRight(100.0f, &VersionUpdate, &DownloadButton);
		VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

		static CButtonContainer s_DownloadButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DownloadButton, Localize("Download"), 0, &DownloadButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), nullptr, &s_aAnimators[ANIM_UPDATE]))
		{
			Client()->ViewLink("https://github.com/mrazigde-cloud/HydraClientForDDnet/releases");
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("HydraClient %s is out!"), Client()->LatestVersion());
		SLabelProperties UpdateLabelProps;
		UpdateLabelProps.SetColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
		Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML, UpdateLabelProps);
	}
#endif

	if(NewPage != -1)
	{
		if(Client()->State() == IClient::STATE_ONLINE)
			GameClient()->m_Menus.SetGamePage(NewPage);
		else
			GameClient()->m_Menus.SetMenuPage(NewPage);
		GameClient()->m_Menus.SetShowStart(false);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
