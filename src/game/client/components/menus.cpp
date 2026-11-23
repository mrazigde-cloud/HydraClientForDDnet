/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include <base/color.h>
#include <base/dbg.h>
#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/client/updater.h>
#include <engine/config.h>
#include <engine/editor.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/console.h>
#include <game/client/components/events_manager.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace std::chrono_literals;

ColorRGBA CMenus::ms_GuiColor;
ColorRGBA CMenus::ms_ColorTabbarInactiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarActiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarHoverOutgame;
ColorRGBA CMenus::ms_ColorTabbarInactive;
ColorRGBA CMenus::ms_ColorTabbarActive = ColorRGBA(0, 0, 0, 0.5f);
ColorRGBA CMenus::ms_ColorTabbarHover;
ColorRGBA CMenus::ms_ColorTabbarInactiveIngame;
ColorRGBA CMenus::ms_ColorTabbarActiveIngame;
ColorRGBA CMenus::ms_ColorTabbarHoverIngame;

float CMenus::ms_ButtonHeight = 25.0f;
float CMenus::ms_ListheaderHeight = 17.0f;

CMenus::CMenus()
{
	m_Popup = POPUP_NONE;
	m_MenuPage = 0;
	m_GamePage = PAGE_GAME;
	m_AnimatedPage = 0;
	m_PageAnimationStart = 0;

	m_NeedRestartGraphics = false;
	m_NeedRestartSound = false;
	m_NeedSendinfo = false;
	m_NeedSendDummyinfo = false;
	m_MenuActive = true;
	m_ShowStart = true;

	str_copy(m_aCurrentDemoFolder, "demos");
	m_DemolistStorageType = IStorage::TYPE_ALL;

	m_DemoPlayerState = DEMOPLAYER_NONE;
	m_Dummy = false;

	for(SUIAnimator &Animator : m_aAnimatorsSettingsTab)
	{
		Animator.m_YOffset = -2.5f;
		Animator.m_HOffset = 5.0f;
		Animator.m_WOffset = 5.0f;
		Animator.m_RepositionLabel = true;
	}

	for(SUIAnimator &Animator : m_aAnimatorsBigPage)
	{
		Animator.m_YOffset = -5.0f;
		Animator.m_HOffset = 5.0f;
	}

	for(SUIAnimator &Animator : m_aAnimatorsSmallPage)
	{
		Animator.m_YOffset = -2.5f;
		Animator.m_HOffset = 2.5f;
	}

	m_PasswordInput.SetBuffer(g_Config.m_Password, sizeof(g_Config.m_Password));
	m_PasswordInput.SetHidden(true);
}

int CMenus::DoButton_Toggle(const void *pId, int Checked, const CUIRect *pRect, bool Active, const unsigned Flags)
{
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_GUIBUTTONS].m_Id);
	Graphics()->QuadsBegin();
	if(!Active)
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
	Graphics()->SelectSprite(Checked ? SPRITE_GUIBUTTON_ON : SPRITE_GUIBUTTON_OFF);
	IGraphics::CQuadItem QuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	if(Ui()->HotItem() == pId && Active)
	{
		Graphics()->SelectSprite(SPRITE_GUIBUTTON_HOVER);
		QuadItem = IGraphics::CQuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
	}
	Graphics()->QuadsEnd();

	return Active ? Ui()->DoButtonLogic(pId, Checked, pRect, Flags) : 0;
}
void CMenus::DrawMenuButtonOutline(const CUIRect *pRect, float Rounding)
{
	if(!g_Config.m_ClButtonOutline)
		return;
	const float Width = (float)g_Config.m_ClButtonOutlineWidth;
	if(Width <= 0.0f)
		return;

	float Shift = 0.0f;
	if(g_Config.m_ClButtonOutlineMode == 1)
	{
		const float AnimTime = (float)time() / (float)time_freq();
		const float Speed = (float)g_Config.m_ClButtonOutlineSpeed / 100.0f;
		Shift = fmodf(AnimTime * Speed, 1.0f);
	}

	const ColorRGBA Color1 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClButtonOutlineColor1));
	const ColorRGBA Color2 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClButtonOutlineColor2));

	// Clamp radius to half the smaller dimension so straight segments never
	// get negative lengths.
	const float R = std::min({Rounding, pRect->w * 0.5f, pRect->h * 0.5f});
	const float W = pRect->w;
	const float H = pRect->h;
	const float x0 = pRect->x;
	const float y0 = pRect->y;

	const float LineW = W - 2.0f * R;   // horizontal straight length
	const float LineH = H - 2.0f * R;   // vertical straight length
	const float ArcLen = (pi * 0.5f) * R;
	const float Perim = 2.0f * LineW + 2.0f * LineH + 4.0f * ArcLen;
	if(Perim <= 0.0f)
		return;

	// Color along the perimeter. `Triangle` gives C1 at t=0, C2 at t=0.5,
	// C1 at t=1 — the ring closes without a visible color seam, and the
	// `Shift` slides the whole gradient around the perimeter.
	auto MixColorAt = [&](float T) -> ColorRGBA {
		float P = fmodf(T + Shift, 1.0f);
		if(P < 0.0f)
			P += 1.0f;
		const float Tri = P < 0.5f ? (P * 2.0f) : (2.0f - P * 2.0f);
		const float Q = 1.0f - Tri;
		return ColorRGBA(
			Color1.r * Q + Color2.r * Tri,
			Color1.g * Q + Color2.g * Tri,
			Color1.b * Q + Color2.b * Tri,
			Color1.a * Q + Color2.a * Tri);
	};

	auto EmitSeg = [&](const vec2 &P0, const vec2 &P1, const vec2 &N0, const vec2 &N1, float tA, float tB) {
		const vec2 P0o = P0 + N0 * Width;
		const vec2 P1o = P1 + N1 * Width;
		const ColorRGBA cA = MixColorAt(tA);
		const ColorRGBA cB = MixColorAt(tB);
		Graphics()->SetColor4(cA, cA, cB, cB);
		IGraphics::CFreeformItem F(P0.x, P0.y, P0o.x, P0o.y, P1.x, P1.y, P1o.x, P1o.y);
		Graphics()->QuadsDrawFreeform(&F, 1);
	};

	auto AddLine = [&](const vec2 &Start, const vec2 &End, const vec2 &Normal, float TStart, float TEnd, int Segs) {
		for(int i = 0; i < Segs; i++)
		{
			const float sA = (float)i / Segs;
			const float sB = (float)(i + 1) / Segs;
			const vec2 P0 = Start + (End - Start) * sA;
			const vec2 P1 = Start + (End - Start) * sB;
			const float tA = TStart + (TEnd - TStart) * sA;
			const float tB = TStart + (TEnd - TStart) * sB;
			EmitSeg(P0, P1, Normal, Normal, tA, tB);
		}
	};

	auto AddArc = [&](const vec2 &Center, float StartAngle, float EndAngle, float Radius, float TStart, float TEnd, int Segs) {
		for(int i = 0; i < Segs; i++)
		{
			const float sA = (float)i / Segs;
			const float sB = (float)(i + 1) / Segs;
			const float aA = StartAngle + (EndAngle - StartAngle) * sA;
			const float aB = StartAngle + (EndAngle - StartAngle) * sB;
			const vec2 D0(cosf(aA), sinf(aA));
			const vec2 D1(cosf(aB), sinf(aB));
			const vec2 P0 = Center + D0 * Radius;
			const vec2 P1 = Center + D1 * Radius;
			const float tA = TStart + (TEnd - TStart) * sA;
			const float tB = TStart + (TEnd - TStart) * sB;
			EmitSeg(P0, P1, D0, D1, tA, tB);
		}
	};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	const int NumLineSegs = 6;
	const int NumArcSegs = 6;

	// Cumulative parameter at each piece boundary (clockwise from top edge)
	const float TTop1 = LineW / Perim;
	const float TTR1 = TTop1 + ArcLen / Perim;
	const float TRight1 = TTR1 + LineH / Perim;
	const float TBR1 = TRight1 + ArcLen / Perim;
	const float TBottom1 = TBR1 + LineW / Perim;
	const float TBL1 = TBottom1 + ArcLen / Perim;
	const float TLeft1 = TBL1 + LineH / Perim;
	const float TTL1 = TLeft1 + ArcLen / Perim;

	// Top edge (left → right)
	if(LineW > 0.0f)
		AddLine(vec2(x0 + R, y0), vec2(x0 + W - R, y0), vec2(0.0f, -1.0f), 0.0f, TTop1, NumLineSegs);

	// Top-right arc (-π/2 → 0)
	if(R > 0.0f)
		AddArc(vec2(x0 + W - R, y0 + R), -pi * 0.5f, 0.0f, R, TTop1, TTR1, NumArcSegs);

	// Right edge (top → bottom)
	if(LineH > 0.0f)
		AddLine(vec2(x0 + W, y0 + R), vec2(x0 + W, y0 + H - R), vec2(1.0f, 0.0f), TTR1, TRight1, NumLineSegs);

	// Bottom-right arc (0 → π/2)
	if(R > 0.0f)
		AddArc(vec2(x0 + W - R, y0 + H - R), 0.0f, pi * 0.5f, R, TRight1, TBR1, NumArcSegs);

	// Bottom edge (right → left)
	if(LineW > 0.0f)
		AddLine(vec2(x0 + W - R, y0 + H), vec2(x0 + R, y0 + H), vec2(0.0f, 1.0f), TBR1, TBottom1, NumLineSegs);

	// Bottom-left arc (π/2 → π)
	if(R > 0.0f)
		AddArc(vec2(x0 + R, y0 + H - R), pi * 0.5f, pi, R, TBottom1, TBL1, NumArcSegs);

	// Left edge (bottom → top)
	if(LineH > 0.0f)
		AddLine(vec2(x0, y0 + H - R), vec2(x0, y0 + R), vec2(-1.0f, 0.0f), TBL1, TLeft1, NumLineSegs);

	// Top-left arc (π → 3π/2)
	if(R > 0.0f)
		AddArc(vec2(x0 + R, y0 + R), pi, pi * 1.5f, R, TLeft1, TTL1, NumArcSegs);

	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
int CMenus::DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, const void *pImageHotId, SUIAnimator *pAnimator)
{
	// Hover animation: expand around center while keeping the original click zone.
	CUIRect Rect = *pRect;
	if(pAnimator != nullptr)
	{
		auto Time = time_get_nanoseconds();
		if(pAnimator->m_Time + 100ms < Time)
		{
			pAnimator->m_Value = pAnimator->m_Active ? 1 : 0;
			pAnimator->m_Time = Time;
		}

		const bool MouseInside = Ui()->HotItem() == pButtonContainer;
		pAnimator->m_Active = Checked || MouseInside || Ui()->CheckActiveItem(pButtonContainer);

		if(pAnimator->m_Active)
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value + (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);
		else
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value - (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);

		pAnimator->m_Time = Time;

		const float V = pAnimator->m_Value * pAnimator->m_Value * (3.0f - 2.0f * pAnimator->m_Value);
		const float DeltaW = V * pAnimator->m_WOffset;
		const float DeltaH = V * pAnimator->m_HOffset;
		Rect.x -= DeltaW * 0.5f;
		Rect.y -= DeltaH * 0.5f;
		Rect.w += DeltaW;
		Rect.h += DeltaH;
	}

	CUIRect Text = *pRect;
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	if(Text.h <= 0.0f)
	{
		Text = *pRect;
		Text.Margin(1.0f, &Text);
	}

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);
	DrawMenuButtonOutline(&Rect, Rounding);
	Rect.Draw(Color, Corners, Rounding);

	if(pImageName)
	{
		CUIRect Image;
		Rect.VSplitRight(Rect.h * BUTTON_IMAGE_WIDTH_FACTOR, &Text, &Image);
		// render image
		const CMenuImage *pImage = FindMenuImage(pImageName);
		if(pImage)
		{
			const bool ImageHot = Ui()->HotItem() == pButtonContainer || (pImageHotId != nullptr && Ui()->HotItem() == pImageHotId);
			Graphics()->TextureSet(ImageHot ? pImage->m_OrgTexture : pImage->m_GreyTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	}

	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, Flags);
}

