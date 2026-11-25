#include "events_manager.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>

#include <engine/font_icons.h>
#include <engine/sound.h>
#include <game/client/components/sounds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

static bool IsBlankText(const char *pText)
{
	for(; *pText; pText++)
	{
		if(*pText != ' ' && *pText != '\t' && *pText != '\n' && *pText != '\r')
			return false;
	}
	return true;
}

static void UrlEncodeSimple(const char *pSrc, char *pDst, size_t DstSize)
{
	size_t j = 0;
	for(size_t i = 0; pSrc[i] != '\0' && j + 4 < DstSize; i++)
	{
		unsigned char c = (unsigned char)pSrc[i];
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
		{
			pDst[j++] = c;
		}
		else
		{
			snprintf(pDst + j, 4, "%%%02X", c);
			j += 3;
		}
	}
	pDst[j] = '\0';
}

static int NotificationKey(int Id, const char *pTitle, const char *pText)
{
	char aBuf[1024];
	str_format(aBuf, sizeof(aBuf), "%d|%s|%s", Id, pTitle, pText);

	// FNV-1a
	unsigned int Hash = 2166136261u;
	for(const char *p = aBuf; *p; p++)
	{
		Hash ^= (unsigned char)*p;
		Hash *= 16777619u;
	}
	// Ключи всегда >= 1000000, чтобы старые записи (обычные id 1, 2, 3...) отличались от них
	return 1000000 + (int)(Hash % 2000000000u);
}

void CEventsManager::OnInit()
{
	m_NotifySound = Sound()->LoadOpus("audio/notification.opus");
	LoadReadState();
	FetchGitHubConfig();
	FetchFirebaseData();
	m_LastCheckTime = time_get();
}

void CEventsManager::FetchGitHubConfig()
{
	m_pConfigTask = HttpGet("https://github.io");
	m_pConfigTask->Timeout(CTimeout{5000, 0, 500, 5});
	Http()->Run(m_pConfigTask);
}

void CEventsManager::FetchFirebaseData()
{
	if(m_pEventsTask)
		return;

	std::string FullUrl = m_FirebaseUrl + "/.json";
	m_pEventsTask = HttpGet(FullUrl.c_str());
	m_pEventsTask->Timeout(CTimeout{10000, 0, 500, 10});
	Http()->Run(m_pEventsTask);
}

void CEventsManager::OnRender()
{
	// 1. Обработка ответа от GitHub (Remote Config)
	if(m_pConfigTask && m_pConfigTask->State() == EHttpState::DONE)
	{
		json_value *pJson = m_pConfigTask->ResultJson();
		if(pJson && pJson->type == json_object)
		{
			const json_value &Url = (*pJson)["firebase_url"];
			if(Url.type == json_string && Url.u.string.ptr[0] != '\0')
			{
				m_FirebaseUrl = Url.u.string.ptr;
			}
		}
		json_value_free(pJson);
		m_pConfigTask = nullptr;
	}

	// 2. Периодический опрос Firebase каждые 15 секунд
	if(time_get() - m_LastCheckTime > time_freq() * 15)
	{
		m_LastCheckTime = time_get();
		FetchFirebaseData();
	}

	// 3. Обработка ответа от Firebase
	if(m_pEventsTask && m_pEventsTask->State() == EHttpState::DONE)
	{
		json_value *pRoot = m_pEventsTask->ResultJson();
		if(pRoot)
		{
			ParseFirebaseResponse(pRoot);
			json_value_free(pRoot);
		}
		m_pEventsTask = nullptr;
	}
	else if(m_pEventsTask && (m_pEventsTask->State() == EHttpState::ERROR || m_pEventsTask->State() == EHttpState::ABORTED))
	{
		// Иначе FetchFirebaseData() больше никогда не запустится
		log_error("events", "Firebase request failed");
		m_pEventsTask = nullptr;
	}

	// 4. Отрисовка всплывающего уведомления поверх игры
	RenderToast();
}

