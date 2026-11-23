/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

#include <base/dbg.h>
#include <base/mem.h>
#include <base/time.h>
#include <base/vmath.h>
#include <base/math.h>
#include <cmath>
#include <engine/client.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>

#include <algorithm>

// Direction rotation: rotate the aiming vector that is sent to the server.
// The visual cursor (m_aMousePos / m_aTargetPos) is NOT rotated, so the
// crosshair stays under the physical mouse position, but the hook and
// bullets will fly along the rotated angle.
static bool ShouldRotateInputForConn(int Conn)
{
	if(!g_Config.m_ClDirRotation)
		return false;
	if(Conn == IClient::CONN_MAIN)
		return g_Config.m_ClDirRotationOwn != 0;
	if(Conn == IClient::CONN_DUMMY)
		return g_Config.m_ClDirRotationDummy != 0;
	return g_Config.m_ClDirRotationExtra != 0;
}

// Replace the aim direction of the input with an independent spin angle.
// The crosshair (MousePos) is ignored for the angle, but its length is
// preserved so the server-side physics sees a valid magnitude.
//
// While fire/hook is held:
//   - hammer and gun: instant snap to the crosshair (1-tick), then the
//     spin resumes on subsequent held ticks (pre-existing behaviour).
//   - any other weapon: smoothly rotate the aim toward the crosshair at a
//     constant angular speed, until fire/hook is released.
void CControls::ApplySpin(CNetObj_PlayerInput *pInput, const CNetObj_PlayerInput *pLastInput, int Slot, const vec2 &MousePos, float SpinAngle)
{
	if(!ShouldRotateInputForConn(Slot))
		return;

	const int Weapon = GameClient()->m_Snap.m_pLocalCharacter
		? GameClient()->m_Snap.m_pLocalCharacter->m_Weapon
		: WEAPON_HAMMER;

	const bool Jetpack =
		GameClient()->m_Snap.m_LocalClientId >= 0 &&
		GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Jetpack;

	const bool FireHeld = (pInput->m_Fire & 1) != 0;
	const bool HookHeld = pInput->m_Hook != 0;
	const bool Held = FireHeld || HookHeld;

	// Whether the aim keeps spinning while fire/hook is held.
	//   In jetpack:        only ninja and the hook keep spinning; every other
	//                      fire action locks the aim to the cursor.
	//   Without jetpack:   hammer / gun / ninja spin, other weapons lock to cursor.
	const bool SpinWhileHeld = Jetpack
		? (Weapon == WEAPON_NINJA || HookHeld)
		: (Weapon == WEAPON_HAMMER || Weapon == WEAPON_GUN || Weapon == WEAPON_NINJA);

	const bool FireJustPressed = FireHeld && (pLastInput->m_Fire & 1) == 0;
	const bool HookJustPressed = HookHeld && pLastInput->m_Hook == 0;
	const bool JustPressed = FireJustPressed || HookJustPressed;

	// First tick of press: always snap to the cursor (single shot / hook).
	if(JustPressed)
		return;

	// Hold with a weapon that should not spin -> stay on cursor.
	if(Held && !SpinWhileHeld)
		return;

	// Otherwise spin.
	const float Len = std::max(length(MousePos), 1.0f);
	pInput->m_TargetX = round_to_int(cosf(SpinAngle) * Len);
	pInput->m_TargetY = round_to_int(sinf(SpinAngle) * Len);
	if(!pInput->m_TargetX && !pInput->m_TargetY)
		pInput->m_TargetX = 1;
}

int CControls::ActiveSlot() const
{
	// Extra-слот только если extra реально онлайн и мы им управляем.
	// Иначе всегда ClDummy (0/1), как в оригинальном DDNet.
	if(Client()->IsExtraActive())
	{
		const int Idx = Client()->GetActiveExtraIndex();
		if(Idx >= 0)
			return 2 + Idx;
	}
	return g_Config.m_ClDummy;
}
CControls::CControls()
{
	mem_zero(&m_aLastData, sizeof(m_aLastData));
	std::fill(std::begin(m_aMouseOnAction), std::end(m_aMouseOnAction), false);
	std::fill(std::begin(m_aMousePos), std::end(m_aMousePos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMousePosOnAction), std::end(m_aMousePosOnAction), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aTargetPos), std::end(m_aTargetPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMouseInputType), std::end(m_aMouseInputType), EMouseInputType::ABSOLUTE);
}