int CMenus::DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator, const ColorRGBA *pDefaultColor, const ColorRGBA *pActiveColor, const ColorRGBA *pHoverColor, float EdgeRounding, const CCommunityIcon *pCommunityIcon)
{
	const bool MouseInside = Ui()->HotItem() == pButtonContainer;
	CUIRect Rect = *pRect;

	if(pAnimator != nullptr)
	{
		auto Time = time_get_nanoseconds();

		if(pAnimator->m_Time + 100ms < Time)
		{
			pAnimator->m_Value = pAnimator->m_Active ? 1 : 0;
			pAnimator->m_Time = Time;
		}

		pAnimator->m_Active = Checked || MouseInside;

		if(pAnimator->m_Active)
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value + (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);
		else
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value - (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);

		const float Eased = pAnimator->m_Value * pAnimator->m_Value * (3.0f - 2.0f * pAnimator->m_Value);
		Rect.w += Eased * pAnimator->m_WOffset;
		Rect.h += Eased * pAnimator->m_HOffset;
		Rect.x += Eased * pAnimator->m_XOffset;
		Rect.y += Eased * pAnimator->m_YOffset;

		pAnimator->m_Time = Time;
	}

	if(Checked)
	{
		ColorRGBA ColorMenuTab = ms_ColorTabbarActive;
		if(pActiveColor)
			ColorMenuTab = *pActiveColor;

		Rect.Draw(ColorMenuTab, Corners, EdgeRounding);
	}
	else
	{
		if(MouseInside)
		{
			ColorRGBA HoverColorMenuTab = ms_ColorTabbarHover;
			if(pHoverColor)
				HoverColorMenuTab = *pHoverColor;

			Rect.Draw(HoverColorMenuTab, Corners, EdgeRounding);
		}
		else
		{
			ColorRGBA ColorMenuTab = ms_ColorTabbarInactive;
			if(pDefaultColor)
				ColorMenuTab = *pDefaultColor;

			Rect.Draw(ColorMenuTab, Corners, EdgeRounding);
		}
	}

	if(pAnimator != nullptr)
	{
		if(pAnimator->m_RepositionLabel)
		{
			Rect.x += Rect.w - pRect->w + Rect.x - pRect->x;
			Rect.y += Rect.h - pRect->h + Rect.y - pRect->y;
		}

		if(!pAnimator->m_ScaleLabel)
		{
			Rect.w = pRect->w;
			Rect.h = pRect->h;
		}
	}

	if(pCommunityIcon)
	{
		CUIRect CommunityIcon;
		Rect.Margin(2.0f, &CommunityIcon);
		m_CommunityIcons.Render(pCommunityIcon, CommunityIcon, true);
	}
	else
	{
		CUIRect Label;
		Rect.HMargin(2.0f, &Label);
		Ui()->DoLabel(&Label, pText, Label.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	}

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align)
{
	if(Checked == 2)
		pRect->Draw(ColorRGBA(1, 0.98f, 0.5f, 0.55f), IGraphics::CORNER_T, 5.0f);
	else if(Checked)
		pRect->Draw(ColorRGBA(1, 1, 1, 0.5f), IGraphics::CORNER_T, 5.0f);

	CUIRect Temp;
	pRect->VMargin(5.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, pRect->h * CUi::ms_FontmodHeight, Align);
	return Ui()->DoButtonLogic(pId, Checked, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect)
{
	if(Checked || (pParentId != nullptr && Ui()->HotItem() == pParentId) || Ui()->HotItem() == pButtonId)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		const float Alpha = Ui()->HotItem() == pButtonId ? 0.2f : 0.0f;
		TextRender()->TextColor(Checked ? ColorRGBA(1.0f, 0.85f, 0.3f, 0.8f + Alpha) : ColorRGBA(0.5f, 0.5f, 0.5f, 0.8f + Alpha));
		SLabelProperties Props;
		Props.m_MaxWidth = pRect->w;
		Ui()->DoLabel(pRect, FontIcon::STAR, 12.0f, TEXTALIGN_MC, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	return Ui()->DoButtonLogic(pButtonId, 0, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags)
{
	CUIRect Box, Label;
	pRect->VSplitLeft(pRect->h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);

	Box.Margin(2.0f, &Box);
	Box.Draw(ColorRGBA(1, 1, 1, 0.25f * Ui()->ButtonColorMul(pId)), IGraphics::CORNER_ALL, 3.0f);

	const bool Checkable = *pBoxText == 'X';
	if(Checkable)
	{
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&Box, FontIcon::XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
	{
		Ui()->DoLabel(&Box, pBoxText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	}

	TextRender()->SetRenderFlags(0);
	Ui()->DoLabel(&Label, pText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);

	return Ui()->DoButtonLogic(pId, 0, pRect, Flags);
}

bool CMenus::DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value)
{
	dbg_assert(vButtonContainers.size() == vValues.size(), "vButtonContainers and vValues must have the same size");
	dbg_assert(vButtonContainers.size() == vLabels.size(), "vButtonContainers and vLabels must have the same size");
	const int N = vButtonContainers.size();
	const float Spacing = 2.0f;
	const float ButtonHeight = 20.0f;
	CUIRect Label, Buttons;
	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(ButtonHeight, &Buttons, &View);
	Buttons.VSplitMid(&Label, &Buttons, 10.0f);
	Buttons.HMargin(2.0f, &Buttons);
	Ui()->DoLabel(&Label, pLabel, 13.0f, TEXTALIGN_ML);
	const float W = Buttons.w / N;
	bool Pressed = false;
	for(int i = 0; i < N; ++i)
	{
		CUIRect Button;
		Buttons.VSplitLeft(W, &Button, &Buttons);
		int Corner = IGraphics::CORNER_NONE;
		if(i == 0)
			Corner = IGraphics::CORNER_L;
		if(i == N - 1)
			Corner = IGraphics::CORNER_R;
		if(DoButton_Menu(&vButtonContainers[i], vLabels[i], vValues[i] == Value, &Button, BUTTONFLAG_LEFT, nullptr, Corner))
		{
			Pressed = true;
			Value = vValues[i];
		}
	}
	return Pressed;
}

ColorHSLA CMenus::DoLine_ColorPicker(CButtonContainer *pResetId, const float LineSize, const float LabelSize, const float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA DefaultColor, bool CheckBoxSpacing, int *pCheckBoxValue, bool Alpha)
{
	CUIRect Section, ColorPickerButton, ResetButton, Label;

	pMainRect->HSplitTop(LineSize, &Section, pMainRect);
	pMainRect->HSplitTop(BottomMargin, nullptr, pMainRect);

	Section.VSplitRight(60.0f, &Section, &ResetButton);
	Section.VSplitRight(8.0f, &Section, nullptr);
	Section.VSplitRight(Section.h, &Section, &ColorPickerButton);
	Section.VSplitRight(8.0f, &Label, nullptr);

	if(pCheckBoxValue != nullptr)
	{
		Label.Margin(2.0f, &Label);
		if(DoButton_CheckBox(pCheckBoxValue, pText, *pCheckBoxValue, &Label))
			*pCheckBoxValue ^= 1;
	}
	else if(CheckBoxSpacing)
	{
		Label.VSplitLeft(Label.h + 5.0f, nullptr, &Label);
	}
	if(pCheckBoxValue == nullptr)
	{
		Ui()->DoLabel(&Label, pText, LabelSize, TEXTALIGN_ML);
	}

	const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorPickerButton, pColorValue, Alpha);

	ResetButton.HMargin(2.0f, &ResetButton);
	if(DoButton_Menu(pResetId, Localize("Reset"), 0, &ResetButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
	{
		*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(Alpha);
	}

	return PickedColor;
}

ColorHSLA CMenus::DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha)
{
	ColorHSLA HslaColor = ColorHSLA(*pHslaColor, Alpha);

	ColorRGBA Outline = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);
	Outline.a *= Ui()->ButtonColorMul(pHslaColor);

	CUIRect Rect;
	pRect->Margin(3.0f, &Rect);

	pRect->Draw(Outline, IGraphics::CORNER_ALL, 4.0f);
	Rect.Draw(color_cast<ColorRGBA>(HslaColor), IGraphics::CORNER_ALL, 4.0f);

	if(Ui()->DoButtonLogic(pHslaColor, 0, pRect, BUTTONFLAG_LEFT))
	{
		m_ColorPickerPopupContext.m_pHslaColor = pHslaColor;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = Alpha;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	}
	else if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == pHslaColor)
	{
		HslaColor = color_cast<ColorHSLA>(m_ColorPickerPopupContext.m_HsvaColor);
	}

	return HslaColor;
}

int CMenus::DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin)
{
	CUIRect CheckBoxRect;
	pRect->HSplitTop(VMargin, &CheckBoxRect, pRect);

	int Logic = DoButton_CheckBox_Common(pId, pText, *pValue ? "X" : "", &CheckBoxRect, BUTTONFLAG_LEFT);

	if(Logic)
		*pValue ^= 1;

	return Logic;
}

int CMenus::DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	return DoButton_CheckBox_Common(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Checked);
	return DoButton_CheckBox_Common(pId, pText, aBuf, pRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
}

void CMenus::RenderMenubar(CUIRect Box, IClient::EClientState ClientState)
{
	CUIRect Button;

	int NewPage = -1;
	int ActivePage = -1;
	if(ClientState == IClient::STATE_OFFLINE)
	{
		ActivePage = m_MenuPage;
	}
	else if(ClientState == IClient::STATE_ONLINE)
	{
		ActivePage = m_GamePage;
	}
	else
	{
		dbg_assert_failed("Client state %d is invalid for RenderMenubar", ClientState);
	}

	// First render buttons aligned from right side so remaining
	// width is known when rendering buttons from left side.
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_QuitButton;
	ColorRGBA QuitColor(1, 0, 0, 0.5f);
	Ui()->DoToolTip(&s_QuitButton, &Button, Localize("Quit"));

	Box.VSplitRight(10.0f, &Box, nullptr);
	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_SettingsButton;
	if(DoButton_MenuTab(&s_SettingsButton, FontIcon::GEAR, ActivePage == PAGE_SETTINGS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_SETTINGS]))
	{
		NewPage = PAGE_SETTINGS;
	}
	Ui()->DoToolTip(&s_SettingsButton, &Button, Localize("Settings"));

	Box.VSplitRight(10.0f, &Box, nullptr);
	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_EditorButton;
	if(DoButton_MenuTab(&s_EditorButton, FontIcon::PEN_TO_SQUARE, 0, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_EDITOR]))
	{
		g_Config.m_ClEditor = 1;
	}
	Ui()->DoToolTip(&s_EditorButton, &Button, Localize("Editor"));

	if(ClientState == IClient::STATE_OFFLINE)
	{
		Box.VSplitRight(10.0f, &Box, nullptr);
		Box.VSplitRight(33.0f, &Box, &Button);
		static CButtonContainer s_DemoButton;
		if(DoButton_MenuTab(&s_DemoButton, FontIcon::CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_DEMOBUTTON]))
		{
			NewPage = PAGE_DEMOS;
		}
		Ui()->DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
		Box.VSplitRight(10.0f, &Box, nullptr);

		Box.VSplitLeft(33.0f, &Button, &Box);

		bool GotNewsOrUpdate = false;

#if defined(CONF_AUTOUPDATE)
		int State = Updater()->GetCurrentState();
		bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");
		if(State == IUpdater::CLEAN && NeedUpdate)
		{
			GotNewsOrUpdate = true;
		}
#endif

		GotNewsOrUpdate |= (bool)g_Config.m_UiUnreadNews;

		ColorRGBA HomeButtonColorAlert(0, 1, 0, 0.25f);
		ColorRGBA HomeButtonColorAlertHover(0, 1, 0, 0.5f);
		ColorRGBA *pHomeButtonColor = nullptr;
		ColorRGBA *pHomeButtonColorHover = nullptr;

		const char *pHomeScreenButtonLabel = FontIcon::HOUSE;
		if(GotNewsOrUpdate)
		{
			pHomeScreenButtonLabel = FontIcon::NEWSPAPER;
			pHomeButtonColor = &HomeButtonColorAlert;
			pHomeButtonColorHover = &HomeButtonColorAlertHover;
		}

		static CButtonContainer s_StartButton;
		if(DoButton_MenuTab(&s_StartButton, pHomeScreenButtonLabel, false, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_HOME], pHomeButtonColor, pHomeButtonColor, pHomeButtonColorHover, 10.0f))
		{
			// Ведём себя так же, как Escape: если анимация стартового меню
			// включена и текущая страница её поддерживает — запускаем обратный
			// transition. Иначе — мгновенный переход (как раньше).
			if(!m_StartMenuTransition.m_Active && !m_MenuPageTransition.m_Active)
			{
				Ui()->SetActiveItem(nullptr);

				if(g_Config.m_ClStartMenuAnimation &&
					((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) ||
						m_MenuPage == PAGE_DEMOS ||
						m_MenuPage == PAGE_SETTINGS))
				{
					m_StartMenuTransition.m_Active = true;
					m_StartMenuTransition.m_Reverse = true;
					m_StartMenuTransition.m_TargetPage = m_MenuPage;
					m_StartMenuTransition.m_StartTime = time();
				}
				else
				{
					m_ShowStart = true;
				}
			}
		}
		Ui()->DoToolTip(&s_StartButton, &Button, Localize("Main menu"));
		const float BrowserButtonWidth = 75.0f;
		Box.VSplitLeft(10.0f, nullptr, &Box);
		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_InternetButton;
		if(DoButton_MenuTab(&s_InternetButton, FontIcon::EARTH_AMERICAS, ActivePage == PAGE_INTERNET, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_INTERNET]))
		{
			NewPage = PAGE_INTERNET;
		}
		Ui()->DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_LanButton;
		if(DoButton_MenuTab(&s_LanButton, FontIcon::NETWORK_WIRED, ActivePage == PAGE_LAN, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_LAN]))
		{
			NewPage = PAGE_LAN;
		}
		Ui()->DoToolTip(&s_LanButton, &Button, Localize("LAN"));

		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_FavoritesButton;
		if(DoButton_MenuTab(&s_FavoritesButton, FontIcon::STAR, ActivePage == PAGE_FAVORITES, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_FAVORITES]))
		{
			NewPage = PAGE_FAVORITES;
		}
		Ui()->DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

		const std::vector<const CCommunity *> vpFavoriteCommunities = ServerBrowser()->FavoriteCommunities();
		const int MaxPage = PAGE_FAVORITES + vpFavoriteCommunities.size();
		if(
			!Ui()->IsPopupOpen() &&
			CLineInput::GetActiveInput() == nullptr &&
			(g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= MaxPage) &&
			(m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5))
		{
			if(Input()->KeyPress(KEY_RIGHT))
			{
				NewPage = g_Config.m_UiPage + 1;
				if(NewPage > MaxPage)
					NewPage = PAGE_INTERNET;
			}
			if(Input()->KeyPress(KEY_LEFT))
			{
				NewPage = g_Config.m_UiPage - 1;
				if(NewPage < PAGE_INTERNET)
					NewPage = MaxPage;
			}
		}

		size_t FavoriteCommunityIndex = 0;
		static CButtonContainer s_aFavoriteCommunityButtons[5];
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)BIT_TAB_FAVORITE_COMMUNITY_5 - BIT_TAB_FAVORITE_COMMUNITY_1 + 1);
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 + 1);
		for(const CCommunity *pCommunity : vpFavoriteCommunities)
		{
			if(Box.w < BrowserButtonWidth)
				break;
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
			if(DoButton_MenuTab(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FontIcon::ELLIPSIS, ActivePage == Page, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIT_TAB_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex], nullptr, nullptr, nullptr, 10.0f, m_CommunityIcons.Find(pCommunity->Id())))
			{
				NewPage = Page;
			}
			Ui()->DoToolTip(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], &Button, pCommunity->Name());

			++FavoriteCommunityIndex;
			if(FavoriteCommunityIndex >= std::size(s_aFavoriteCommunityButtons))
				break;
		}

		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
	{
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		// online menus
		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_GameButton;
		if(DoButton_MenuTab(&s_GameButton, Localize("Game"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL))
			NewPage = PAGE_GAME;

		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_PlayersButton;
		if(DoButton_MenuTab(&s_PlayersButton, Localize("Players"), ActivePage == PAGE_PLAYERS, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_PLAYERS;

		Box.VSplitLeft(130.0f, &Button, &Box);
		static CButtonContainer s_ServerInfoButton;
		if(DoButton_MenuTab(&s_ServerInfoButton, Localize("Server info"), ActivePage == PAGE_SERVER_INFO, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_SERVER_INFO;

		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_NetworkButton;
		if(DoButton_MenuTab(&s_NetworkButton, Localize("Browser"), ActivePage == PAGE_NETWORK, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_NETWORK;

		if(GameClient()->m_GameInfo.m_Race)
		{
			Box.VSplitLeft(90.0f, &Button, &Box);
			static CButtonContainer s_GhostButton;
			if(DoButton_MenuTab(&s_GhostButton, Localize("Ghost"), ActivePage == PAGE_GHOST, &Button, IGraphics::CORNER_NONE))
				NewPage = PAGE_GHOST;
		}

		Box.VSplitLeft(100.0f, &Button, &Box);
		Box.VSplitLeft(4.0f, nullptr, &Box);
		static CButtonContainer s_CallVoteButton;
		if(DoButton_MenuTab(&s_CallVoteButton, Localize("Call vote"), ActivePage == PAGE_CALLVOTE, &Button, IGraphics::CORNER_TR))
		{
			NewPage = PAGE_CALLVOTE;
			m_ControlPageOpening = true;
		}

		if(Box.w >= 10.0f + 33.0f + 10.0f)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			Box.VSplitRight(10.0f, &Box, nullptr);
			Box.VSplitRight(33.0f, &Box, &Button);
			static CButtonContainer s_DemoButton;
			if(DoButton_MenuTab(&s_DemoButton, FontIcon::CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_DEMOBUTTON]))
			{
				NewPage = PAGE_DEMOS;
			}
			Ui()->DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
			Box.VSplitRight(10.0f, &Box, nullptr);

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
	}

	if(NewPage != -1)
	{
		if(ClientState == IClient::STATE_ONLINE)
			SetGamePage(NewPage);
		else
			SetMenuPage(NewPage);
		SetShowStart(false);
	}
}

void CMenus::RenderLoadingDirect(const char *pCaption, const char *pContent, std::optional<float> Progress)
{
	// TODO: not supported right now due to separate render thread

	const std::chrono::nanoseconds Now = time_get_nanoseconds();

	int RefreshRate = g_Config.m_GfxVsync || !Progress.has_value() ? 60 : (in_range(g_Config.m_GfxRefreshRate, 1, 300) ? g_Config.m_GfxRefreshRate : 300);
	if(RefreshRate > 0 && Now - m_LoadingState.m_LastRender < std::chrono::nanoseconds(1s) / RefreshRate)
		return;

	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	Ui()->MapScreen();

	if(GameClient()->m_MenuBackground.IsLoading())
		return;

	// ---- Чёрный экран вместо бокса с надписью «Loading» ----
	// Полностью перекрываем задний фон загрузочного экрана.
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 1.0f);
	IGraphics::CQuadItem BlackQuad(0.0f, 0.0f, Ui()->Screen()->w, Ui()->Screen()->h);
	Graphics()->QuadsDrawTL(&BlackQuad, 1);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	m_LoadingState.m_LastRender = Now;

	Client()->UpdateAndSwap();
}

void CMenus::RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter)
{
	// does not support multithreading

	const int CurLoadRenderCount = m_LoadingState.m_Current;
	m_LoadingState.m_Current += IncreaseCounter;
	dbg_assert(m_LoadingState.m_Current <= m_LoadingState.m_Total, "Invalid progress for RenderLoading");
	RenderLoadingDirect(pCaption, pContent, m_LoadingState.m_Total > 0 ? std::make_optional(CurLoadRenderCount / (float)m_LoadingState.m_Total) : std::nullopt);
}

void CMenus::FinishLoading()
{
	m_LoadingState.m_Current = 0;
	m_LoadingState.m_Total = 0;
}
void CMenus::RandomizeNickAndSkins()
{
	// 1. Ник
	std::vector<std::string> vNicks;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CGameClient::CClientData &C = GameClient()->m_aClients[i];
		if(!C.m_Active) continue;
		if(C.m_aName[0] == '\0') continue;
		if(str_comp(C.m_aName, g_Config.m_PlayerName) == 0) continue;
		if(str_comp(C.m_aName, g_Config.m_ClDummyName) == 0) continue;
		vNicks.emplace_back(C.m_aName);
	}

	if(!vNicks.empty())
	{
		std::string Nick = vNicks[rand() % vNicks.size()];
		if(!Nick.empty())
		{
			if(Nick[0] >= 'a' && Nick[0] <= 'z')
				Nick[0] = Nick[0] - 'a' + 'A';
			else if(Nick[0] >= 'A' && Nick[0] <= 'Z')
				Nick[0] = Nick[0] - 'A' + 'a';
		}
		str_copy(g_Config.m_PlayerName, Nick.c_str(), sizeof(g_Config.m_PlayerName));
		str_copy(g_Config.m_ClDummyName, Nick.c_str(), sizeof(g_Config.m_ClDummyName));
	}

	// 2. Скины + цвета
	auto &SkinList = GameClient()->m_Skins.SkinList();
	if(SkinList.Skins().empty())
		SkinList.ForceRefresh();

	auto PickSkin = [&]() -> const char * {
		if(SkinList.Skins().empty()) return "default";
		const int Idx = rand() % (int)SkinList.Skins().size();
		auto *pC = SkinList.Skins()[Idx].SkinContainer();
		return pC ? pC->Name() : "default";
	};

	str_copy(g_Config.m_ClPlayerSkin, PickSkin(), sizeof(g_Config.m_ClPlayerSkin));
	g_Config.m_ClPlayerUseCustomColor = 1;
	g_Config.m_ClPlayerColorBody = rand() % 0xFFFFFF;
	g_Config.m_ClPlayerColorFeet = rand() % 0xFFFFFF;

	str_copy(g_Config.m_ClDummySkin, PickSkin(), sizeof(g_Config.m_ClDummySkin));
	g_Config.m_ClDummyUseCustomColor = 1;
	g_Config.m_ClDummyColorBody = rand() % 0xFFFFFF;
	g_Config.m_ClDummyColorFeet = rand() % 0xFFFFFF;

	// 3. Отправить на сервер
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		GameClient()->SendInfo(false);
		GameClient()->SendDummyInfo(false);
	}
}
void CMenus::RenderStartMenuIntro(CUIRect Screen, float T)
{
	m_HideStartMenuLogo = true;

	constexpr float P1_Zoom = 0.8f;
	constexpr float ShakeDur = 0.55f;
	constexpr float WaitDur = 0.4f;
	constexpr float P2_Fade = 1.0f;
	constexpr float HoldEnd = P1_Zoom + ShakeDur + WaitDur; // 1.75
	constexpr float ImpactT = 0.14f; // момент, когда логотип "бьётся" в 100% размера

	auto SmoothStep = [](float x) {
		x = std::clamp(x, 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	};
	auto EaseOutCubic = [](float x) {
		x = std::clamp(x, 0.0f, 1.0f);
		return 1.0f - std::pow(1.0f - x, 3.0f);
	};
	auto EaseInOutCubic = [](float x) {
		x = std::clamp(x, 0.0f, 1.0f);
		return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
	};

	const float BaseH = 103.0f;
	const float BaseW = m_HydraLogoLoaded ? BaseH * m_HydraLogoAspect : 360.0f;
	const vec2 StartC = Screen.Center();
	const float FloatTime = (float)time() / (float)time_freq();
	const float FloatY = sinf(FloatTime * 1.5f) * 6.0f;
	const vec2 EndC(Screen.w / 2.0f + 10.0f, Screen.y + 60.0f + FloatY + BaseH * 0.5f);

	// ---- Пружинный масштаб и наклон (затухающие колебания) ----
	const float Spring = std::max(0.0f, 1.0f - expf(-7.0f * T) * cosf(11.0f * T));
	const float Tilt = 0.16f * expf(-6.0f * T) * cosf(13.0f * T); // радианы

	// ---- Удар: сквош/стретч, вспышка, тряска ----
	const float Age = T - ImpactT;
	float Wob = 0.0f;
	float FlashAlpha = 0.0f;
	float ShakeX = 0.0f, ShakeY = 0.0f;
	if(Age >= 0.0f)
	{
		Wob = expf(-8.0f * Age) * sinf(30.0f * Age);

		constexpr float FlashDur = 0.25f;
		if(Age < FlashDur)
		{
			const float F = 1.0f - Age / FlashDur;
			FlashAlpha = 0.25f * F * F;
		}
		if(Age < ShakeDur)
		{
			const float Decay = (1.0f - Age / ShakeDur) * (1.0f - Age / ShakeDur);
			ShakeX = sinf(Age * 62.0f) * 12.0f * Decay;
			ShakeY = cosf(Age * 47.0f) * 9.0f * Decay;
		}
	}

	// ---- "Дыхание" во время паузы (в конце удержания равно нулю) ----
	const float BreathEnv = SmoothStep(T / P1_Zoom) * (T < HoldEnd ? 1.0f : 0.0f);
	const float Breath = 0.012f * sinf(6.0f * (T - HoldEnd)) * BreathEnv;

	// Равномерная пульсация вместо сквоша: пропорции баннера не искажаются
	const float ScaleX = Spring * (1.0f + 0.025f * Wob) + Breath;
	const float ScaleY = ScaleX;
	const float LogoAlpha = SmoothStep(T / 0.12f);

	// ---- Фаза ухода наверх ----
	float BlackAlpha = 1.0f;
	float FadeT = 0.0f;
	if(T >= HoldEnd)
	{
		FadeT = std::clamp((T - HoldEnd) / P2_Fade, 0.0f, 1.0f);
		BlackAlpha = 1.0f - SmoothStep(FadeT);
	}

	// ---- Рисовалки ----
	auto FillScreen = [&](float R, float G, float B, float A) {
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(R, G, B, A);
		IGraphics::CQuadItem Q(0.0f, 0.0f, Ui()->Screen()->w, Ui()->Screen()->h);
		Graphics()->QuadsDrawTL(&Q, 1);
		Graphics()->QuadsEnd();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	auto DrawBanner = [&](vec2 C, float W, float H, float Angle, float Alpha) {
		if(Alpha <= 0.001f || W <= 0.5f || H <= 0.5f)
			return;
		Graphics()->TextureSet(m_HydraLogoLoaded ? m_HydraLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetRotation(Angle); // поворот вокруг центра квада
		IGraphics::CQuadItem QuadItem(C.x - W * 0.5f, C.y - H * 0.5f, W, H);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	// Меню под чёрным (без своего логотипа)
	if(T >= HoldEnd)
		m_MenusStart.RenderStartMenu(Screen);

	// Чёрный overlay
	if(BlackAlpha > 0.001f)
		FillScreen(0.0f, 0.0f, 0.0f, BlackAlpha);

	// ---- Эффекты удара (под баннером) ----
	if(Age >= 0.0f && Age < 0.8f)
	{
		// Ударная волна
		constexpr float RingDur = 0.6f;
		if(Age < RingDur)
		{
			const float P = Age / RingDur;
			const float Radius = 40.0f + 380.0f * EaseOutCubic(P);
			const float Thick = 1.0f + 7.0f * (1.0f - P);
			const float A = 0.5f * (1.0f - P) * (1.0f - P);
			constexpr int Segs = 56;

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, A);
			for(int i = 0; i < Segs; i++)
			{
				const float A0 = (float)i / Segs * pi * 2.0f;
				const float A1 = (float)(i + 1) / Segs * pi * 2.0f;
				const vec2 D0(cosf(A0), sinf(A0));
				const vec2 D1(cosf(A1), sinf(A1));
				const vec2 I0 = StartC + D0 * Radius;
				const vec2 O0 = StartC + D0 * (Radius + Thick);
				const vec2 I1 = StartC + D1 * Radius;
				const vec2 O1 = StartC + D1 * (Radius + Thick);
				IGraphics::CFreeformItem F(I0.x, I0.y, O0.x, O0.y, I1.x, I1.y, O1.x, O1.y);
				Graphics()->QuadsDrawFreeform(&F, 1);
			}
			Graphics()->QuadsEnd();
		}

		// Искры
		constexpr float SparkDur = 0.7f;
		if(Age < SparkDur)
		{
			const float P = Age / SparkDur;
			const float E = EaseOutCubic(P);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(int i = 0; i < 18; i++)
			{
				const float Ang = i * 2.39996f; // золотой угол — равномерный разброс
				const float Reach = 120.0f + (i % 4) * 55.0f;
				const vec2 Pos = StartC + vec2(cosf(Ang), sinf(Ang)) * (60.0f + Reach * E);
				const float Sz = (2.0f + (i % 3) * 1.5f) * (1.0f - P);
				Graphics()->SetColor(1.0f, 0.95f, 0.8f, 0.9f * (1.0f - P));
				IGraphics::CQuadItem Q(Pos.x - Sz * 0.5f, Pos.y - Sz * 0.5f, Sz, Sz);
				Graphics()->QuadsDrawTL(&Q, 1);
			}
			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Эхо-пульс баннера
		constexpr float EchoDur = 0.5f;
		if(Age < EchoDur)
		{
			const float P = Age / EchoDur;
			const float S = 1.0f + 0.35f * EaseOutCubic(P);
			DrawBanner(StartC, BaseW * S, BaseH * S, 0.0f, 0.3f * (1.0f - P) * LogoAlpha);
		}
	}

	// Вспышка
	if(FlashAlpha > 0.001f)
		FillScreen(1.0f, 1.0f, 1.0f, FlashAlpha);

	// ---- Баннер ----
	if(ScaleX > 0.001f && ScaleY > 0.001f)
	{
		if(T < HoldEnd)
		{
			DrawBanner(StartC + vec2(ShakeX, ShakeY), BaseW * ScaleX, BaseH * ScaleY, Tilt, LogoAlpha);
		}
		else
		{
			// Ухода вверх: лёгкая "пульсация" размера и шлейф из двух призраков
			auto CenterAt = [&](float Ft) {
				const float U = EaseInOutCubic(Ft);
				return StartC + (EndC - StartC) * U;
			};
			const float Bump = 1.0f + 0.04f * sinf(pi * EaseInOutCubic(FadeT));

			for(int k = 2; k >= 1; k--)
			{
				const float Ft = FadeT - 0.05f * k;
				if(Ft > 0.0f && FadeT < 1.0f)
					DrawBanner(CenterAt(Ft), BaseW * Bump, BaseH * Bump, 0.0f, 0.18f / k);
			}
			DrawBanner(CenterAt(FadeT), BaseW * Bump, BaseH * Bump, 0.0f, 1.0f);
		}
	}
}
void CMenus::RenderNews(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_NEWS);

	g_Config.m_UiUnreadNews = false;

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.VSplitLeft(15.0f, nullptr, &MainView);

	CUIRect Label;

	const char *pStr = Client()->News();
	char aLine[256];
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		const int Len = str_length(aLine);
		if(Len > 0 && aLine[0] == '|' && aLine[Len - 1] == '|')
		{
			MainView.HSplitTop(30.0f, &Label, &MainView);
			aLine[Len - 1] = '\0';
			Ui()->DoLabel(&Label, aLine + 1, 20.0f, TEXTALIGN_ML);
		}
		else
		{
			MainView.HSplitTop(20.0f, &Label, &MainView);
			Ui()->DoLabel(&Label, aLine, 15.f, TEXTALIGN_ML);
		}
	}
}

void CMenus::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);
	m_MenusIngameTouchControls.OnInterfacesInit(pClient);
	m_MenusSettingsControls.OnInterfacesInit(pClient);
	m_MenusStart.OnInterfacesInit(pClient);
	m_CommunityIcons.OnInterfacesInit(pClient);
}
void CMenus::PreloadSettingsTextures()
{
	// Грузим всё, что используется во вкладках Settings (Player, Tee,
	// Hydra Client → Force Skin, Trail), чтобы первый клик по вкладке
	// не фризил из-за синхронной загрузки текстур.
	//
	// RequestLoad() только ставит скин в очередь CSkins. Дальше очередь
	// обычно обрабатывается по одному скину за кадр в CSkins::OnUpdate().
	// Здесь мы «прокручиваем» её до конца синхронно — клиент загружается
	// дольше, зато после старта всё уже в памяти.

	// ---- Skins (0.6) ----
	{
		auto &SkinList = GameClient()->m_Skins.SkinList();
		if(SkinList.Skins().empty())
			SkinList.ForceRefresh();

		// Поставить все скины в очередь.
		for(auto &Entry : SkinList.Skins())
			Entry.RequestLoad();

		// Прокрутить очередь. CSkins::OnUpdate() за один вызов обрабатывает
		// ограниченное число скинов (обычно 1-2), поэтому крутим в цикле,
		// пока все SkinContainer и Skin() не будут готовы. 10000 — safety cap
		// на случай битого скина, который никогда не загрузится.
		for(int Iter = 0; Iter < 10000; ++Iter)
		{
			bool AllLoaded = true;
			for(auto &Entry : SkinList.Skins())
			{
				const auto *pC = Entry.SkinContainer();
				if(pC == nullptr || !pC->Skin())
				{
					AllLoaded = false;
					break;
				}
			}
			if(AllLoaded)
				break;
			GameClient()->m_Skins.OnUpdate();
		}
	}

	// ---- Trail dash sprites (Hydra) ----
	// GetSpriteTexture() сам подгружает PNG с диска и заливает в GPU,
	// поэтому достаточно один раз пройтись по списку.
	{
		GameClient()->m_Trails.RefreshAvailableSprites();
		for(const auto &Sprite : GameClient()->m_Trails.GetAvailableSprites())
			GameClient()->m_Trails.GetSpriteTexture(Sprite.c_str());
	}
	int LoadedCount = 0;
	int TotalCount = 0;
	for(auto &Entry : GameClient()->m_Skins.SkinList().Skins())
	{
		TotalCount++;
		if(Entry.SkinContainer() && Entry.SkinContainer()->Skin())
			LoadedCount++;
	}
	dbg_msg("menus", "PreloadSettingsTextures: %d/%d skins loaded", LoadedCount, TotalCount);
}
void CMenus::OnInit()
{
	if(m_IsInit)
		return;

	if(g_Config.m_ClShowWelcome)
	{
		m_Popup = POPUP_LANGUAGE;
		m_CreateDefaultFavoriteCommunities = true;
	}

	if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
		(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size())
	{
		// Reset page to internet when there is no favorite community for this page.
		g_Config.m_UiPage = PAGE_INTERNET;
	}

	if(g_Config.m_ClSkipStartMenu)
	{
		m_ShowStart = false;
	}

	// ui_page хранит только серверные вкладки; несерверные страницы
	// (Settings/Demos) не должны восстанавливаться из конфига, иначе
	// пользователя будет перекидывать на серверную вкладку.
	if(g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)
		m_MenuPage = g_Config.m_UiPage;
	else
		m_MenuPage = PAGE_NEWS;

	m_RefreshButton.Init(Ui(), -1);
	m_ConnectButton.Init(Ui(), -1);

	Console()->Chain("add_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("remove_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("add_friend", ConchainFriendlistUpdate, this);
	Console()->Chain("remove_friend", ConchainFriendlistUpdate, this);

	Console()->Chain("add_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_type", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_type", ConchainCommunitiesUpdate, this);

	Console()->Chain("ui_page", ConchainUiPageUpdate, this);

	Console()->Chain("snd_enable", ConchainUpdateMusicState, this);
	Console()->Chain("snd_enable_music", ConchainUpdateMusicState, this);
	Console()->Chain("cl_background_entities", ConchainBackgroundEntities, this);

	Console()->Chain("cl_assets_entities", ConchainAssetsEntities, this);
	Console()->Chain("cl_asset_game", ConchainAssetGame, this);
	Console()->Chain("cl_asset_emoticons", ConchainAssetEmoticons, this);
	Console()->Chain("cl_asset_particles", ConchainAssetParticles, this);
	Console()->Chain("cl_asset_hud", ConchainAssetHud, this);
	Console()->Chain("cl_asset_extras", ConchainAssetExtras, this);

	Console()->Chain("demo_play", ConchainDemoPlay, this);
	Console()->Chain("demo_speed", ConchainDemoSpeed, this);

	m_TextureBlob = Graphics()->LoadTexture("blob.png", IStorage::TYPE_ALL);
	CImageInfo LogoInfo;
	if(Graphics()->LoadPng(LogoInfo, "gui_logo.png", IStorage::TYPE_ALL))
	{
		m_LogoWidth = LogoInfo.m_Width;
		m_LogoHeight = LogoInfo.m_Height;
		LogoInfo.Free();
	}
	m_LogoTexture = Graphics()->LoadTexture("gui_logo.png", IStorage::TYPE_ALL);
	m_HydraLogoLoaded = false;
	{
		CImageInfo HydraInfo;
		if(Graphics()->LoadPng(HydraInfo, "hydra_logo.png", IStorage::TYPE_ALL))
		{
			m_HydraLogoAspect = HydraInfo.m_Height > 0 ? (float)HydraInfo.m_Width / (float)HydraInfo.m_Height : m_HydraLogoAspect;
			m_HydraLogoTexture = Graphics()->LoadTextureRawMove(HydraInfo, 0, "hydra_logo.png");
			m_HydraLogoLoaded = true;
		}
		else
		{
			log_error("menus", "hydra_logo.png not found or not a valid PNG, using default banner");
		}
	}
	// setup load amount
	m_LoadingState.m_Current = 0;
	m_LoadingState.m_Total = g_pData->m_NumImages + GameClient()->ComponentCount();
	if(!g_Config.m_ClThreadsoundloading)
		m_LoadingState.m_Total += g_pData->m_NumSounds;
	// ---- Предзагрузка текстур для вкладок настроек ----
	{
		auto &SkinList = GameClient()->m_Skins.SkinList();
		if(SkinList.Skins().empty())
			SkinList.ForceRefresh();

		for(auto &Entry : SkinList.Skins())
			Entry.RequestLoad();
	}

	// Hydra: спрайты для Trail (dash sprites)
	GameClient()->m_Trails.RefreshAvailableSprites();
	{
		const auto &vSprites = GameClient()->m_Trails.GetAvailableSprites();
		for(const auto &Sprite : vSprites)
			GameClient()->m_Trails.GetSpriteTexture(Sprite.c_str());
	}
	m_IsInit = true;

	// load menu images
	m_vMenuImages.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "menuimages", MenuImageScan, this);

	m_CommunityIcons.Load();

	// Quad for the direction arrows above the player
	m_DirectionQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadContainerAddSprite(m_DirectionQuadContainerIndex, 0.f, 0.f, 22.f);
	Graphics()->QuadContainerUpload(m_DirectionQuadContainerIndex);
}

void CMenus::ConchainBackgroundEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CMenus *pSelf = (CMenus *)pUserData;
		if(str_comp(g_Config.m_ClBackgroundEntities, pSelf->GameClient()->m_Background.MapName()) != 0)
			pSelf->GameClient()->m_Background.LoadBackground();
	}
}

