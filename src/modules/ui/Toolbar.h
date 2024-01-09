/**
 * @file
 */

#pragma once

#include "IMGUIApp.h"
#include "IMGUIEx.h"
#include "ScopedStyle.h"
#include "command/CommandHandler.h"
#include "dearimgui/imgui.h"

namespace ui {

class Toolbar {
protected:
	int _nextId;
	ImVec2 _pos;
	const float _startingPosX;
	const ImVec2 _size;
	command::CommandExecutionListener *_listener;
	float windowWidth() const;
	void setCursor();
	void next();
	void newline();
	void last();
public:
	Toolbar(const ImVec2 &size, command::CommandExecutionListener *listener = nullptr);
	~Toolbar();

	bool button(const char *icon, const char *command, bool darken = false);

	void end() {
		last();
	}

	template<class FUNC>
	bool button(const char *icon, const char *tooltip, FUNC func, bool highlight = false) {
		newline();
		ui::ScopedStyle style;
		style.setFramePadding(ImVec2(0.0f, 0.0f));
		if (highlight) {
			style.highlight(ImGuiCol_Text);
		}
		ImGui::PushID(_nextId);
		bool pressed = ImGui::Button(icon, _size);
		ImGui::PopID();
		if (pressed) {
			func();
		}
		if (tooltip != nullptr && tooltip[0] != '\0') {
			ui::ScopedStyle tooltipStyle;
			tooltipStyle.setFont(imguiApp()->defaultFont());
			ImGui::TooltipText("%s", tooltip);
		}
		next();
		return pressed;
	}

	template<class FUNC>
	void custom(FUNC func) {
		newline();
		ui::ScopedStyle style;
		style.setFramePadding(ImVec2(0.0f, 0.0f));
		func();
		next();
	}

	template<class FUNC>
	void customNoStyle(FUNC func) {
		newline();
		func();
		next();
	}
};

} // namespace ui