void CControls::OnReset()
{
	for(int i = 0; i < NUM_CONNS; i++)
		ResetInput(i);

	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);

	m_LastSendTime = 0;
}

void CControls::ResetInput(int Dummy)
{
	m_aLastData[Dummy].m_Direction = 0;
	// simulate releasing the fire button
	if((m_aLastData[Dummy].m_Fire & 1) != 0)
		m_aLastData[Dummy].m_Fire++;
	m_aLastData[Dummy].m_Fire &= INPUT_STATE_MASK;
	m_aLastData[Dummy].m_Jump = 0;
	m_aInputData[Dummy] = m_aLastData[Dummy];

	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
}

void CControls::OnPlayerDeath()
{
	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);
}

void CControls::ForceReleaseFire(int Slot)
{
	if(Slot < 0 || Slot >= NUM_CONNS)
		return;
	// m_Fire — счётчик: нечётное = зажато, чётное = отпущено.
	// Принудительно переводим в чётное и синхронизируем m_aLastData,
	// чтобы SnapInput не отправил «зажатое» состояние при следующем изменении
	// других полей (мышь, чат, и т.д.).
	if(m_aInputData[Slot].m_Fire & 1)
		m_aInputData[Slot].m_Fire = (m_aInputData[Slot].m_Fire + 1) & 63;
	m_aLastData[Slot].m_Fire = m_aInputData[Slot].m_Fire;
}

void CControls::SetSlotFireReleased(int Slot, int Fire)
{
	if(Slot < 0 || Slot >= NUM_CONNS)
		return;
	Fire &= INPUT_STATE_MASK;
	if(Fire & 1)
		Fire = (Fire + 1) & INPUT_STATE_MASK;
	m_aInputData[Slot].m_Fire = Fire;
	m_aLastData[Slot].m_Fire = Fire;
}

struct CInputState
{
	CControls *m_pControls;
	int *m_apVariables[NUM_CONNS];
	bool m_IsMouseAction;
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int Stroke = pResult->GetInteger(0);
	*pState->m_apVariables[pState->m_pControls->ActiveSlot()] = Stroke;

	if(Stroke != 0 && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming && pState->m_pControls->GameClient()->m_Binds.m_HandlingNewBind)
		pState->m_pControls->m_aMouseOnAction[pState->m_pControls->ActiveSlot()] = true;
}

void CControls::ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if((pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active) || pState->m_pControls->GameClient()->m_Spectator.IsActive())
		return;

	int *pVariable = pState->m_apVariables[pState->m_pControls->ActiveSlot()];
	const int Stroke = pResult->GetInteger(0);
	if(((*pVariable) & 1) != Stroke)
	{
		(*pVariable)++;
		if(Stroke != 0 && pState->m_IsMouseAction && g_Config.m_ClSubTickAiming && pState->m_pControls->GameClient()->m_Binds.m_HandlingNewBind)
			pState->m_pControls->m_aMouseOnAction[pState->m_pControls->ActiveSlot()] = true;
	}
	*pVariable &= INPUT_STATE_MASK;
}

struct CInputSet
{
	CControls *m_pControls;
	int *m_apVariables[NUM_CONNS];
	int m_Value;
};

void CControls::ConKeyInputSet(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	if(pResult->GetInteger(0))
	{
		*pSet->m_apVariables[pSet->m_pControls->ActiveSlot()] = pSet->m_Value;
	}
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	ConKeyInputCounter(pResult, pSet);
	pSet->m_pControls->m_aInputData[pSet->m_pControls->ActiveSlot()].m_WantedWeapon = 0;
}