void CEventsManager::ParseFirebaseResponse(json_value *pRoot)
{
	if(pRoot->type != json_object)
	{
		log_error("events", "Firebase response is not a JSON object");
		return;
	}

	// Firebase отдаёт массив, а если ключи не подряд с 0, то объект
	const json_value &Notifs = (*pRoot)["notifications"];
	std::vector<const json_value *> vItems;
	if(Notifs.type == json_array)
	{
		for(unsigned int i = 0; i < Notifs.u.array.length; i++)
			vItems.push_back(Notifs.u.array.values[i]);
	}
	else if(Notifs.type == json_object)
	{
		for(unsigned int i = 0; i < Notifs.u.object.length; i++)
			vItems.push_back(Notifs.u.object.values[i].value);
	}
	else
	{
		log_error("events", "'notifications' not found in Firebase response");
		return;
	}

	m_vNotifications.clear();
	for(const json_value *pItem : vItems)
	{
		const json_value &Item = *pItem;
		if(Item.type != json_object)
			continue;

		const json_value &IdVal = Item["id"];
		const json_value &TitleVal = Item["title"];
		const json_value &TextVal = Item["text"];
		const json_value &TypeVal = Item["type"];

		if(IdVal.type != json_integer || TitleVal.type != json_string || TextVal.type != json_string)
		{
			log_error("events", "skipped notification with invalid fields (id must be a number)");
			continue;
		}

		CEventNotification Entry;
		Entry.m_Id = (int)IdVal.u.integer;
		str_copy(Entry.m_aTitle, TitleVal.u.string.ptr);
		str_copy(Entry.m_aText, TextVal.u.string.ptr);

		const json_value &LinkVal = Item["link"];
		if(LinkVal.type == json_string)
			str_copy(Entry.m_aLink, LinkVal.u.string.ptr);
		else
			Entry.m_aLink[0] = '\0';

		Entry.m_Type = CEventNotification::TYPE_NEWS;
		if(TypeVal.type == json_string)
		{
			if(str_comp(TypeVal.u.string.ptr, "update") == 0)
				Entry.m_Type = CEventNotification::TYPE_UPDATE;
			else if(str_comp(TypeVal.u.string.ptr, "event") == 0)
				Entry.m_Type = CEventNotification::TYPE_EVENT;
		}
		Entry.m_Key = NotificationKey(Entry.m_Id, Entry.m_aTitle, Entry.m_aText);
		Entry.m_Read = IsIdRead(Entry.m_Key);

		m_vNotifications.push_back(Entry);

		if(!Entry.m_Read && std::find(m_vToastedIds.begin(), m_vToastedIds.end(), Entry.m_Key) == m_vToastedIds.end())
		{
			MarkToasted(Entry.m_Key);
			ShowToast(Entry);
		}
	}
	m_NotificationsLoaded = true;
	log_info("events", "loaded %d notifications, %d unread", (int)m_vNotifications.size(), UnreadCount());
}

void CEventsManager::ShowToast(const CEventNotification &Notification)
{
	SToast Toast;
	Toast.m_Id = Notification.m_Id;
	Toast.m_Type = Notification.m_Type;
	str_copy(Toast.m_aTitle, Notification.m_aTitle);
	str_copy(Toast.m_aText, Notification.m_aText);
	m_vToasts.push_back(Toast);
}

bool CEventsManager::IsIdRead(int Id) const
{
	return std::find(m_vReadIds.begin(), m_vReadIds.end(), Id) != m_vReadIds.end();
}

void CEventsManager::LoadReadState()
{
	// Значения меньше 1000000 остались от старой схемы (по id) и игнорируются
	constexpr int MIN_KEY = 1000000;

	m_vReadIds.clear();
	const char *pStr = g_Config.m_ClReadNotifications;
	char aToken[16];
	while((pStr = str_next_token(pStr, ",", aToken, sizeof(aToken))))
	{
		const int Key = str_toint(aToken);
		if(Key >= MIN_KEY)
			m_vReadIds.push_back(Key);
	}

	m_vToastedIds.clear();
	const char *pToasted = g_Config.m_ClToastedNotifications;
	while((pToasted = str_next_token(pToasted, ",", aToken, sizeof(aToken))))
	{
		const int Key = str_toint(aToken);
		if(Key >= MIN_KEY)
			m_vToastedIds.push_back(Key);
	}
}

void CEventsManager::SaveReadState()
{
	// Большие (новые) ID первыми: при переполнении строки отбрасываются самые старые
	std::sort(m_vReadIds.begin(), m_vReadIds.end(), std::greater<int>());
	m_vReadIds.erase(std::unique(m_vReadIds.begin(), m_vReadIds.end()), m_vReadIds.end());

	char aBuf[sizeof(g_Config.m_ClReadNotifications)];
	aBuf[0] = '\0';
	for(int Id : m_vReadIds)
	{
		char aId[16];
		if(aBuf[0] == '\0')
			str_format(aId, sizeof(aId), "%d", Id);
		else
			str_format(aId, sizeof(aId), ",%d", Id);

		if(str_length(aBuf) + str_length(aId) >= (int)sizeof(aBuf))
			break;
		str_append(aBuf, aId);
	}
	str_copy(g_Config.m_ClReadNotifications, aBuf);
}

int CEventsManager::UnreadCount() const
{
	int Count = 0;
	for(const CEventNotification &Notification : m_vNotifications)
	{
		if(!Notification.m_Read)
			Count++;
	}
	return Count;
}