void CMenus::ConchainUpdateMusicState(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	auto *pSelf = (CMenus *)pUserData;
	if(pResult->NumArguments())
		pSelf->UpdateMusicState();
}

void CMenus::UpdateMusicState()
{
	const bool ShouldPlay = Client()->State() == IClient::STATE_OFFLINE && g_Config.m_SndEnable && g_Config.m_SndMusic;
	if(ShouldPlay && !GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Enqueue(CSounds::CHN_MUSIC, SOUND_MENU);
	else if(!ShouldPlay && GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Stop(SOUND_MENU);
	if(!ShouldPlay)
		GameClient()->m_MapSounds.StopAll();
}

void CMenus::PopupMessage(const char *pTitle, const char *pMessage, const char *pButtonLabel, int NextPopup, FPopupButtonCallback pfnButtonCallback)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = NextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnButtonCallback;
	m_Popup = POPUP_MESSAGE;
}

void CMenus::PopupConfirm(const char *pTitle, const char *pMessage, const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
	FPopupButtonCallback pfnConfirmButtonCallback, int ConfirmNextPopup, FPopupButtonCallback pfnCancelButtonCallback, int CancelNextPopup)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pConfirmButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = ConfirmNextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnConfirmButtonCallback;
	str_copy(m_aPopupButtons[BUTTON_CANCEL].m_aLabel, pCancelButtonLabel);
	m_aPopupButtons[BUTTON_CANCEL].m_NextPopup = CancelNextPopup;
	m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback = pfnCancelButtonCallback;
	m_Popup = POPUP_CONFIRM;
}