void CControls::OnConsoleInit()
{
	// game commands
	{
		static CInputState s_State = {this, {&m_aInputDirectionLeft[0], &m_aInputDirectionLeft[1], &m_aInputDirectionLeft[2], &m_aInputDirectionLeft[3]}, false};
		Console()->Register("+left", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move left");
	}
	{
		static CInputState s_State = {this, {&m_aInputDirectionRight[0], &m_aInputDirectionRight[1], &m_aInputDirectionRight[2], &m_aInputDirectionRight[3]}, false};
		Console()->Register("+right", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move right");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Jump, &m_aInputData[1].m_Jump, &m_aInputData[2].m_Jump, &m_aInputData[3].m_Jump}, false};
		Console()->Register("+jump", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Jump");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Hook, &m_aInputData[1].m_Hook, &m_aInputData[2].m_Hook, &m_aInputData[3].m_Hook}, true};
		Console()->Register("+hook", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Hook");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Fire, &m_aInputData[1].m_Fire, &m_aInputData[2].m_Fire, &m_aInputData[3].m_Fire}, true};
		Console()->Register("+fire", "", CFGFLAG_CLIENT, ConKeyInputCounter, &s_State, "Fire");
	}
	{
		static CInputState s_State = {this, {&m_aShowHookColl[0], &m_aShowHookColl[1], &m_aShowHookColl[2], &m_aShowHookColl[3]}, false};
		Console()->Register("+showhookcoll", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Show Hook Collision");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon, &m_aInputData[2].m_WantedWeapon, &m_aInputData[3].m_WantedWeapon}, 1};
		Console()->Register("+weapon1", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to hammer");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon, &m_aInputData[2].m_WantedWeapon, &m_aInputData[3].m_WantedWeapon}, 2};
		Console()->Register("+weapon2", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to gun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon, &m_aInputData[2].m_WantedWeapon, &m_aInputData[3].m_WantedWeapon}, 3};
		Console()->Register("+weapon3", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to shotgun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon, &m_aInputData[2].m_WantedWeapon, &m_aInputData[3].m_WantedWeapon}, 4};
		Console()->Register("+weapon4", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to grenade");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon, &m_aInputData[2].m_WantedWeapon, &m_aInputData[3].m_WantedWeapon}, 5};
		Console()->Register("+weapon5", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to laser");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_NextWeapon, &m_aInputData[1].m_NextWeapon, &m_aInputData[2].m_NextWeapon, &m_aInputData[3].m_NextWeapon}, 0};
		Console()->Register("+nextweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to next weapon");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_PrevWeapon, &m_aInputData[1].m_PrevWeapon, &m_aInputData[2].m_PrevWeapon, &m_aInputData[3].m_PrevWeapon}, 0};
		Console()->Register("+prevweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to previous weapon");
	}

	Console()->Register("tp_to_cursor", "", CFGFLAG_CLIENT, ConTpToCursor, this, "Practice: teleport yourself to your cursor");
	Console()->Register("tp_dummy_to_cursor", "", CFGFLAG_CLIENT, ConTpDummyToCursor, this, "Practice: teleport the other tee to your cursor");
}

void CControls::ConTpToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN);
}

void CControls::ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!pSelf->Client()->DummyConnected())
	{
		pSelf->GameClient()->Echo(Localize("Dummy is not connected"));
		return;
	}

	// Send on the inactive connection; the server only teleports the sender.
	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_MAIN : IClient::CONN_DUMMY);
}

vec2 CControls::CursorWorldPos() const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const vec2 TargetPos = m_aTargetPos[ActiveSlot()];
	return Center + (TargetPos - Center) * GameClient()->m_Camera.m_Zoom;
}

void CControls::SendPracticeTeleportToCursor(int Conn)
{
	const vec2 CursorPos = CursorWorldPos();
	CNetMsg_Cl_PracticeTeleport Msg;
	Msg.m_X = round_to_int(CursorPos.x);
	Msg.m_Y = round_to_int(CursorPos.y);
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons)
			m_aInputData[ActiveSlot()].m_WantedWeapon = pMsg->m_Weapon + 1;
		// We don't really know ammo count, until we'll switch to that weapon, but any non-zero count will suffice here
		m_aAmmoCount[std::max(0, pMsg->m_Weapon % NUM_WEAPONS)] = 10;
	}
}