void CEventsManager::MarkRead(int Key)
{
	for(CEventNotification &Notification : m_vNotifications)
	{
		if(Notification.m_Key == Key)
			Notification.m_Read = true;
	}
	if(!IsIdRead(Key))
	{
		m_vReadIds.push_back(Key);
		SaveReadState();
	}
}

void CEventsManager::MarkAllRead()
{
	bool Changed = false;
	for(CEventNotification &Notification : m_vNotifications)
	{
		Notification.m_Read = true;
		if(!IsIdRead(Notification.m_Key))
		{
			m_vReadIds.push_back(Notification.m_Key);
			Changed = true;
		}
	}
	if(Changed)
		SaveReadState();
}

void CEventsManager::MarkToasted(int Id)
{
	m_vToastedIds.push_back(Id);

	// Новые (большие) ID первыми: при переполнении строки отбрасываются самые старые
	std::sort(m_vToastedIds.begin(), m_vToastedIds.end(), std::greater<int>());
	m_vToastedIds.erase(std::unique(m_vToastedIds.begin(), m_vToastedIds.end()), m_vToastedIds.end());

	char aBuf[sizeof(g_Config.m_ClToastedNotifications)];
	aBuf[0] = '\0';
	for(int ToastedId : m_vToastedIds)
	{
		char aId[16];
		if(aBuf[0] == '\0')
			str_format(aId, sizeof(aId), "%d", ToastedId);
		else
			str_format(aId, sizeof(aId), ",%d", ToastedId);

		if(str_length(aBuf) + str_length(aId) >= (int)sizeof(aBuf))
			break;
		str_append(aBuf, aId);
	}
	str_copy(g_Config.m_ClToastedNotifications, aBuf);
}

void CEventsManager::RenderToast()
{
	const int64_t Now = time_get();
	const float Freq = (float)time_freq();
	float DeltaTime = m_LastRenderTime == 0 ? 0.0f : (float)(Now - m_LastRenderTime) / Freq;
	m_LastRenderTime = Now;
	DeltaTime = std::clamp(DeltaTime, 0.0f, 0.1f);

	if(m_vToasts.empty())
		return;

	constexpr float FadeIn = 0.35f;
	const float Hold = (float)g_Config.m_ClNotificationDuration;
	constexpr float FadeOut = 0.4f;
	const float Total = FadeIn + Hold + FadeOut;

	// Убираем отработавшие карточки
	for(size_t i = 0; i < m_vToasts.size();)
	{
		const SToast &Toast = m_vToasts[i];
		if(Toast.m_Started && (float)(Now - Toast.m_StartTime) / Freq >= Total)
			m_vToasts.erase(m_vToasts.begin() + i);
		else
			i++;
	}

	// Активируем карточки, которым хватило места (остальные ждут в очереди)
	const int NumVisible = std::min((int)m_vToasts.size(), (int)MAX_VISIBLE_TOASTS);
	for(int i = 0; i < NumVisible; i++)
	{
		if(!m_vToasts[i].m_Started)
		{
			m_vToasts[i].m_Started = true;
			m_vToasts[i].m_StartTime = Now;
			if(g_Config.m_SndEnable && m_NotifySound >= 0)
				Sound()->Play(CSounds::CHN_GUI, m_NotifySound, 0, 1.0f);
		}
	}

	const float ScreenHeight = 400.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, ScreenWidth, ScreenHeight));

	const float BoxW = 175.0f;
	const float BoxH = 80.0f;
	const float Spacing = 6.0f;
	const float X = ScreenWidth - BoxW - 10.0f;

	for(int i = 0; i < NumVisible; i++)
	{
		SToast &Toast = m_vToasts[i];

		// Плавное перемещение вверх, когда карточка выше исчезла (не зависит от FPS)
		const float TargetY = 15.0f + i * (BoxH + Spacing);
		if(!Toast.m_YInit)
		{
			Toast.m_CurY = TargetY;
			Toast.m_YInit = true;
		}
		else
		{
			Toast.m_CurY += (TargetY - Toast.m_CurY) * (1.0f - expf(-12.0f * DeltaTime));
		}

		// Прозрачность и сдвиг сверху вниз
		const float T = (float)(Now - Toast.m_StartTime) / Freq;
		float Alpha = 1.0f;
		float SlideY = 0.0f;
		if(T < FadeIn)
		{
			const float P = T / FadeIn;
			const float Eased = 1.0f - powf(1.0f - P, 3.0f);
			Alpha = Eased;
			SlideY = -(1.0f - Eased) * 24.0f;
		}
		else if(T > Total - FadeOut)
		{
			const float P = std::clamp((T - (Total - FadeOut)) / FadeOut, 0.0f, 1.0f);
			Alpha = 1.0f - P * P * (3.0f - 2.0f * P);
		}

		// Цвет и иконка по типу
		ColorRGBA Accent(0.55f, 0.85f, 0.45f, 1.0f);
		const char *pIcon = FontIcon::NEWSPAPER;
		if(Toast.m_Type == CEventNotification::TYPE_UPDATE)
		{
			Accent = ColorRGBA(0.30f, 0.75f, 1.0f, 1.0f);
			pIcon = FontIcon::GEAR;
		}
		else if(Toast.m_Type == CEventNotification::TYPE_EVENT)
		{
			Accent = ColorRGBA(1.0f, 0.65f, 0.2f, 1.0f);
			pIcon = FontIcon::STAR;
		}

		const float Y = Toast.m_CurY + SlideY;

		// Тонкая цветная обводка + тёмная полупрозрачная заливка
		Graphics()->DrawRect(X, Y, BoxW, BoxH, ColorRGBA(Accent.r, Accent.g, Accent.b, 0.9f * Alpha), IGraphics::CORNER_ALL, 5.0f);
		Graphics()->DrawRect(X + 1.0f, Y + 1.0f, BoxW - 2.0f, BoxH - 2.0f, ColorRGBA(0.08f, 0.08f, 0.12f, 0.88f * Alpha), IGraphics::CORNER_ALL, 4.0f);

		// Иконка типа
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(ColorRGBA(Accent.r, Accent.g, Accent.b, Alpha));
		TextRender()->Text(X + 8.0f, Y + 6.0f, 11.0f, pIcon, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		// Заголовок и текст
		const float TextX = X + 26.0f;
		const float TextW = BoxW - 26.0f - 8.0f;
		TextRender()->TextColor(ColorRGBA(Accent.r, Accent.g, Accent.b, Alpha));
		if(IsBlankText(Toast.m_aText))
		{
			// Только заголовок: по центру карточки
			const float TitleSize = 9.0f;
			const float TitleWidth = std::min(TextRender()->TextWidth(TitleSize, Toast.m_aTitle, -1, -1.0f), TextW);
			TextRender()->Text(TextX + (TextW - TitleWidth) / 2.0f, Y + BoxH / 2.0f - 5.5f, TitleSize, Toast.m_aTitle, TextW);
		}
		else
		{
			TextRender()->Text(TextX, Y + 5.0f, 7.5f, Toast.m_aTitle, TextW);

			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * Alpha));
			TextRender()->Text(TextX, Y + 25.0f, 6.0f, Toast.m_aText, TextW);
		}
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Ui()->MapScreen();
}