void CMenus::PopupWarning(const char *pTopic, const char *pBody, const char *pButton, std::chrono::nanoseconds Duration)
{
	// no multiline support for console
	std::string BodyStr = pBody;
	std::replace(BodyStr.begin(), BodyStr.end(), '\n', ' ');
	log_warn("client", "%s: %s", pTopic, BodyStr.c_str());

	Ui()->SetActiveItem(nullptr);

	str_copy(m_aMessageTopic, pTopic);
	str_copy(m_aMessageBody, pBody);
	str_copy(m_aMessageButton, pButton);
	m_Popup = POPUP_WARNING;
	SetActive(true);

	m_PopupWarningDuration = Duration;
	m_PopupWarningLastTime = time_get_nanoseconds();
}

bool CMenus::CanDisplayWarning() const
{
	return m_Popup == POPUP_NONE;
}

void CMenus::Render()
{
	Ui()->MapScreen();
	Ui()->SetMouseSlow(false);

	static int s_Frame = 0;
	// Предзагрузка assets — один раз, на первом кадре Render, чтобы
	// сканирование не блокировало загрузочный экран OnInit и не дёргало
	// Client()->UpdateAndSwap() из середины init-фазы.
	if(!m_AssetsPreloaded)
	{
		m_AssetsPreloaded = true;
		PreloadAssetsTextures();
	}
	if(s_Frame == 0)
	{
		RefreshBrowserTab(true);
		s_Frame++;
	}
	// Скан папки skins/ асинхронный: SkinList.Skins() пуст (2 дефолта)
	// в момент OnInit и наполняется через 1–2 секунды. Ждём, пока число
	// скинов перестанет меняться 5 кадров подряд — это значит скан
	// закончился. Только тогда запускаем предзагрузку.
	if(!m_SkinsPreloaded)
	{
		auto &SkinList = GameClient()->m_Skins.SkinList();
		const int Count = (int)SkinList.Skins().size();
		if(Count == m_LastSkinCount)
		{
			m_SkinCountStableFrames++;
		}
		else
		{
			m_LastSkinCount = Count;
			m_SkinCountStableFrames = 0;
		}

		// >2 — скан что-то нашёл, кроме встроенных дефолтов.
		// Стабильность 5 кадров — скан закончился.
		if(Count > 2 && m_SkinCountStableFrames >= 5)
		{
			PreloadSettingsTextures();
			m_SkinsPreloaded = true;
		}
	}
	else if(s_Frame == 1)
	{
		UpdateMusicState();
		s_Frame++;
	}
	else
	{
		m_CommunityIcons.Update();
	}

	// Initially add DDNet as favorite community and select its tab.
	// This must be delayed until the DDNet info is available.
	if(m_CreateDefaultFavoriteCommunities &&
		ServerBrowser()->DDNetInfoAvailable())
	{
		m_CreateDefaultFavoriteCommunities = false;
		// Не перекидывать, если пользователь уже сам выбрал вкладку
		// (например, Настройки/Демо) до того, как пришёл DDNetInfo.
		if(m_ShowStart && ServerBrowser()->Community(IServerBrowser::COMMUNITY_DDNET) != nullptr)
		{
			ServerBrowser()->FavoriteCommunitiesFilter().Clear();
			ServerBrowser()->FavoriteCommunitiesFilter().Add(IServerBrowser::COMMUNITY_DDNET);
			SetMenuPage(PAGE_FAVORITE_COMMUNITY_1);
			SetShowStart(false);
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITE_COMMUNITY_1);
		}
	}
	if(m_JoinTutorial.m_Queued && m_Popup == POPUP_NONE)
	{
		const char *pAddr = ServerBrowser()->GetTutorialServer();
		if(pAddr)
		{
			Client()->Connect(pAddr);
		}
		else
		{
			m_Popup = POPUP_JOIN_TUTORIAL;
		}
		m_JoinTutorial.m_Queued = false;
	}

	// Determine the client state once before rendering because it can change
	// while rendering which causes frames with broken user interface.
	const IClient::EClientState ClientState = Client()->State();

	if(ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_DEMOPLAYBACK)
	{
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveIngame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveIngame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverIngame;
	}
	else
	{
		if(!GameClient()->m_MenuBackground.Render())
		{
			RenderBackground();
		}
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveOutgame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveOutgame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverOutgame;
	}

	CUIRect Screen = *Ui()->Screen();
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK || m_Popup != POPUP_NONE)
	{
		Screen.Margin(10.0f, &Screen);
	}
	auto RenderPageContent = [&](int Page, CUIRect View, bool Online)
	{
		if(!Online)
		{
			if(Page == PAGE_NEWS)
				RenderNews(View);
			else if(Page >= PAGE_INTERNET && Page <= PAGE_FAVORITE_COMMUNITY_5)
				RenderServerbrowser(View);
			else if(Page == PAGE_DEMOS)
				RenderDemoBrowser(View);
			else if(Page == PAGE_SETTINGS)
				RenderSettings(View);
		}
		else
		{
			if(Page == PAGE_GAME)
			{
				RenderGame(View);
				RenderIngameHint();
			}
			else if(Page == PAGE_PLAYERS)
				RenderPlayers(View);
			else if(Page == PAGE_SERVER_INFO)
				RenderServerInfo(View);
			else if(Page == PAGE_NETWORK)
				RenderInGameNetwork(View);
			else if(Page == PAGE_GHOST)
				RenderGhost(View);
			else if(Page == PAGE_CALLVOTE)
				RenderServerControl(View);
			else if(Page == PAGE_DEMOS)
				RenderDemoBrowser(View);
			else if(Page == PAGE_SETTINGS)
				RenderSettings(View);
		}
	};

	auto RenderAnimatedPage = [&](SPageTransition &Animation, int CurrentPage, CUIRect View, bool Online)
	{
		if(Animation.m_ToPage == -1)
		{
			Animation.m_FromPage = CurrentPage;
			Animation.m_ToPage = CurrentPage;
		}

		if(Animation.m_ToPage != CurrentPage)
		{
			Animation.m_FromPage = Animation.m_ToPage;
			Animation.m_ToPage = CurrentPage;
			Animation.m_StartTime = time();
			Animation.m_Active = true;

			// Все серверные вкладки (Internet/LAN/Favorites/Communities) рендерятся
			// одной и той же RenderServerbrowser(View), а она берёт список серверов
			// из ServerBrowser()->GetCurrentType() — глобального состояния, которое
			// SetMenuPage→RefreshBrowserTab уже переключил на новый тип ДО начала
			// анимации. Подмена m_MenuPage/g_Config.m_UiPage в DrawPage меняет
			// только фон и community filter, но не список серверов, поэтому обе
			// DrawPage рисуют один и тот же список — визуально «два одинаковых окна».
			// Пропускаем анимацию для серверных вкладок: переключение мгновенное.
			const auto IsServerPage = [](int Page) {
				return Page >= PAGE_INTERNET && Page <= PAGE_FAVORITE_COMMUNITY_5;
			};
			if(IsServerPage(Animation.m_FromPage) && IsServerPage(Animation.m_ToPage))
			{
				Animation.m_FromPage = CurrentPage;
				Animation.m_ToPage = CurrentPage;
				Animation.m_Active = false;
			}
		}

		if(!Animation.m_Active)
		{
			RenderPageContent(CurrentPage, View, Online);
			return;
		}

	    const float Duration = g_Config.m_ClSettingsTabAnimationDuration / 1000.0f;
		float Progress = (time() - Animation.m_StartTime) / (float)time_freq() / Duration;
		Progress = std::clamp(Progress, 0.0f, 1.0f);

		if(Progress >= 1.0f)
		{
			Animation.m_Active = false;
		}

	    const float Eased = EaseTabSlide(Progress, g_Config.m_ClSettingsTabAnimationEasing);
		const float Direction = Animation.m_ToPage > Animation.m_FromPage ? 1.0f : -1.0f;

		auto DrawPage = [&](int Page, CUIRect PageView, bool Interactive)
		{
			const float X0 = std::max(View.x, PageView.x);
			const float X1 = std::min(View.x + View.w, PageView.x + PageView.w);

			if(X1 <= X0)
				return;

			CUIRect Clip = View;
			Clip.x = X0;
			Clip.w = X1 - X0;

			// Browser pages (Internet/LAN/Favorites/Communities) are all rendered by the
			// same RenderServerbrowser(View), which decides which tab to show by reading
			// m_MenuPage / g_Config.m_UiPage. During the slide animation we call it for
			// both the old and new page, but if we don't swap those globals, both calls
			// see the *new* page and the animation shows the same page twice. Temporarily
			// set them to the page we are about to draw, then restore.
			const int SavedMenuPage = m_MenuPage;
			const int SavedUiPage = g_Config.m_UiPage;
			m_MenuPage = Page;
			if(Page >= PAGE_INTERNET && Page <= PAGE_FAVORITE_COMMUNITY_5)
				g_Config.m_UiPage = Page;

			Ui()->ClipEnable(&Clip);
			Ui()->SetEnabled(Interactive);
			RenderPageContent(Page, PageView, Online);
			Ui()->SetEnabled(true);
			Ui()->ClipDisable();

			m_MenuPage = SavedMenuPage;
			g_Config.m_UiPage = SavedUiPage;
		};

		CUIRect OldView = View;
		OldView.x -= Direction * Eased * View.w;
		DrawPage(Animation.m_FromPage, OldView, false);

		CUIRect NewView = View;
		NewView.x += Direction * (1.0f - Eased) * View.w;
		DrawPage(Animation.m_ToPage, NewView, true);
	};
	const int CurrentPage = ClientState == IClient::STATE_ONLINE ? m_GamePage : m_MenuPage;
	if(m_AnimatedPage != CurrentPage)
	{
		m_AnimatedPage = CurrentPage;
		m_PageAnimationStart = time_get();
	}
	const float PageAnimationProgress = std::clamp((time_get() - m_PageAnimationStart) / (float)(time_freq() * 0.24), 0.0f, 1.0f);
	const float PageAnimationEase = 1.0f - std::pow(1.0f - PageAnimationProgress, 3.0f);
	const float PageAnimationOffset = (1.0f - PageAnimationEase) * 42.0f;

	switch(ClientState)
	{
	case IClient::STATE_QUITTING:
	case IClient::STATE_RESTARTING:
		// Render nothing except menu background. This should not happen for more than one frame.
		return;

	case IClient::STATE_CONNECTING:
		RenderPopupConnecting(Screen);
		break;

	case IClient::STATE_LOADING:
		RenderPopupLoading(Screen);
		break;

	case IClient::STATE_OFFLINE:
		if(m_Popup != POPUP_NONE)
		{
			RenderPopupFullscreen(Screen);
		}
		else if(m_StartMenuTransition.m_Active)
		{
			const float Duration = g_Config.m_ClStartMenuAnimationDuration / 1000.0f;
			float Progress = (time() - m_StartMenuTransition.m_StartTime) / (float)time_freq() / Duration;
			Progress = std::clamp(Progress, 0.0f, 1.0f);

		    const float Eased = EaseTabSlide(Progress, g_Config.m_ClStartMenuAnimationEasing);

			CUIRect OldScreen = Screen;
			CUIRect NewScreen = Screen;

			if(!m_StartMenuTransition.m_Reverse)
			{
				OldScreen.y -= Eased * Screen.h;
				NewScreen.y += (1.0f - Eased) * Screen.h;
			}
			else
			{
				OldScreen.y += Eased * Screen.h;
				NewScreen.y -= (1.0f - Eased) * Screen.h;
			}

		Ui()->ClipEnable(&Screen);

		if(!m_StartMenuTransition.m_Reverse)
		{
			Ui()->SetEnabled(false);
			m_MenusStart.RenderStartMenu(OldScreen);
			Ui()->SetEnabled(true);

			CUIRect TabBar, MainView;
			NewScreen.HSplitTop(24.0f, &TabBar, &MainView);

			const int TargetPage = m_StartMenuTransition.m_TargetPage;

			if(TargetPage >= PAGE_INTERNET && TargetPage <= PAGE_FAVORITE_COMMUNITY_5)
				RenderServerbrowser(MainView);
			else if(TargetPage == PAGE_DEMOS)
				RenderDemoBrowser(MainView);
			else if(TargetPage == PAGE_SETTINGS)
				RenderSettings(MainView);
		}
		else
		{
			Ui()->SetEnabled(false);
			m_MenusStart.RenderStartMenu(NewScreen);
			Ui()->SetEnabled(true);

			CUIRect TabBar, MainView;
			OldScreen.HSplitTop(24.0f, &TabBar, &MainView);

			const int TargetPage = m_StartMenuTransition.m_TargetPage;

			if(TargetPage >= PAGE_INTERNET && TargetPage <= PAGE_FAVORITE_COMMUNITY_5)
				RenderServerbrowser(MainView);
			else if(TargetPage == PAGE_DEMOS)
				RenderDemoBrowser(MainView);
			else if(TargetPage == PAGE_SETTINGS)
				RenderSettings(MainView);
		}

		Ui()->ClipDisable();

			if(Progress >= 1.0f)
			{
				m_StartMenuTransition.m_Active = false;

				if(m_StartMenuTransition.m_Reverse)
				{
					m_ShowStart = true;
					m_StartMenuTransition.m_Reverse = false;
				}

				m_StartMenuTransition.m_TargetPage = -1;
			}
		}
		else if(m_ShowStart)
		{
			const bool PreloadsDone = m_AssetsPreloaded && m_SkinsPreloaded;
			constexpr float Total = 0.8f + 0.55f + 0.4f + 1.0f;

			if(!PreloadsDone && !m_StartMenuIntro.m_Played)
			{
				// Preload идёт — чёрный экран (без мелькания меню)
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
				Graphics()->SetColor(0.0f, 0.0f, 0.0f, 1.0f);
				IGraphics::CQuadItem Q(0.0f, 0.0f, Ui()->Screen()->w, Ui()->Screen()->h);
				Graphics()->QuadsDrawTL(&Q, 1);
				Graphics()->QuadsEnd();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
			else if(!m_StartMenuIntro.m_Played)
			{
				m_StartMenuIntro.m_Active = true;
				m_StartMenuIntro.m_Played = true;
				m_StartMenuIntro.m_StartTime = time();
				RenderStartMenuIntro(Screen, 0.0f);
			}
			else if(m_StartMenuIntro.m_Active)
			{
				const float T = (time() - m_StartMenuIntro.m_StartTime) / (float)time_freq();
				if(T >= Total)
				{
					m_StartMenuIntro.m_Active = false;
					m_HideStartMenuLogo = false;
					m_MenusStart.RenderStartMenu(Screen);
				}
				else
				{
					RenderStartMenuIntro(Screen, T);
				}
			}
			else
			{
				m_MenusStart.RenderStartMenu(Screen);
			}
		}
		else
		{
			CUIRect TabBar, MainView;
			Screen.HSplitTop(24.0f, &TabBar, &MainView);

			const CUIRect PageClip = MainView;
			Ui()->ClipEnable(&PageClip);
			MainView.x += PageAnimationOffset;

			RenderAnimatedPage(m_MenuPageTransition, m_MenuPage, MainView, false);

			Ui()->ClipDisable();
			RenderMenubar(TabBar, ClientState);
		}
		break;

	case IClient::STATE_ONLINE:
		if(m_Popup != POPUP_NONE)
		{
			RenderPopupFullscreen(Screen);
		}
		else if(m_MenuActive)
		{
			CUIRect TabBar, MainView;
			Screen.HSplitTop(24.0f, &TabBar, &MainView);

			const CUIRect PageClip = MainView;
			Ui()->ClipEnable(&PageClip);
			MainView.x += PageAnimationOffset;

			RenderAnimatedPage(m_GamePageTransition, m_GamePage, MainView, true);

			Ui()->ClipDisable();
			RenderMenubar(TabBar, ClientState);
			RenderIngameHint();
		}
		break;

	case IClient::STATE_DEMOPLAYBACK:
		if(m_Popup != POPUP_NONE)
		{
			RenderPopupFullscreen(Screen);
		}
		else
		{
			RenderDemoPlayer(Screen);
		}
		break;
	}

	Ui()->RenderPopupMenus();

	// Prevent UI elements from being hovered while a key reader is active
	if(GameClient()->m_KeyBinder.IsActive())
	{
		Ui()->SetHotItem(nullptr);
	}

	// Handle this escape hotkey after popup menus
	if(!m_ShowStart && !m_StartMenuTransition.m_Active &&
		!m_MenuPageTransition.m_Active &&
		ClientState == IClient::STATE_OFFLINE &&
		Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Ui()->SetActiveItem(nullptr);

		if(g_Config.m_ClStartMenuAnimation &&
			((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) ||
				m_MenuPage == PAGE_DEMOS ||
				m_MenuPage == PAGE_SETTINGS))
		{
			m_StartMenuTransition.m_Active = true;
			m_StartMenuTransition.m_Reverse = true;
			m_StartMenuTransition.m_TargetPage = m_MenuPage;
			m_StartMenuTransition.m_StartTime = time();
		}
		else
		{
			m_ShowStart = true;
		}
	}
}
void CMenus::RenderPopupFullscreen(CUIRect Screen)
{
	char aBuf[1536];
	const char *pTitle = "";
	const char *pExtraText = "";
	const char *pButtonText = "";
	bool TopAlign = false;

	ColorRGBA BgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		pTitle = m_aPopupTitle;
		pExtraText = m_aPopupMessage;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_DISCONNECTED)
	{
		pTitle = Localize("Disconnected");
		pExtraText = Client()->ErrorString();
		pButtonText = Localize("Ok");
		if(Client()->ReconnectTime() > 0)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Reconnect in %d sec"), (int)((Client()->ReconnectTime() - time_get()) / time_freq()) + 1);
			pTitle = Client()->ErrorString();
			pExtraText = aBuf;
			pButtonText = Localize("Abort");
		}
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		dbg_assert(m_DemolistSelectedIndex >= 0, "m_DemolistSelectedIndex invalid for POPUP_RENAME_DEMO");
		pTitle = m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir ? Localize("Rename folder") : Localize("Rename demo");
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		pTitle = Localize("Render demo");
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		pTitle = Localize("Render complete");
	}