int CControls::SnapInput(int *pData, int Slot)
{
	if(Slot < 0)
		Slot = ActiveSlot();

	if(m_ForceSnapInput)
	{
		m_ForceSnapInput = false;
		m_aInputData[Slot].m_TargetX = (int)m_aMousePos[Slot].x;
		m_aInputData[Slot].m_TargetY = (int)m_aMousePos[Slot].y;
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
			ApplySpin(&m_aInputData[Slot], &m_aLastData[Slot], Slot, m_aMousePos[Slot], GameClient()->m_Players.GetDirectionRotationOffset());
		mem_copy(pData, &m_aInputData[Slot], sizeof(CNetObj_PlayerInput));
		return sizeof(CNetObj_PlayerInput);
	}

	// update player state
	if(GameClient()->m_Chat.IsActive())
		m_aInputData[Slot].m_PlayerFlags = PLAYERFLAG_CHATTING;
	else if(GameClient()->m_Menus.IsActive())
		m_aInputData[Slot].m_PlayerFlags = PLAYERFLAG_IN_MENU;
	else
		m_aInputData[Slot].m_PlayerFlags = PLAYERFLAG_PLAYING;

	if(GameClient()->m_Scoreboard.IsActive())
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_SCOREBOARD;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Controls.m_aShowHookColl[Slot])
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_AIM;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Camera.CamType() == CCamera::CAMTYPE_SPEC)
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_SPEC_CAM;

	switch(m_aMouseInputType[Slot])
	{
	case CControls::EMouseInputType::AUTOMATED:
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE;
		break;
	case CControls::EMouseInputType::ABSOLUTE:
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE | PLAYERFLAG_INPUT_MANUAL;
		break;
	case CControls::EMouseInputType::RELATIVE:
		m_aInputData[Slot].m_PlayerFlags |= PLAYERFLAG_INPUT_MANUAL;
		break;
	}

	bool Send = m_aLastData[Slot].m_PlayerFlags != m_aInputData[Slot].m_PlayerFlags;

	m_aLastData[Slot].m_PlayerFlags = m_aInputData[Slot].m_PlayerFlags;

	// we freeze the input if chat or menu is activated
	if(!(m_aInputData[Slot].m_PlayerFlags & PLAYERFLAG_PLAYING))
	{
		if(Slot == ActiveSlot())
		{
			if(!GameClient()->m_GameInfo.m_BugDDRaceInput)
			{
				// DDNet: полный сброс (direction/jump/fire/hook).
				ResetInput(Slot);
			}
			else
			{
				// DDRace: BugDDRaceInput сохраняет direction/hook/jump,
				// чтобы персонаж не останавливался в меню. Но fire нужно
				// отпустить — иначе зажатый fire при открытии меню
				// отправляется на сервер как «press», и персонаж стреляет.
				// SetSlotFireReleased переводит fire в чётное (release),
				// синхронизируя m_aInputData и m_aLastData.
				SetSlotFireReleased(Slot, m_aLastData[Slot].m_Fire);
			}
		}

		mem_copy(pData, &m_aInputData[Slot], sizeof(m_aInputData[0]));

		// set the target anyway though so that we can keep seeing our surroundings,
		// even if chat or menu are activated
		m_aInputData[Slot].m_TargetX = (int)m_aMousePos[Slot].x;
		m_aInputData[Slot].m_TargetY = (int)m_aMousePos[Slot].y;

		// send once a second just to be sure
		Send = Send || time_get() > m_LastSendTime + time_freq();
	}
	else
	{
		m_aInputData[Slot].m_TargetX = (int)m_aMousePos[Slot].x;
		m_aInputData[Slot].m_TargetY = (int)m_aMousePos[Slot].y;

		if(g_Config.m_ClSubTickAiming && m_aMousePosOnAction[Slot] != vec2(0.0f, 0.0f))
		{
			m_aInputData[Slot].m_TargetX = (int)m_aMousePosOnAction[Slot].x;
			m_aInputData[Slot].m_TargetY = (int)m_aMousePosOnAction[Slot].y;
			m_aMousePosOnAction[Slot] = vec2(0.0f, 0.0f);
		}

		if(!m_aInputData[Slot].m_TargetX && !m_aInputData[Slot].m_TargetY)
		{
			m_aInputData[Slot].m_TargetX = 1;
			m_aMousePos[Slot].x = 1;
		}

		// set direction
		m_aInputData[Slot].m_Direction = 0;
		if(m_aInputDirectionLeft[Slot] && !m_aInputDirectionRight[Slot])
			m_aInputData[Slot].m_Direction = -1;
		if(!m_aInputDirectionLeft[Slot] && m_aInputDirectionRight[Slot])
			m_aInputData[Slot].m_Direction = 1;

		// dummy copy moves
		if(g_Config.m_ClDummyCopyMoves)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;

			// Don't copy any input to dummy when spectating others
			if(!GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
			{
				pDummyInput->m_Direction = m_aInputData[Slot].m_Direction;
				pDummyInput->m_Hook = m_aInputData[Slot].m_Hook;
				pDummyInput->m_Jump = m_aInputData[Slot].m_Jump;
				pDummyInput->m_PlayerFlags = m_aInputData[Slot].m_PlayerFlags;
				pDummyInput->m_TargetX = m_aInputData[Slot].m_TargetX;
				pDummyInput->m_TargetY = m_aInputData[Slot].m_TargetY;
				pDummyInput->m_WantedWeapon = m_aInputData[Slot].m_WantedWeapon;

				if(!g_Config.m_ClDummyControl)
					pDummyInput->m_Fire += m_aInputData[Slot].m_Fire - m_aLastData[Slot].m_Fire;

				pDummyInput->m_NextWeapon += m_aInputData[Slot].m_NextWeapon - m_aLastData[Slot].m_NextWeapon;
				pDummyInput->m_PrevWeapon += m_aInputData[Slot].m_PrevWeapon - m_aLastData[Slot].m_PrevWeapon;
			}

			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		if(g_Config.m_ClDummyControl)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;
			pDummyInput->m_Jump = g_Config.m_ClDummyJump;

			if(g_Config.m_ClDummyFire)
				pDummyInput->m_Fire = g_Config.m_ClDummyFire;
			else if((pDummyInput->m_Fire & 1) != 0)
				pDummyInput->m_Fire++;

			pDummyInput->m_Hook = g_Config.m_ClDummyHook;
			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		// stress testing
		if(g_Config.m_DbgStress)
		{
			float t = Client()->LocalTime();
			mem_zero(&m_aInputData[Slot], sizeof(m_aInputData[0]));

			m_aInputData[Slot].m_Direction = ((int)t / 2) & 1;
			m_aInputData[Slot].m_Jump = ((int)t);
			m_aInputData[Slot].m_Fire = ((int)(t * 10));
			m_aInputData[Slot].m_Hook = ((int)(t * 2)) & 1;
			m_aInputData[Slot].m_WantedWeapon = ((int)t) % NUM_WEAPONS;
			m_aInputData[Slot].m_TargetX = (int)(std::sin(t * 3) * 100.0f);
			m_aInputData[Slot].m_TargetY = (int)(std::cos(t * 3) * 100.0f);
		}

		// Direction rotation: replace the aim with an independent spin angle
		// while fire and hook are released. When either is pressed, the input
		// is left untouched so the shot/hook lands under the crosshair.
		// Skip in spectate — local input doesn't matter there, and churning
		// it every tick causes visible lag for other players in prediction.
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
			ApplySpin(&m_aInputData[Slot], &m_aLastData[Slot], Slot, m_aMousePos[Slot], GameClient()->m_Players.GetDirectionRotationOffset());
		// check if we need to send input
		Send = Send || m_aInputData[Slot].m_Direction != m_aLastData[Slot].m_Direction;
		Send = Send || m_aInputData[Slot].m_Jump != m_aLastData[Slot].m_Jump;
		Send = Send || m_aInputData[Slot].m_Fire != m_aLastData[Slot].m_Fire;
		Send = Send || m_aInputData[Slot].m_Hook != m_aLastData[Slot].m_Hook;
		Send = Send || m_aInputData[Slot].m_WantedWeapon != m_aLastData[Slot].m_WantedWeapon;
		Send = Send || m_aInputData[Slot].m_NextWeapon != m_aLastData[Slot].m_NextWeapon;
		Send = Send || m_aInputData[Slot].m_PrevWeapon != m_aLastData[Slot].m_PrevWeapon;
		Send = Send || time_get() > m_LastSendTime + time_freq() / 25; // send at least 25 Hz
		Send = Send || (GameClient()->m_Snap.m_pLocalCharacter && GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_NINJA && (m_aInputData[Slot].m_Direction || m_aInputData[Slot].m_Jump || m_aInputData[Slot].m_Hook));
	}

	// copy and return size
	m_aLastData[Slot] = m_aInputData[Slot];
	if(!Send)
		return 0;

	m_LastSendTime = time_get();
	mem_copy(pData, &m_aInputData[Slot], sizeof(m_aInputData[0]));
	return sizeof(m_aInputData[0]);
}