void CEventsManager::SendScore(const char *pPlayerName, int NewScore)
{
	char aEscapedName[512];
	UrlEncodeSimple(pPlayerName, aEscapedName, sizeof(aEscapedName));

	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "%s/active_event/scores/%s.json", m_FirebaseUrl.c_str(), aEscapedName);

	char aBody[32];
	str_format(aBody, sizeof(aBody), "%d", NewScore);

	std::shared_ptr<IHttpRequest> pTask = CreateHttpRequest(aUrl);
	pTask->Put((const unsigned char *)aBody, str_length(aBody));
	Http()->Run(pTask);
}
void CEventsManager::OnConsoleInit()
{
	Console()->Register("event_test_score", "i[score]", CFGFLAG_CLIENT, ConTestScore, this, "Send a test score to Firebase");
	Console()->Register("event_reset", "", CFGFLAG_CLIENT, ConResetNotifications, this, "Reset read/shown state of all notifications and show them again");
}

void CEventsManager::ConResetNotifications(IConsole::IResult *pResult, void *pUserData)
{
	((CEventsManager *)pUserData)->ResetNotifications();
}

void CEventsManager::ResetNotifications()
{
	m_vReadIds.clear();
	m_vToastedIds.clear();
	m_vToasts.clear();
	g_Config.m_ClReadNotifications[0] = '\0';
	g_Config.m_ClToastedNotifications[0] = '\0';

	// Показать карточки заново для всех загруженных уведомлений
	for(CEventNotification &Notification : m_vNotifications)
	{
		Notification.m_Read = false;
		MarkToasted(Notification.m_Key);
		ShowToast(Notification);
	}
	log_info("events", "notification state reset (%d notifications)", (int)m_vNotifications.size());
}

void CEventsManager::ConTestScore(IConsole::IResult *pResult, void *pUserData)
{
	CEventsManager *pThis = (CEventsManager *)pUserData;
	int Score = pResult->GetInteger(0);
	pThis->SendScore("TestPlayer", Score);
}