#endif
	else if(m_Popup == POPUP_PASSWORD)
	{
		pTitle = Localize("Password incorrect");
		pButtonText = Localize("Try again");
	}
	else if(m_Popup == POPUP_RESTART)
	{
		pTitle = Localize("Restart");
		pExtraText = Localize("Are you sure that you want to restart?");
	}
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		pTitle = Localize("Welcome to DDNet");
		str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s",
			Localize("DDraceNetwork is a cooperative online game where the goal is for you and your group of tees to reach the finish line of the map. As a newcomer you should start on Novice servers, which host the easiest maps. Consider the ping to choose a server close to you."),
			Localize("Use k key to kill (restart), q to pause and watch other players. See settings for other key binds."),
			Localize("It's recommended that you check the settings to adjust them to your liking before joining a server."),
			Localize("Please enter your nickname below."));
		pExtraText = aBuf;
		pButtonText = Localize("Ok");
		TopAlign = true;
	}
	else if(m_Popup == POPUP_POINTS)
	{
		pTitle = Localize("Existing Player");
		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Your nickname '%s' is already used (%d points). Do you still want to use it?"), Client()->PlayerName(), Client()->Points());
			pExtraText = aBuf;
			TopAlign = true;
		}
		else
		{
			pExtraText = Localize("Checking for existing player with your name");
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		BgColor = ColorRGBA(0.5f, 0.0f, 0.0f, 0.7f);
		pTitle = m_aMessageTopic;
		pExtraText = m_aMessageBody;
		pButtonText = m_aMessageButton;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		pTitle = Localize("Save skin");
		pExtraText = Localize("Are you sure you want to save your skin? If a skin with this name already exists, it will be replaced.");
	}
	else if(m_Popup == POPUP_NOTIFICATIONS)
	{
		pTitle = Localize("Notifications");
	}

	CUIRect Box, Part;
	Box = Screen;
	if(m_Popup == POPUP_NOTIFICATIONS)
	{
		Box.Margin(50.0f, &Box);
	}
	else if(m_Popup != POPUP_FIRST_LAUNCH)
	{
		Box.Margin(150.0f, &Box);
	}

	// Background
	Box.Draw(BgColor, IGraphics::CORNER_ALL, 15.0f);

	// Title
	{
		CUIRect Title;
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &Title, &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Title.VMargin(20.0f, &Title);

		const float TitleFontSize = 24.0f;
		if(TextRender()->TextWidth(TitleFontSize, pTitle) > Title.w)
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_ML, {.m_MaxWidth = Title.w});
		else
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_MC);
	}

	// Extra text (optional)
	if(m_Popup != POPUP_JOIN_TUTORIAL)
	{
		CUIRect ExtraText;
		Box.HSplitTop(24.0f, &ExtraText, &Box);
		ExtraText.VMargin(20.0f, &ExtraText);
		if(pExtraText[0] != '\0')
		{
			const float ExtraTextFontSize = m_Popup == POPUP_FIRST_LAUNCH ? 16.0f : 20.0f;

			if(TopAlign)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_TL, {.m_MaxWidth = ExtraText.w});
			else if(TextRender()->TextWidth(ExtraTextFontSize, pExtraText) > ExtraText.w)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_ML, {.m_MaxWidth = ExtraText.w});
			else
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_MC);
		}
	}

	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		CUIRect ButtonBar;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(100.0f, &ButtonBar);

		if(m_Popup == POPUP_MESSAGE)
		{
			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ButtonBar) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
		else if(m_Popup == POPUP_CONFIRM)
		{
			CUIRect CancelButton, ConfirmButton;
			ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, 40.0f);

			static CButtonContainer s_ButtonCancel;
			if(DoButton_Menu(&s_ButtonCancel, m_aPopupButtons[BUTTON_CANCEL].m_aLabel, 0, &CancelButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = m_aPopupButtons[BUTTON_CANCEL].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback)();
			}

			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ConfirmButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
	}
	else if(m_Popup == POPUP_RESTART)
	{
		CUIRect Yes, No;
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		// additional info
		Box.VMargin(20.f, &Box);
		if(GameClient()->Editor()->HasUnsavedData())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved map in the editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}
		else if(GameClient()->m_TouchControls.HasEditingChanges() || m_MenusIngameTouchControls.UnsavedChanges())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved change in the touch controls editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}

		// buttons
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&No, &Yes);
		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Yes"), 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_RESTART)
			{
				m_Popup = POPUP_NONE;
				Client()->Restart();
			}
			else
			{
				m_Popup = POPUP_NONE;
				Client()->Quit();
			}
		}
	}
	else if(m_Popup == POPUP_PASSWORD)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(100.0f, &Part);

		CUIRect TryAgain, Abort;
		Part.VSplitMid(&Abort, &TryAgain, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_Popup = POPUP_NONE;
		}

		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Try again"), 0, &TryAgain) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->Connect(aAddr, g_Config.m_Password);
		}

		Box.VMargin(60.0f, &Box);
		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Label, TextBox;
		Part.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Password"), 18.0f, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&m_PasswordInput, &TextBox, 12.0f);

		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Address;
		Part.VSplitLeft(100.0f, &Label, &Address);
		Address.VSplitLeft(20.0f, nullptr, &Address);
		Ui()->DoLabel(&Label, Localize("Address"), 18.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Address, aAddr, 18.0f, TEXTALIGN_ML);

		const CServerBrowser::CServerEntry *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
		if(pEntry != nullptr && pEntry->m_GotInfo)
		{
			const CCommunity *pCommunity = ServerBrowser()->Community(pEntry->m_Info.m_aCommunityId);
			const CCommunityIcon *pIcon = pCommunity == nullptr ? nullptr : m_CommunityIcons.Find(pCommunity->Id());

			Box.HSplitBottom(32.0f, &Box, nullptr);
			Box.HSplitBottom(24.0f, &Box, &Part);

			CUIRect Name;
			Part.VSplitLeft(100.0f, &Label, &Name);
			Name.VSplitLeft(20.0f, nullptr, &Name);
			if(pIcon != nullptr)
			{
				CUIRect Icon;
				static char s_CommunityTooltipButtonId;
				Name.VSplitLeft(2.5f * Name.h, &Icon, &Name);
				m_CommunityIcons.Render(pIcon, Icon, true);
				Ui()->DoButtonLogic(&s_CommunityTooltipButtonId, 0, &Icon, BUTTONFLAG_NONE);
				Ui()->DoToolTip(&s_CommunityTooltipButtonId, &Icon, pCommunity->Name());
			}

			Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
			Ui()->DoLabel(&Name, pEntry->m_Info.m_aName, 18.0f, TEXTALIGN_ML);
		}
	}
	else if(m_Popup == POPUP_LANGUAGE)
	{
		CUIRect Button;
		Screen.Margin(150.0f, &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Button);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.VMargin(20.0f, &Box);
		const bool Activated = RenderLanguageSelection(Box);
		Button.VMargin(120.0f, &Button);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Ok"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Activated)
			m_Popup = POPUP_FIRST_LAUNCH;
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		CUIRect Label, TextBox, Ok, Abort;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&Abort, &Ok);

		Ok.VMargin(20.0f, &Ok);
		Abort.VMargin(20.0f, &Abort);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// rename demo
			char aBufOld[IO_MAX_PATH_LENGTH];
			str_format(aBufOld, sizeof(aBufOld), "%s/%s", m_aCurrentDemoFolder, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aFilename);
			char aBufNew[IO_MAX_PATH_LENGTH];
			str_format(aBufNew, sizeof(aBufNew), "%s/%s", m_aCurrentDemoFolder, m_DemoRenameInput.GetString());
			if(!m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir && !str_endswith(aBufNew, ".demo"))
				str_append(aBufNew, ".demo");

			if(str_comp(aBufOld, aBufNew) == 0)
			{
				// Nothing to rename, also same capitalization
			}
			else if(!str_valid_filename(m_DemoRenameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(str_utf8_comp_nocase(aBufOld, aBufNew) != 0 && // Allow renaming if it only changes capitalization to support case-insensitive filesystems
				Storage()->FileExists(aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				PopupMessage(Localize("Error"), Localize("A demo with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->FolderExists(aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->RenameFile(aBufOld, aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				str_copy(m_aCurrentDemoSelectionName, m_DemoRenameInput.GetString());
				if(!m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir)
					fs_split_file_extension(m_DemoRenameInput.GetString(), m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName));
				DemolistPopulate();
				DemolistOnUpdate(false);
			}
			else
			{
				PopupMessage(Localize("Error"), m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir ? Localize("Unable to rename the folder") : Localize("Unable to rename the demo"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(120.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("New name:"), 18.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_DemoRenameInput, &TextBox, 12.0f);
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		CUIRect Row, Ok, Abort;
		Box.VMargin(60.0f, &Box);
		Box.HMargin(20.0f, &Box);
		Box.HSplitBottom(24.0f, &Box, &Row);
		Box.HSplitBottom(40.0f, &Box, nullptr);
		Row.VMargin(40.0f, &Row);
		Row.VSplitMid(&Abort, &Ok, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_DemoRenderInput.Clear();
			m_Popup = POPUP_NONE;
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// render video
			char aVideoPath[IO_MAX_PATH_LENGTH];
			str_format(aVideoPath, sizeof(aVideoPath), "videos/%s", m_DemoRenderInput.GetString());
			if(!str_endswith(aVideoPath, ".mp4"))
				str_append(aVideoPath, ".mp4");

			if(!str_valid_filename(m_DemoRenderInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FolderExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FileExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				char aMessage[128 + IO_MAX_PATH_LENGTH];
				str_format(aMessage, sizeof(aMessage), Localize("File '%s' already exists, do you want to overwrite it?"), m_DemoRenderInput.GetString());
				PopupConfirm(Localize("Replace video"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDemoReplaceVideo, POPUP_NONE, &CMenus::DefaultButtonCallback, POPUP_RENDER_DEMO);
			}
			else
			{
				PopupConfirmDemoReplaceVideo();
			}
		}

		CUIRect ShowChatCheckbox, UseSoundsCheckbox;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Box.HSplitBottom(10.0f, &Box, nullptr);
		Row.VSplitMid(&ShowChatCheckbox, &UseSoundsCheckbox, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowChat, Localize("Show chat"), g_Config.m_ClVideoShowChat, &ShowChatCheckbox))
			g_Config.m_ClVideoShowChat ^= 1;

		if(DoButton_CheckBox(&g_Config.m_ClVideoSndEnable, Localize("Use sounds"), g_Config.m_ClVideoSndEnable, &UseSoundsCheckbox))
			g_Config.m_ClVideoSndEnable ^= 1;

		CUIRect ShowHudButton;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Row.VSplitMid(&Row, &ShowHudButton, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowhud, Localize("Show ingame HUD"), g_Config.m_ClVideoShowhud, &ShowHudButton))
			g_Config.m_ClVideoShowhud ^= 1;

		// slowdown
		CUIRect SlowDownButton;
		Row.VSplitLeft(20.0f, &SlowDownButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_SlowDownButton;
		if(Ui()->DoButton_FontIcon(&s_SlowDownButton, FontIcon::BACKWARD, 0, &SlowDownButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed - 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// paused
		CUIRect PausedButton;
		Row.VSplitLeft(20.0f, &PausedButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_PausedButton;
		if(Ui()->DoButton_FontIcon(&s_PausedButton, FontIcon::PAUSE, 0, &PausedButton, BUTTONFLAG_LEFT))
			m_StartPaused ^= 1;

		// fastforward
		CUIRect FastForwardButton;
		Row.VSplitLeft(20.0f, &FastForwardButton, &Row);
		Row.VSplitLeft(8.0f, nullptr, &Row);
		static CButtonContainer s_FastForwardButton;
		if(Ui()->DoButton_FontIcon(&s_FastForwardButton, FontIcon::FORWARD, 0, &FastForwardButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed + 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// speed meter
		char aBuffer[128];
		const char *pPaused = m_StartPaused ? Localize("(paused)") : "";
		str_format(aBuffer, sizeof(aBuffer), "%s: ×%g %s", Localize("Speed"), DEMO_SPEEDS[m_Speed], pPaused);
		Ui()->DoLabel(&Row, aBuffer, 12.8f, TEXTALIGN_ML);
		Box.HSplitBottom(16.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Row);

		CUIRect Label, TextBox;
		Row.VSplitLeft(110.0f, &Label, &TextBox);
		TextBox.VSplitLeft(10.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Video name:"), 12.8f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_DemoRenderInput, &TextBox, 12.8f);

		// Warn about disconnect if online
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			Box.HSplitBottom(10.0f, &Box, nullptr);
			Box.HSplitBottom(20.0f, &Box, &Row);
			SLabelProperties LabelProperties;
			LabelProperties.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			Ui()->DoLabel(&Row, Localize("You will be disconnected from the server."), 12.8f, TEXTALIGN_MC, LabelProperties);
		}
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		CUIRect Ok, OpenFolder;

		char aFilePath[IO_MAX_PATH_LENGTH];
		char aSaveFolder[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "videos", aSaveFolder, sizeof(aSaveFolder));
		str_format(aFilePath, sizeof(aFilePath), "%s/%s.mp4", aSaveFolder, m_DemoRenderInput.GetString());

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&OpenFolder, &Ok);

		Ok.VMargin(20.0f, &Ok);
		OpenFolder.VMargin(20.0f, &OpenFolder);

		static CButtonContainer s_ButtonOpenFolder;
		if(DoButton_Menu(&s_ButtonOpenFolder, Localize("Videos directory"), 0, &OpenFolder))
		{
			Client()->ViewFile(aSaveFolder);
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			m_DemoRenderInput.Clear();
		}

		Box.HSplitBottom(160.f, &Box, &Part);
		Part.VMargin(20.0f, &Part);

		str_format(aBuf, sizeof(aBuf), Localize("Video was saved to '%s'"), aFilePath);

		SLabelProperties MessageProps;
		MessageProps.m_MaxWidth = (int)Part.w;
		Ui()->DoLabel(&Part, aBuf, 18.0f, TEXTALIGN_TL, MessageProps);
	}
#endif
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		CUIRect Label, TextBox, Skip, Join;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&Skip, &Join);
		Skip.VMargin(20.0f, &Skip);
		Join.VMargin(20.0f, &Join);

		static CButtonContainer s_JoinTutorialButton;
		if(DoButton_Menu(&s_JoinTutorialButton, Localize("Join Tutorial Server"), 0, &Join) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->RequestDDNetInfo();
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
			JoinTutorial();
		}

		static CButtonContainer s_SkipTutorialButton;
		if(DoButton_Menu(&s_SkipTutorialButton, Localize("Skip Tutorial"), 0, &Skip) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			Client()->RequestDDNetInfo();
			m_JoinTutorial.m_Queued = false;
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
		}

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(30.0f, nullptr, &Part);
		str_format(aBuf, sizeof(aBuf), "%s\n(%s)",
			Localize("Show DDNet map finishes in server browser"),
			Localize("transmits your player name to info.ddnet.org"));

		if(DoButton_CheckBox(&g_Config.m_BrIndicateFinished, aBuf, g_Config.m_BrIndicateFinished, &Part))
			g_Config.m_BrIndicateFinished ^= 1;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(100.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("Nickname"), 16.0f, TEXTALIGN_ML);
		static CLineInput s_PlayerNameInput(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_PlayerNameInput.SetEmptyText(Client()->PlayerName());
		Ui()->DoEditBox(&s_PlayerNameInput, &TextBox, 12.0f);
	}
	else if(m_Popup == POPUP_JOIN_TUTORIAL)
	{
		CUIRect ButtonBar, StatusLabel, ProgressLabel, ProgressIndicator;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(120.0f, &ButtonBar);
		Box.HSplitBottom(20.0f, &StatusLabel, nullptr);
		StatusLabel.VMargin(20.0f, &StatusLabel);
		StatusLabel.HSplitMid(&StatusLabel, &ProgressLabel);
		ProgressLabel.VSplitLeft(50.0f, &ProgressIndicator, &ProgressLabel);

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			if(ServerBrowser()->IsGettingServerlist() ||
				Client()->InfoState() == IClient::EInfoState::LOADING)
			{
				// Still refreshing
			}
			else if(ServerBrowser()->IsServerlistError() ||
				Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::SERVER_LIST_ERROR;
			}
			else
			{
				const char *pAddr = ServerBrowser()->GetTutorialServer();
				if(pAddr)
				{
					Client()->Connect(pAddr);
				}
				else
				{
					m_JoinTutorial.m_Status = CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE;
				}
			}
		}

		const char *pStatusLabel = nullptr;
		switch(m_JoinTutorial.m_Status)
		{
		case CJoinTutorial::EStatus::REFRESHING:
			pStatusLabel = Localize("Getting server list from master server");
			break;
		case CJoinTutorial::EStatus::SERVER_LIST_ERROR:
			pStatusLabel = Localize("Could not get server list from master server");
			break;
		case CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE:
			pStatusLabel = Localize("There are no Tutorial servers available");
			break;
		}
		if(pStatusLabel != nullptr)
		{
			Ui()->DoLabel(&StatusLabel, pStatusLabel, 20.0f, TEXTALIGN_ML);
		}

		const char *pProgressLabel = nullptr;
		bool ProgressDeterminate = true;
		const float LastStateChangeSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(time_get_nanoseconds() - m_JoinTutorial.m_StateChange).count();
		constexpr float RefreshDelay = 5.0f;

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			pProgressLabel = Localize("Please wait…");
			ProgressDeterminate = false;
		}
		else if(!m_JoinTutorial.m_TryRefresh)
		{
			if(!m_JoinTutorial.m_TriedRefresh)
			{
				m_JoinTutorial.m_TryRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::NOT_TRIED)
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::TRY;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
		}

		if(m_JoinTutorial.m_TryRefresh)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				// Activate internet tab before joining tutorial to make sure the server info
				// for the tutorial servers is available.
				GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_INTERNET);
				GameClient()->m_Menus.RefreshBrowserTab(true);
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
				m_JoinTutorial.m_TryRefresh = false;
				m_JoinTutorial.m_TriedRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				pProgressLabel = Localize("Retrying…");
			}
		}

		const auto &&ShowFinalErrorMessage = [&]() {
			PopupMessage(Localize("Error joining Tutorial server"), Localize("Could not find a Tutorial server. Check your internet connection."), Localize("Ok"));
		};
		const auto &&RunServer = [&]() {
			char aMotd[256];
			str_copy(aMotd, "sv_motd \"");
			char *pDst = aMotd + str_length(aMotd);
			str_escape(&pDst, Localize("You're playing on a local server because no online Tutorial server could be found.\n\nYour record will only be saved locally."), aMotd + sizeof(aMotd) - 1);
			str_append(aMotd, "\"");
			if(GameClient()->m_LocalServer.RunServer({"sv_register 0", "sv_map Tutorial", aMotd}))
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_START;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				ShowFinalErrorMessage();
			}
		};
		if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::TRY)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				if(GameClient()->m_LocalServer.IsServerRunning())
				{
					GameClient()->m_LocalServer.KillServer();
					m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_STOP;
					m_JoinTutorial.m_StateChange = time_get_nanoseconds();
				}
				else
				{
					RunServer();
				}
			}
			else
			{
				pProgressLabel = Localize("Could not find online Tutorial server.\nStarting and connecting to local server…");
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_STOP)
		{
			if(LastStateChangeSeconds >= 5.0f)
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(!GameClient()->m_LocalServer.IsServerRunning())
				{
					RunServer();
				}

				pProgressLabel = Localize("Waiting for local server to stop…");
				ProgressDeterminate = false;
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_START)
		{
			if(LastStateChangeSeconds >= 5.0f && !GameClient()->m_LocalServer.IsStarting())
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(LastStateChangeSeconds >= 2.0f &&
					GameClient()->m_LocalServer.IsServerRunning())
				{
					GameClient()->m_LocalServer.Connect();
				}

				pProgressLabel = Localize("Waiting for local server to start…");
				ProgressDeterminate = false;
			}
		}

		if(pProgressLabel != nullptr)
		{
			Ui()->RenderProgressSpinner(ProgressIndicator.Center(), 12.0f, {.m_Progress = ProgressDeterminate ? (LastStateChangeSeconds / RefreshDelay) : -1.0f});
			Ui()->DoLabel(&ProgressLabel, pProgressLabel, 20.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &ButtonBar) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			GameClient()->m_LocalServer.CancelConnect();
			m_Popup = POPUP_NONE;
		}
	}
	else if(m_Popup == POPUP_POINTS)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			CUIRect Yes, No;
			Part.VSplitMid(&No, &Yes, 40.0f);
			static CButtonContainer s_ButtonNo;
			if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = POPUP_FIRST_LAUNCH;
			}

			static CButtonContainer s_ButtonYes;
			if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), 0, &Yes) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = POPUP_NONE;
			}
		}
		else
		{
			static CButtonContainer s_Button;
			if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Part) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) ||
				Client()->InfoState() == IClient::EInfoState::SUCCESS)
			{
				m_Popup = POPUP_NONE;
			}
			if(Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				PopupMessage(Localize("Error checking player name"), Localize("Could not check for existing player with your name. Check your internet connection."), Localize("Ok"));
			}
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || (m_PopupWarningDuration > 0s && time_get_nanoseconds() - m_PopupWarningLastTime >= m_PopupWarningDuration))
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		CUIRect Label, TextBox, Yes, No;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&No, &Yes);

		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonNo;
		if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonYes;
		if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), m_SkinNameInput.IsEmpty() ? 1 : 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(!str_valid_filename(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(CSkins7::IsSpecialSkin(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin with a reserved name"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(!GameClient()->m_Skins7.SaveSkinfile(m_SkinNameInput.GetString(), m_Dummy))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else
			{
				m_Popup = POPUP_NONE;
				m_SkinList7LastRefreshTime = std::nullopt;
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VMargin(60.0f, &Label);
		Label.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&m_SkinNameInput, &TextBox, 12.0f);
	}
	else if(m_Popup == POPUP_NOTIFICATIONS)
	{
		CEventsManager &Events = GameClient()->m_EventsManager;

		CUIRect ButtonBar, CloseButton, MarkAllButton, List, Details;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		Box.HSplitBottom(10.0f, &Box, nullptr);
		ButtonBar.VMargin(100.0f, &ButtonBar);
		ButtonBar.VSplitMid(&CloseButton, &MarkAllButton, 40.0f);

		static CButtonContainer s_CloseButton;
		if(DoButton_Menu(&s_CloseButton, Localize("Close"), 0, &CloseButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
		}

		static CButtonContainer s_MarkAllButton;
		if(DoButton_Menu(&s_MarkAllButton, Localize("Mark all as read"), 0, &MarkAllButton))
		{
			Events.MarkAllRead();
		}

		Box.VMargin(20.0f, &Box);
		Box.VSplitLeft(Box.w * 0.35f, &List, &Details);
		Details.VSplitLeft(15.0f, nullptr, &Details);

		const int NumItems = (int)Events.m_vNotifications.size();
		if(m_NotificationSelected >= NumItems)
			m_NotificationSelected = -1;

		if(NumItems == 0)
		{
			Ui()->DoLabel(&Box, Events.HasLoaded() ? Localize("No notifications") : Localize("Loading…"), 16.0f, TEXTALIGN_MC);
		}
		else
		{
			const float ItemHeight = 28.0f;
			const float ItemSpacing = 4.0f;
			static CScrollRegion s_ScrollRegion;
			static CButtonContainer s_aItemButtons[128];

			CScrollRegionParams ScrollParams;
			ScrollParams.m_ScrollbarThickness = 12.0f;
			ScrollParams.m_ScrollbarMargin = 2.0f;
			ScrollParams.m_ScrollUnit = 3.0f * (ItemHeight + ItemSpacing);
			s_ScrollRegion.Begin(&List, &ScrollParams);

			const int NumShown = std::min(NumItems, (int)std::size(s_aItemButtons));
			for(int i = 0; i < NumShown; i++)
			{
				CUIRect Item;
				List.HSplitTop(ItemHeight, &Item, &List);
				List.HSplitTop(ItemSpacing, nullptr, &List);

				// Не рисуем и не обрабатываем клики для элементов вне видимой области
				if(!s_ScrollRegion.AddRect(Item))
					continue;

				const CEventNotification &Notification = Events.m_vNotifications[i];
				char aLabel[300];
				str_format(aLabel, sizeof(aLabel), "%s%s", Notification.m_Read ? "" : "* ", Notification.m_aTitle);
				const ColorRGBA ItemColor = Notification.m_Read ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f) : ColorRGBA(1.0f, 0.8f, 0.2f, 0.45f);
				if(DoButton_Menu(&s_aItemButtons[i], aLabel, i == m_NotificationSelected, &Item, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ItemColor))
				{
					m_NotificationSelected = i;
					Events.MarkRead(Notification.m_Id);
				}
			}

			s_ScrollRegion.End();

			if(m_NotificationSelected >= 0)
			{
				const CEventNotification &Selected = Events.m_vNotifications[m_NotificationSelected];
				const char *pType = Localize("News");
				if(Selected.m_Type == CEventNotification::TYPE_UPDATE)
					pType = Localize("Update");
				else if(Selected.m_Type == CEventNotification::TYPE_EVENT)
					pType = Localize("Event");

				CUIRect TypeRow, TitleRow;
				Details.HSplitTop(20.0f, &TypeRow, &Details);
				Ui()->DoLabel(&TypeRow, pType, 14.0f, TEXTALIGN_ML);
				Details.HSplitTop(34.0f, &TitleRow, &Details);
				Ui()->DoLabel(&TitleRow, Selected.m_aTitle, 22.0f, TEXTALIGN_ML, {.m_MaxWidth = TitleRow.w});

				static CScrollRegion s_DetailsScroll;
				CScrollRegionParams DetailsParams;
				DetailsParams.m_ScrollbarThickness = 12.0f;
				DetailsParams.m_ScrollbarMargin = 2.0f;
				s_DetailsScroll.Begin(&Details, &DetailsParams);

				const float TextHeight = std::max(Details.h, TextRender()->TextBoundingBox(16.0f, Selected.m_aText, -1, Details.w - 4.0f).m_H + 8.0f);
				CUIRect TextRect;
				Details.HSplitTop(TextHeight, &TextRect, &Details);
				s_DetailsScroll.AddRect(TextRect);
				Ui()->DoLabel(&TextRect, Selected.m_aText, 16.0f, TEXTALIGN_TL, {.m_MaxWidth = TextRect.w});

				s_DetailsScroll.End();
			}
			else
			{
				Ui()->DoLabel(&Details, Localize("Select a notification"), 16.0f, TEXTALIGN_MC);
			}
		}
	}
	else
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_DISCONNECTED && Client()->ReconnectTime() > 0)
				Client()->SetReconnectTime(0);
			m_Popup = POPUP_NONE;
			RefreshBrowserTab(true);
		}
	}

	if(m_Popup == POPUP_NONE)
		Ui()->SetActiveItem(nullptr);
}

