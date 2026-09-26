// 实际调用 CLineInput::ProcessInput，使用内存剪贴板避免访问系统剪贴板。
#include <base/dbg.h>

#include <engine/external/tinyexpr.h>
#include <engine/keys.h>

#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" [[noreturn]] void dbg_assert_imp(const char *pFile, int Line, const char *pFormat, ...)
{
	std::fprintf(stderr, "assert at %s:%d: ", pFile, Line);
	va_list Args;
	va_start(Args, pFormat);
	std::vfprintf(stderr, pFormat, Args);
	va_end(Args);
	std::abort();
}

class CClipboardInput final : public IInput
{
public:
	std::string m_Text;
	std::vector<CTouchFingerState> m_vFingers;
	void ConsumeEvents(std::function<void(const CEvent &)>) const override {}
	void Clear() override {}
	float GetUpdateTime() const override { return 0; }
	bool ModifierIsPressed() const override { return true; }
	bool ShiftIsPressed() const override { return false; }
	bool AltIsPressed() const override { return false; }
	bool KeyIsPressed(int) const override { return false; }
	bool KeyPress(int) const override { return false; }
	const char *KeyName(int) const override { return ""; }
	int FindKeyByName(const char *) const override { return 0; }
	size_t NumJoysticks() const override { return 0; }
	IJoystick *GetJoystick(size_t) override { return nullptr; }
	IJoystick *GetActiveJoystick() override { return nullptr; }
	void SetActiveJoystick(size_t) override {}
	vec2 NativeMousePos() const override { return vec2(0, 0); }
	bool NativeMousePressed(int) const override { return false; }
	void MouseModeRelative() override {}
	void MouseModeAbsolute() override {}
	bool MouseRelative(float *, float *) override { return false; }
	const std::vector<CTouchFingerState> &TouchFingerStates() const override { return m_vFingers; }
	void ClearTouchDeltas() override {}
	std::string GetClipboardText() override { return m_Text; }
	void SetClipboardText(const char *pText) override { m_Text = pText; }
	void StartTextInput() override {}
	void StopTextInput() override {}
	void EnsureScreenKeyboardShown() override {}
	const char *GetComposition() const override { return ""; }
	bool HasComposition() const override { return false; }
	int GetCompositionCursor() const override { return 0; }
	int GetCompositionLength() const override { return 0; }
	const char *GetCandidate(int) const override { return ""; }
	int GetCandidateCount() const override { return 0; }
	int GetCandidateSelectedIndex() const override { return 0; }
	int GetCandidatePageStart() const override { return 0; }
	int GetCandidatePageSize() const override { return 0; }
	int GetCandidateTotalCount() const override { return 0; }
	void SetCompositionWindowPosition(float, float, float) override {}
	bool GetDropFile(char *, int) override { return false; }
};

int main()
{
	CClipboardInput Input;
	CLineInput::Init(nullptr, nullptr, &Input, nullptr);
	int Failures = 0;
	auto Check = [&](const char *pName, std::string Text, const char *pInitial, const std::vector<std::string> &vExpected, const char *pRemaining, bool ReplaceSelection = false, bool UseCallback = true) {
		char aBuffer[16] = {};
		CLineInput Line(aBuffer, sizeof(aBuffer));
		Line.Set(pInitial);
		Line.SelectNothing();
		if(ReplaceSelection)
		{
			Line.SetCursorOffset(3);
			Line.SetSelection(1, 3);
		}
		std::vector<std::string> vActual;
		if(UseCallback)
			Line.SetClipboardLineCallback([&](const char *pText) { vActual.emplace_back(pText); });
		Input.m_Text = Text;
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_PRESS;
		Event.m_Key = KEY_V;
		if(!Line.ProcessInput(Event) || vActual != vExpected || std::string(Line.GetString()) != pRemaining)
		{
			std::fprintf(stderr, "FAIL %s: callbacks=%zu, second bytes=%zu, remaining=%s\n", pName, vActual.size(), vActual.size() > 1 ? vActual[1].size() : 0, Line.GetString());
			++Failures;
		}
		else
			std::printf("PASS %s\n", pName);
	};
	Check("long second line", "first\n" + std::string(4096, 'a') + "\nlast", "", {"first", std::string(15, 'a')}, "last");
	Check("first line appends", "cmd\nsecond\nlast", "pre:", {"pre:cmd", "second"}, "last");
	Check("empty lines and CRLF", "\nfirst\r\n\nsecond\r\ntail", "", {"", "first", "", "second"}, "tail");
	Check("trailing newline", "first\nsecond\n", "", {"first", "second"}, "");
	Check("UTF8 truncation", "first\n" + std::string("中文中文中文中文中文中文") + "\n尾", "", {"first", "中文中文中"}, "尾");
	Check("UTF8 partial character", "first\nx中文中文中文中文\n尾", "", {"first", "x中文中文"}, "尾");
	Check("UTF8 four byte character", "first\n😀😀😀😀😀\n末😀😀😀😀", "", {"first", "😀😀😀"}, "末😀😀😀");
	Check("replace selected text", "X\nsecond\ntail", "abcd", {"aXd", "second"}, "tail", true);
	Check("no callback", "a\nb\nc", "pre:", {}, "pre:a b c", false, false);
	CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
	return Failures ? 1 : 0;
}
// 剪贴板测试不应进入渲染、计时或表达式路径；误调用时立即失败。
std::chrono::nanoseconds time_get_nanoseconds() { std::abort(); }
STextBoundingBox CTextCursor::BoundingBox() const { std::abort(); }
void CTextCursor::SetPosition(vec2) { std::abort(); }
vec2 CUi::CalcAlignedCursorPos(const CUIRect *, vec2, int, const float *) { std::abort(); }
double te_interp(const char *, int *) { std::abort(); }