void CControls::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClAutoswitchWeaponsOutOfAmmo && !GameClient()->m_GameInfo.m_UnlimitedAmmo && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Keep track of ammo count, we know weapon ammo only when we switch to that weapon, this is tracked on server and protocol does not track that
		m_aAmmoCount[std::max(0, GameClient()->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS)] = GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount;
		// Autoswitch weapon if we're out of ammo
		if(m_aInputData[ActiveSlot()].m_Fire % 2 != 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount == 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_HAMMER &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_NINJA)
		{
			int Weapon;
			for(Weapon = WEAPON_LASER; Weapon > WEAPON_GUN; Weapon--)
			{
				if(Weapon == GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
					continue;
				if(m_aAmmoCount[Weapon] > 0)
					break;
			}
			if(Weapon != GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
				m_aInputData[ActiveSlot()].m_WantedWeapon = Weapon + 1;
		}
	}

	// update target pos
	if(GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// make sure to compensate for smooth dyncam to ensure the cursor stays still in world space if zoomed
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[ActiveSlot()];
		float Zoom = GameClient()->m_Camera.m_Zoom;
		m_aTargetPos[ActiveSlot()] = GameClient()->m_LocalCharacterPos + m_aMousePos[ActiveSlot()] - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aTargetPos[ActiveSlot()] = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aMousePos[ActiveSlot()];
	}
	else
	{
		m_aTargetPos[ActiveSlot()] = m_aMousePos[ActiveSlot()];
	}
}