void CMenus::RenderPopupConnecting(CUIRect Screen)
{
	CUIRect Box;
	Ui()->Screen()->Margin(160.0f, &Box);
	Graphics()->TextureClear();
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(20.0f, &Box);

	CUIRect Label;
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Connecting to"), 24.0f, TEXTALIGN_MC);

	Box.HSplitTop(20.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Client()->ConnectAddressString(), 20.0f, TEXTALIGN_MC);
}

void CMenus::RenderPopupLoading(CUIRect Screen)
{
	CUIRect Box;
	Ui()->Screen()->Margin(160.0f, &Box);

	Graphics()->TextureClear();
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(20.0f, &Box);

	CUIRect Label;
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Loading"), 24.0f, TEXTALIGN_MC);
}

#if defined(CONF_VIDEORECORDER)
void CMenus::PopupConfirmDemoReplaceVideo()
{
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s/%s.demo", m_aCurrentDemoFolder, m_aCurrentDemoSelectionName);
	char aVideoName[IO_MAX_PATH_LENGTH];
	str_copy(aVideoName, m_DemoRenderInput.GetString());
	const char *pError = Client()->DemoPlayer_Render(aBuf, m_DemolistStorageType, aVideoName, m_Speed, m_StartPaused);
	m_Speed = DEMO_SPEED_INDEX_DEFAULT;
	m_StartPaused = false;
	m_LastPauseChange = -1.0f;
	m_LastSpeedChange = -1.0f;
	if(pError)
	{
		m_DemoRenderInput.Clear();
		PopupMessage(Localize("Error loading demo"), pError, Localize("Ok"));
	}
}
#endif

