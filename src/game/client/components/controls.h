/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>
#include <game/client/component.h>

#include <engine/client.h>
static constexpr int NUM_CONNS = IClient::NUM_CONNS;

class CControls : public CComponent
{
public:
	float GetMinMouseDistance() const;
	float GetMaxMouseDistance() const;

	bool m_ForceSnapInput = false;

	enum class EMouseInputType
	{
		ABSOLUTE,
		RELATIVE,
		AUTOMATED,
	};

	bool m_aMouseOnAction[NUM_CONNS];
	vec2 m_aMousePos[NUM_CONNS];
	vec2 m_aMousePosOnAction[NUM_CONNS];
	vec2 m_aTargetPos[NUM_CONNS];

	EMouseInputType m_aMouseInputType[NUM_CONNS];

	int m_aAmmoCount[NUM_WEAPONS];

	int64_t m_LastSendTime;
	CNetObj_PlayerInput m_aInputData[NUM_CONNS];
	CNetObj_PlayerInput m_aLastData[NUM_CONNS];
	int m_aInputDirectionLeft[NUM_CONNS];
	int m_aInputDirectionRight[NUM_CONNS];
	int m_aShowHookColl[NUM_CONNS];

	CControls();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnConsoleInit() override;
	virtual void OnPlayerDeath();
	int ActiveSlot() const;
	int SnapInput(int *pData, int Slot = -1);
	void ClampMousePos();
	void ResetInput(int Dummy);
	void ForceReleaseFire(int Slot);
	void SetSlotFireReleased(int Slot, int Fire);

private:
	void ApplySpin(CNetObj_PlayerInput *pInput, const CNetObj_PlayerInput *pLastInput, int Slot, const vec2 &MousePos, float SpinAngle);
	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConTpToCursor(IConsole::IResult *pResult, void *pUserData);
	static void ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData);
	vec2 CursorWorldPos() const;
	void SendPracticeTeleportToCursor(int Conn);
};
#endif