bool CControls::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(GameClient()->IsWorldPaused())
		return false;

	if(CursorType == IInput::CURSOR_JOYSTICK && g_Config.m_InpControllerAbsolute && GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 AbsoluteDirection;
		if(Input()->GetActiveJoystick()->Absolute(&AbsoluteDirection.x, &AbsoluteDirection.y))
		{
			m_aMousePos[ActiveSlot()] = AbsoluteDirection * GetMaxMouseDistance();
			GameClient()->m_Controls.m_aMouseInputType[ActiveSlot()] = CControls::EMouseInputType::ABSOLUTE;
		}
		return true;
	}

	float Factor = 1.0f;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
	{
		Factor = g_Config.m_ClDyncamMousesens / 100.0f;
	}
	else
	{
		switch(CursorType)
		{
		case IInput::CURSOR_MOUSE:
			Factor = g_Config.m_InpMousesens / 100.0f;
			break;
		case IInput::CURSOR_JOYSTICK:
			Factor = g_Config.m_InpControllerSens / 100.0f;
			break;
		default:
			dbg_assert_failed("CControls::OnCursorMove CursorType %d", (int)CursorType);
		}
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		Factor *= GameClient()->m_Camera.m_Zoom;

	m_aMousePos[ActiveSlot()] += vec2(x, y) * Factor;
	GameClient()->m_Controls.m_aMouseInputType[ActiveSlot()] = CControls::EMouseInputType::RELATIVE;
	ClampMousePos();
	return true;
}

void CControls::ClampMousePos()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
	{
		m_aMousePos[ActiveSlot()].x = std::clamp(m_aMousePos[ActiveSlot()].x, -201.0f * 32, (Collision()->GetWidth() + 201.0f) * 32.0f);
		m_aMousePos[ActiveSlot()].y = std::clamp(m_aMousePos[ActiveSlot()].y, -201.0f * 32, (Collision()->GetHeight() + 201.0f) * 32.0f);
	}
	else
	{
		const float MouseMin = GetMinMouseDistance();
		const float MouseMax = GetMaxMouseDistance();

		float MouseDistance = length(m_aMousePos[ActiveSlot()]);
		if(MouseDistance < 0.001f)
		{
			m_aMousePos[ActiveSlot()].x = 0.001f;
			m_aMousePos[ActiveSlot()].y = 0;
			MouseDistance = 0.001f;
		}
		if(MouseDistance < MouseMin)
			m_aMousePos[ActiveSlot()] = normalize_pre_length(m_aMousePos[ActiveSlot()], MouseDistance) * MouseMin;
		MouseDistance = length(m_aMousePos[ActiveSlot()]);
		if(MouseDistance > MouseMax)
			m_aMousePos[ActiveSlot()] = normalize_pre_length(m_aMousePos[ActiveSlot()], MouseDistance) * MouseMax;
	}
}

float CControls::GetMinMouseDistance() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamMinDistance : g_Config.m_ClMouseMinDistance;
}

float CControls::GetMaxMouseDistance() const
{
	float CameraMaxDistance = 200.0f;
	float FollowFactor = (g_Config.m_ClDyncam ? g_Config.m_ClDyncamFollowFactor : g_Config.m_ClMouseFollowfactor) / 100.0f;
	float DeadZone = g_Config.m_ClDyncam ? g_Config.m_ClDyncamDeadzone : g_Config.m_ClMouseDeadzone;
	float MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
	return std::min(FollowFactor != 0.0f ? CameraMaxDistance / FollowFactor + DeadZone : MaxDistance, MaxDistance);
}