void CMenus::SetActive(bool Active)
{
	if(Active != m_MenuActive)
	{
		Ui()->SetHotItem(nullptr);
		Ui()->SetActiveItem(nullptr);
	}
	m_MenuActive = Active;
	if(!m_MenuActive)
	{
		// Deactivate whichever input was active in the menu (filters, password,
		// demo rename, etc.). Without this the global input priority stays
		// stuck and the chat/console can no longer activate their own input.
		if(CLineInput *pInput = CLineInput::GetActiveInput())
			pInput->Deactivate();

		m_StartMenuTransition.m_Active = false;
		m_MenuPageTransition.m_Active = false;
		m_GamePageTransition.m_Active = false;

		if(m_NeedSendinfo)
		{
			GameClient()->SendInfo(false);
			m_NeedSendinfo = false;
		}

		if(m_NeedSendDummyinfo)
		{
			GameClient()->SendDummyInfo(false);
			m_NeedSendDummyinfo = false;
		}

		if(Client()->State() == IClient::STATE_ONLINE)
		{
			GameClient()->OnRelease();
		}
	}
	else if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		GameClient()->OnRelease();
	}
}

void CMenus::OnShutdown()
{
	m_CommunityIcons.Shutdown();
}

bool CMenus::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_MenuActive)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CMenus::OnInput(const IInput::CEvent &Event)
{
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		// Меню закрыто, мы в игре — открыть меню.
		if(!IsActive() && Client()->State() == IClient::STATE_ONLINE)
		{
			SetActive(true);
			return true;
		}

		// Меню открыто, попапа нет — обрабатываем ESC.
		if(IsActive() && m_Popup == POPUP_NONE)
		{
			// Если активен текстовый ввод — ESC сначала снимает фокус.
			if(CLineInput::GetActiveInput() != nullptr)
			{
				Ui()->OnInput(Event);
				return true;
			}

			// Оффлайн (главное меню/браузер): ESC возвращает на стартовый экран
			// через обработчик в конце Render(). Меню не закрываем —
			// в оффлайне оно не «закрывается», а показывает стартовую страницу.
			if(Client()->State() == IClient::STATE_OFFLINE)
			{
				Ui()->OnInput(Event);
				return true;
			}

			// Онлайн: закрыть меню (байпас застрявших transition'ов).
			SetActive(false);
			return true;
		}
	}

	if(IsActive())
	{
		Ui()->OnInput(Event);
		return true;
	}
	return false;
}

