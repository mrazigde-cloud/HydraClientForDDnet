#include "events_manager.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>

#include <engine/font_icons.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

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

void CEventsManager::OnInit()
{
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
		m_pEventsTask = nullptr;
	}

	// 4. Отрисовка всплывающего уведомления поверх игры
	RenderToast();
}

void CEventsManager::ParseFirebaseResponse(json_value *pRoot)
{
	if(pRoot->type != json_object)
		return;

	const json_value &Notifs = (*pRoot)["notifications"];
	if(Notifs.type == json_array)
	{
		m_vNotifications.clear();
		for(unsigned int i = 0; i < Notifs.u.array.length; i++)
		{
			const json_value &Item = Notifs[i];
			if(Item.type != json_object)
				continue;

			const json_value &IdVal = Item["id"];
			const json_value &TitleVal = Item["title"];
			const json_value &TextVal = Item["text"];
			const json_value &TypeVal = Item["type"];

			if(IdVal.type != json_integer || TitleVal.type != json_string || TextVal.type != json_string)
				continue;

			CEventNotification Entry;
			Entry.m_Id = (int)IdVal.u.integer;
			str_copy(Entry.m_aTitle, TitleVal.u.string.ptr);
			str_copy(Entry.m_aText, TextVal.u.string.ptr);

			Entry.m_Type = CEventNotification::TYPE_NEWS;
			if(TypeVal.type == json_string)
			{
				if(str_comp(TypeVal.u.string.ptr, "update") == 0)
					Entry.m_Type = CEventNotification::TYPE_UPDATE;
				else if(str_comp(TypeVal.u.string.ptr, "event") == 0)
					Entry.m_Type = CEventNotification::TYPE_EVENT;
			}
			Entry.m_Read = IsIdRead(Entry.m_Id);

			m_vNotifications.push_back(Entry);

			if(!Entry.m_Read && std::find(m_vToastedIds.begin(), m_vToastedIds.end(), Entry.m_Id) == m_vToastedIds.end())
			{
				MarkToasted(Entry.m_Id);
				ShowToast(Entry);
			}
		}
		m_NotificationsLoaded = true;
	}
}

void CEventsManager::ShowToast(const CEventNotification &Notification)
{
	SToast Toast;
	Toast.m_Id = Notification.m_Id;
	Toast.m_Type = Notification.m_Type;
	str_copy(Toast.m_aTitle, Notification.m_aTitle);
	str_truncate(Toast.m_aText, sizeof(Toast.m_aText), Notification.m_aText, 70);
	m_vToasts.push_back(Toast);
}

bool CEventsManager::IsIdRead(int Id) const
{
	return std::find(m_vReadIds.begin(), m_vReadIds.end(), Id) != m_vReadIds.end();
}

void CEventsManager::LoadReadState()
{
	m_vReadIds.clear();
	const char *pStr = g_Config.m_ClReadNotifications;
	char aToken[16];
	while((pStr = str_next_token(pStr, ",", aToken, sizeof(aToken))))
	{
		const int Id = str_toint(aToken);
		if(Id > 0)
			m_vReadIds.push_back(Id);
	}

	m_vToastedIds.clear();
	const char *pToasted = g_Config.m_ClToastedNotifications;
	while((pToasted = str_next_token(pToasted, ",", aToken, sizeof(aToken))))
	{
		const int Id = str_toint(aToken);
		if(Id > 0)
			m_vToastedIds.push_back(Id);
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

void CEventsManager::MarkRead(int Id)
{
	for(CEventNotification &Notification : m_vNotifications)
	{
		if(Notification.m_Id == Id)
			Notification.m_Read = true;
	}
	if(!IsIdRead(Id))
	{
		m_vReadIds.push_back(Id);
		SaveReadState();
	}
}

void CEventsManager::MarkAllRead()
{
	bool Changed = false;
	for(CEventNotification &Notification : m_vNotifications)
	{
		Notification.m_Read = true;
		if(!IsIdRead(Notification.m_Id))
		{
			m_vReadIds.push_back(Notification.m_Id);
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
	constexpr float Hold = 6.0f;
	constexpr float FadeOut = 0.4f;
	constexpr float Total = FadeIn + Hold + FadeOut;

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
		}
	}

	const float ScreenHeight = 400.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, ScreenWidth, ScreenHeight));

	const float BoxW = 175.0f;
	const float BoxH = 42.0f;
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
		TextRender()->Text(TextX, Y + 5.0f, 7.5f, Toast.m_aTitle, TextW);

		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * Alpha));
		TextRender()->Text(TextX, Y + 16.0f, 6.0f, Toast.m_aText, TextW);
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
}

void CEventsManager::ConTestScore(IConsole::IResult *pResult, void *pUserData)
{
	CEventsManager *pThis = (CEventsManager *)pUserData;
	int Score = pResult->GetInteger(0);
	pThis->SendScore("TestPlayer", Score);
}