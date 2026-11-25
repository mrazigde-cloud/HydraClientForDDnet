
#ifndef GAME_CLIENT_COMPONENTS_EVENTS_MANAGER_H
#define GAME_CLIENT_COMPONENTS_EVENTS_MANAGER_H

#include <engine/console.h>
#include <engine/http.h>
#include <engine/sound.h>
#include <engine/shared/json.h>
#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

struct CEventNotification
{
	enum EType
	{
		TYPE_NEWS = 0,
		TYPE_UPDATE,
		TYPE_EVENT,
	};

	int m_Id = 0;
	int m_Key = 0; // id + хеш содержимого, по нему определяется прочитано/показано
	int m_Type = TYPE_NEWS;
	bool m_Read = false;
	char m_aTitle[256] = {0};
	char m_aText[512] = {0};
	char m_aLink[512] = {0};
};

class CEventsManager : public CComponent
{
	private:
	std::string m_FirebaseUrl = "https://ddnet-event-default-rtdb.europe-west1.firebasedatabase.app";
	std::shared_ptr<IHttpRequest> m_pConfigTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pEventsTask = nullptr;
	int64_t m_LastCheckTime = 0;
	
	struct SToast
	{
		int m_Id = 0;
		int m_Type = CEventNotification::TYPE_NEWS;
		char m_aTitle[256] = {0};
		char m_aText[128] = {0};
		bool m_Started = false;
		int64_t m_StartTime = 0;
		bool m_YInit = false;
		float m_CurY = 0.0f;
	};
	static constexpr int MAX_VISIBLE_TOASTS = 4;
	std::vector<SToast> m_vToasts;
	std::vector<int> m_vToastedIds;
	int64_t m_LastRenderTime = 0;
	int m_NotifySound = -1;

	std::vector<int> m_vReadIds;
	bool m_NotificationsLoaded = false;
	
	void FetchGitHubConfig();
	void FetchFirebaseData();
	void ParseFirebaseResponse(json_value *pRoot);
	void ShowToast(const CEventNotification &Notification);
	void RenderToast();
	void MarkToasted(int Id);
	void LoadReadState();
	void SaveReadState();
	bool IsIdRead(int Id) const;
	static void ConTestScore(IConsole::IResult *pResult, void *pUserData);
	static void ConResetNotifications(IConsole::IResult *pResult, void *pUserData);
	void ResetNotifications();
	
	public:
	std::vector<CEventNotification> m_vNotifications;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnConsoleInit() override;
	
	void SendScore(const char *pPlayerName, int NewScore);

	// API уведомлений (для меню)
	int UnreadCount() const;
	bool HasLoaded() const { return m_NotificationsLoaded; }
	void MarkRead(int Id);
	void MarkAllRead();
};

#endif