void CMenus::OnStateChange(int NewState, int OldState)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_OFFLINE)
		TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);

	if(NewState == IClient::STATE_OFFLINE)
	{
		if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_QUITTING)
			UpdateMusicState();
		m_Popup = POPUP_NONE;
		if(Client()->ErrorString() && Client()->ErrorString()[0] != 0)
		{
			if(str_find(Client()->ErrorString(), "password"))
			{
				m_Popup = POPUP_PASSWORD;
				m_PasswordInput.SelectAll();
				Ui()->SetActiveItem(&m_PasswordInput);
			}
			else
			{
				m_Popup = POPUP_DISCONNECTED;
			}
		}

		// Reset to a valid offline page if we were on an online-only page
		if(m_MenuPage == PAGE_GAME || m_MenuPage == PAGE_PLAYERS ||
			m_MenuPage == PAGE_SERVER_INFO || m_MenuPage == PAGE_NETWORK ||
			m_MenuPage == PAGE_GHOST || m_MenuPage == PAGE_CALLVOTE)
		{
			m_MenuPage = PAGE_INTERNET;
		}

		// Cancel any stuck page transition
		m_StartMenuTransition.m_Active = false;
		m_MenuPageTransition.m_Active = false;
		m_GamePageTransition.m_Active = false;
	}
	else if(NewState == IClient::STATE_LOADING)
	{
		m_DownloadLastCheckTime = time_get();
		m_DownloadLastCheckSize = 0;
		m_DownloadSpeed = 0.0f;
	}
	else if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
	{
		if(m_Popup != POPUP_WARNING)
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
}

void CMenus::OnWindowResize()
{
	TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);
}

void CMenus::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		SetActive(true);

	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_ServerMode == CGameClient::SERVERMODE_PUREMOD)
	{
		Client()->Disconnect();
		SetActive(true);
		PopupMessage(Localize("Disconnected"), Localize("The server is running a non-standard tuning on a pure game type."), Localize("Ok"));
	}

	Ui()->StartCheck();
	UpdateColors();

	Ui()->Update();

	if(IsActive())
		Ui()->DoBackButton();

	Render();

	if(IsActive())
	{
		Ui()->RenderBackButton();
		const bool HideCursor = m_ShowStart && (!m_StartMenuIntro.m_Played || m_StartMenuIntro.m_Active);
		if(!HideCursor)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	}

	// render debug information
	if(g_Config.m_Debug)
		Ui()->DebugRender(2.0f, Ui()->Screen()->h - 12.0f);

	if(!m_StartMenuTransition.m_Active &&
		!m_MenuPageTransition.m_Active &&
		!m_GamePageTransition.m_Active &&
		Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		SetActive(false);
	}
	// Постоянная рандомизация ника + скинов
	if(g_Config.m_ClRandomizeNickSkin && Client()->State() == IClient::STATE_ONLINE)
	{
		const auto Now = time_get_nanoseconds();
		const auto Interval = std::chrono::seconds(g_Config.m_ClRandomizeNickSkinInterval);

		if(m_LastNickSkinRandomize.count() == 0 || Now - m_LastNickSkinRandomize >= Interval)
		{
			m_LastNickSkinRandomize = Now;
			RandomizeNickAndSkins();
		}
	}
	else
	{
		m_LastNickSkinRandomize = {};
	}
	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}

void CMenus::UpdateColors()
{
	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	ms_ColorTabbarInactiveOutgame = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
	ms_ColorTabbarActiveOutgame = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	ms_ColorTabbarHoverOutgame = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);

	const float ColorIngameScaleI = 0.5f;
	const float ColorIngameScaleA = 0.2f;

	ms_ColorTabbarInactiveIngame = ColorRGBA(
		ms_GuiColor.r * ColorIngameScaleI,
		ms_GuiColor.g * ColorIngameScaleI,
		ms_GuiColor.b * ColorIngameScaleI,
		ms_GuiColor.a * 0.8f);

	ms_ColorTabbarActiveIngame = ColorRGBA(
		ms_GuiColor.r * ColorIngameScaleA,
		ms_GuiColor.g * ColorIngameScaleA,
		ms_GuiColor.b * ColorIngameScaleA,
		ms_GuiColor.a);

	ms_ColorTabbarHoverIngame = ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f);
}

void CMenus::RenderBackground()
{
	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(ScreenWidth, ScreenHeight);

	// render background color
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(ms_GuiColor.WithAlpha(1.0f));
	const IGraphics::CQuadItem BackgroundQuadItem = IGraphics::CQuadItem(0, 0, ScreenWidth, ScreenHeight);
	Graphics()->QuadsDrawTL(&BackgroundQuadItem, 1);
	Graphics()->QuadsEnd();

	// render the tiles
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.045f);
	const float Size = 15.0f;
	const float OffsetTime = std::fmod(Client()->GlobalTime() * 0.15f, 2.0f);
	IGraphics::CQuadItem aCheckerItems[64];
	size_t NumCheckerItems = 0;
	const int NumItemsWidth = std::ceil(ScreenWidth / Size);
	const int NumItemsHeight = std::ceil(ScreenHeight / Size);
	for(int y = -2; y < NumItemsHeight; y++)
	{
		for(int x = 0; x < NumItemsWidth + 4; x += 2)
		{
			aCheckerItems[NumCheckerItems] = IGraphics::CQuadItem((x - 2 * OffsetTime + (y & 1)) * Size, (y + OffsetTime) * Size, Size, Size);
			NumCheckerItems++;
			if(NumCheckerItems == std::size(aCheckerItems))
			{
				Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
				NumCheckerItems = 0;
			}
		}
	}
	if(NumCheckerItems != 0)
		Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
	Graphics()->QuadsEnd();

	// render border fade
	Graphics()->TextureSet(m_TextureBlob);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem BlobQuadItem = IGraphics::CQuadItem(-100, -100, ScreenWidth + 200, ScreenHeight + 200);
	Graphics()->QuadsDrawTL(&BlobQuadItem, 1);
	Graphics()->QuadsEnd();

	// restore screen
	Ui()->MapScreen();
}

int CMenus::DoButton_CheckBox_Tristate(const void *pId, const char *pText, TRISTATE Checked, const CUIRect *pRect)
{
	switch(Checked)
	{
	case TRISTATE::NONE:
		return DoButton_CheckBox_Common(pId, pText, "", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::SOME:
		return DoButton_CheckBox_Common(pId, pText, "O", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::ALL:
		return DoButton_CheckBox_Common(pId, pText, "X", pRect, BUTTONFLAG_LEFT);
	default:
		dbg_assert_failed("Invalid tristate. Checked: %d", static_cast<int>(Checked));
	}
}

int CMenus::MenuImageScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	const char *pExtension = ".png";
	CMenuImage MenuImage;
	CMenus *pSelf = static_cast<CMenus *>(pUser);
	if(IsDir || !str_endswith(pName, pExtension) || str_length(pName) - str_length(pExtension) >= (int)sizeof(MenuImage.m_aName))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "menuimages/%s", pName);

	CImageInfo Info;
	if(!pSelf->Graphics()->LoadPng(Info, aPath, DirType))
	{
		log_error("menus", "Failed to load menu image from '%s'", aPath);
		return 0;
	}
	if(Info.m_Format != CImageInfo::FORMAT_RGBA)
	{
		Info.Free();
		log_error("menus", "Failed to load menu image from '%s': must be an RGBA image", aPath);
		return 0;
	}

	MenuImage.m_OrgTexture = pSelf->Graphics()->LoadTextureRaw(Info, 0, aPath);

	ConvertToGrayscale(Info);
	MenuImage.m_GreyTexture = pSelf->Graphics()->LoadTextureRawMove(Info, 0, aPath);

	str_truncate(MenuImage.m_aName, sizeof(MenuImage.m_aName), pName, str_length(pName) - str_length(pExtension));
	pSelf->m_vMenuImages.push_back(MenuImage);

	pSelf->RenderLoading(Localize("Loading DDNet Client"), Localize("Loading menu images"), 0);

	return 0;
}

const CMenus::CMenuImage *CMenus::FindMenuImage(const char *pName)
{
	for(auto &Image : m_vMenuImages)
		if(str_comp(Image.m_aName, pName) == 0)
			return &Image;
	return nullptr;
}

void CMenus::SetMenuPage(int NewPage)
{
	const bool StartMenuTransition = g_Config.m_ClStartMenuAnimation && m_ShowStart &&
		((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) ||
			m_MenuPage == PAGE_DEMOS ||
			m_MenuPage == PAGE_SETTINGS);

	if(StartMenuTransition)
	{
		m_MenuPageTransition.m_Active = false;
		m_MenuPageTransition.m_FromPage = NewPage;
		m_MenuPageTransition.m_ToPage = NewPage;

		m_StartMenuTransition.m_Active = true;
		m_StartMenuTransition.m_Reverse = false;
		m_StartMenuTransition.m_TargetPage = NewPage;
		m_StartMenuTransition.m_StartTime = time();
	}

	const int OldPage = m_MenuPage;
	m_MenuPage = NewPage;

	if(NewPage >= PAGE_INTERNET && NewPage <= PAGE_FAVORITE_COMMUNITY_5)
	{
		g_Config.m_UiPage = NewPage;
		bool ForceRefresh = false;
		if(m_ForceRefreshLanPage && NewPage == PAGE_LAN)
		{
			ForceRefresh = true;
			m_ForceRefreshLanPage = false;
		}
		if(OldPage != NewPage || ForceRefresh)
		{
			RefreshBrowserTab(ForceRefresh);
		}
	}
}

void CMenus::RefreshBrowserTab(bool Force)
{
	if(g_Config.m_UiPage == PAGE_INTERNET)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_INTERNET)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage == PAGE_LAN)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_LAN)
		{
			ServerBrowser()->Refresh(IServerBrowser::TYPE_LAN);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage == PAGE_FAVORITES)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_FAVORITES)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITES);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)
	{
		const int BrowserType = g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1 + IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
		if(Force || ServerBrowser()->GetCurrentType() != BrowserType)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(BrowserType);
			UpdateCommunityCache(true);
		}
	}
}

void CMenus::ForceRefreshLanPage()
{
	m_ForceRefreshLanPage = true;
}

void CMenus::SetShowStart(bool ShowStart)
{
	m_ShowStart = ShowStart;
}


void CMenus::JoinTutorial()
{
	m_JoinTutorial.m_Queued = true;
	m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
	m_JoinTutorial.m_TryRefresh = false;
	m_JoinTutorial.m_TriedRefresh = false;
	m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::NOT_TRIED;
	m_JoinTutorial.m_StateChange = time_get_nanoseconds();
}
void CMenus::ShowQuitPopup()
{
	Client()->Quit();
}

void CMenus::OpenNotifications()
{
	Ui()->SetActiveItem(nullptr);
	m_NotificationSelected = -1;
	m_Popup = POPUP_NOTIFICATIONS;
}