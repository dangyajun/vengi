//
// Created by Matty on 2022-01-28.
//

#include "core/StandardLib.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui_internal.h"
#include "imgui_neo_internal.h"
#include "imgui_neo_sequencer.h"

#include <unordered_map>

namespace ImGui {

struct ImGuiNeoSequencerInternalData {
	ImVec2 StartCursor = {0, 0};	   // Cursor in TL corner of whole widget
	ImVec2 TopBarStartCursor = {0, 0}; // Cursor on top, below Zoom slider
	ImVec2 StartValuesCursor = {0, 0}; // Cursor on top of values
	ImVec2 ValuesCursor = {0, 0};	   // Current cursor position, used for values drawing

	ImVec2 Size = {0, 0};		// Size of whole sequencer
	ImVec2 TopBarSize = {0, 0}; // Size of top bar without Zoom

	uint32_t StartFrame = 0;
	uint32_t EndFrame = 0;
	uint32_t OffsetFrame = 0; // Offset from start

	float ValuesWidth = 32.0f; // Width of biggest label in timeline, used for offset of timeline

	float FilledHeight = 0.0f; // Height of whole sequencer

	float Zoom = 1.0f;

	ImGuiID SelectedTimeline = 0;

	uint32_t CurrentFrame = 0;
	bool HoldingCurrentFrame = false; // Are we draging current frame?
	ImVec4 CurrentFrameColor; // Color of current frame, we have to save it because we render on EndNeoSequencer, but
							  // process at BeginneoSequencer

	bool HoldingZoomSlider = false;
};

using Map = std::unordered_map<ImGuiID, ImGuiNeoSequencerInternalData>;

static ImGuiNeoSequencerStyle style; // NOLINT(cert-err58-cpp)

// Global context stuff
static bool inSequencer = false;

// Current active sequencer
static ImGuiID currentSequencer;

// Height of timeline right now
static float currentTimelineHeight = 0.0f;

// Current timeline depth, used for offset of label
static uint32_t currentTimelineDepth = 0;

static ImVector<ImGuiColorMod> sequencerColorStack;

// Data of all sequencers, this is main c++ part and I should create C alternative or use imgui ImVector or something
static Map sequencerData;

static uint32_t idCounter = 0;

///////////// STATIC HELPERS ///////////////////////

static float getPerFrameWidth(ImGuiNeoSequencerInternalData &context) {
	return GetPerFrameWidth(context.Size.x, context.ValuesWidth, context.EndFrame, context.StartFrame, context.Zoom);
}

static float getKeyframePositionX(uint32_t frame, ImGuiNeoSequencerInternalData &context) {
	const float perFrameWidth = getPerFrameWidth(context);
	return (float)(frame - context.OffsetFrame) * perFrameWidth;
}

static float getWorkTimelineWidth(ImGuiNeoSequencerInternalData &context) {
	const float perFrameWidth = getPerFrameWidth(context);
	return context.Size.x - context.ValuesWidth - perFrameWidth;
}

// Dont pull frame from context, its used for dragging
static ImRect getCurrentFrameBB(uint32_t frame, ImGuiNeoSequencerInternalData &context) {
	const ImGuiStyle &imStyle = GetStyle();
	const float width = style.CurrentFramePointerSize * GetIO().FontGlobalScale;
	const ImVec2 cursor =
		context.TopBarStartCursor + ImVec2{context.ValuesWidth + imStyle.FramePadding.x - width / 2.0f, 0};
	const ImVec2 currentFrameCursor = cursor + ImVec2{getKeyframePositionX(frame, context), 0};

	float pointerHeight = style.CurrentFramePointerSize * 2.5f;
	ImRect rect{currentFrameCursor, currentFrameCursor + ImVec2{width, pointerHeight * GetIO().FontGlobalScale}};

	return rect;
}

static void processCurrentFrame(uint32_t *frame, ImGuiNeoSequencerInternalData &context) {
	ImRect pointerRect = getCurrentFrameBB(*frame, context);
	pointerRect.Min -= ImVec2{2.0f, 2.0f};
	pointerRect.Max += ImVec2{2.0f, 2.0f};

	const ImGuiStyle &imStyle = GetStyle();

	const float timelineXmin = context.TopBarStartCursor.x + context.ValuesWidth + imStyle.FramePadding.x;

	const ImVec2 timelineXRange = {timelineXmin, // min
								   timelineXmin + context.Size.x - context.ValuesWidth};

	if (!ItemAdd(pointerRect, 0)) {
		return;
	}

	context.CurrentFrameColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointer);

	if (IsItemHovered()) {
		context.CurrentFrameColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointerHovered);
	}

	if (context.HoldingCurrentFrame) {
		if (IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			const float mousePosX = GetMousePos().x;
			const float v = mousePosX - timelineXRange.x;				// Subtract min
			const float normalized = v / getWorkTimelineWidth(context); // Divide by width to remap to 0 - 1 range
			const float clamped = ImClamp(normalized, 0.0f, 1.0f);
			const float viewSize = (float)(context.EndFrame - context.StartFrame) / context.Zoom;
			const float frameViewVal = (float)context.StartFrame + (clamped * (float)viewSize);
			const unsigned int finalFrame = (uint32_t)round(frameViewVal) + context.OffsetFrame;

			context.CurrentFrameColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointerPressed);

			*frame = finalFrame;
		}

		if (!IsMouseDown(ImGuiMouseButton_Left)) {
			context.HoldingCurrentFrame = false;
			context.CurrentFrameColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointer);
		}
	}

	if (IsItemClicked() && !context.HoldingCurrentFrame) {
		context.HoldingCurrentFrame = true;
		context.CurrentFrameColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointerPressed);
	}

	context.CurrentFrame = *frame;
}

static void finishPreviousTimeline(ImGuiNeoSequencerInternalData &context) {
	context.ValuesCursor = {context.TopBarStartCursor.x, context.ValuesCursor.y};
	currentTimelineHeight = 0.0f;
}

static bool createKeyframe(uint32_t *frame) {
	const ImGuiStyle &imStyle = GetStyle();
	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];

	const float timelineOffset = getKeyframePositionX(*frame, context);

	const ImVec2 pos = ImVec2{context.StartValuesCursor.x + imStyle.FramePadding.x, context.ValuesCursor.y} +
					   ImVec2{timelineOffset + context.ValuesWidth, 0};
	const ImVec2 bbPos = pos - ImVec2{currentTimelineHeight / 2, 0};
	const ImRect bb = {bbPos, bbPos + ImVec2{currentTimelineHeight, currentTimelineHeight}};
	if (!ItemAdd(bb, 0)) {
		return false;
	}

	ImDrawList *drawList = ImGui::GetWindowDrawList();

	drawList->AddCircleFilled(
		pos + ImVec2{0, currentTimelineHeight / 2.f}, currentTimelineHeight / 3.0f,
		IsItemHovered() ? ColorConvertFloat4ToU32(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_KeyframeHovered))
						: ColorConvertFloat4ToU32(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_Keyframe)),
		4);

	return true;
}

static const char *generateID() {
	static char idBuffer[16];
	idBuffer[0] = '#';
	idBuffer[1] = '#';
	core_memset(idBuffer + 2, 0, 14);
	SDL_snprintf(idBuffer + 2, 14, "%o", idCounter++);

	return idBuffer;
}

static void resetID() {
	idCounter = 0;
}

static void renderCurrentFrame(ImGuiNeoSequencerInternalData &context) {
	const ImRect bb = getCurrentFrameBB(context.CurrentFrame, context);

	ImDrawList *drawList = ImGui::GetWindowDrawList();

	RenderNeoSequencerCurrentFrame(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_FramePointerLine),
								   context.CurrentFrameColor, bb, context.Size.y - context.TopBarSize.y,
								   style.CurrentFrameLineWidth, drawList);
}

static void processAndRenderZoom(ImGuiNeoSequencerInternalData &context, bool allowEditingLength, uint32_t *start,
								 uint32_t *end) {
	const ImGuiStyle &imStyle = GetStyle();
	ImGuiWindow *window = GetCurrentWindow();
	const float zoomHeight = GetFontSize() * style.ZoomHeightScale;
	ImDrawList *drawList = GetWindowDrawList();
	const float inputWidth = CalcTextSize("123456").x;
	const float inputWidthWithPadding = inputWidth + imStyle.ItemSpacing.x;
	const ImVec2 cursor =
		allowEditingLength ? context.StartCursor + ImVec2{inputWidthWithPadding, 0} : context.StartCursor;
	const float size = allowEditingLength ? context.Size.x - 2 * inputWidthWithPadding : context.Size.x;
	const ImRect bb{cursor, cursor + ImVec2{size, zoomHeight}};
	const ImVec2 frameNumberBorderSize{inputWidth - imStyle.FramePadding.x, zoomHeight};
	const ImVec2 zoomBarEndWithSpacing = ImVec2{bb.Max.x + imStyle.ItemSpacing.x, context.StartCursor.y};
	int32_t startFrameVal = (int32_t)*start;
	int32_t endFrameVal = (int32_t)*end;

	if (allowEditingLength) {
		ImVec2 prevWindowCursor = window->DC.CursorPos;

		PushItemWidth(inputWidth);
		InputScalar("##input_start_frame", ImGuiDataType_U32, &startFrameVal, NULL, NULL, NULL,
					allowEditingLength ? 0 : ImGuiInputTextFlags_ReadOnly);

		window->DC.CursorPos = ImVec2{zoomBarEndWithSpacing.x, prevWindowCursor.y};

		PushItemWidth(inputWidth);
		InputScalar("##input_end_frame", ImGuiDataType_U32, &endFrameVal, NULL, NULL, NULL,
					allowEditingLength ? 0 : ImGuiInputTextFlags_ReadOnly);

		window->DC.CursorPos = prevWindowCursor;
	}

	if (startFrameVal < 0) {
		startFrameVal = (int32_t)*start;
	}

	if (endFrameVal < 0) {
		endFrameVal = (int32_t)*end;
	}

	if (endFrameVal <= startFrameVal) {
		endFrameVal = (int32_t)*end;
	}

	*start = startFrameVal;
	*end = endFrameVal;

	// drawList->AddText(startFrameTextCursor + ImVec2{frameNumberBorderSize.x, 0} -
	// ImVec2{numberTextWidth,0},IM_COL32_WHITE,numberText);

	// Background
	drawList->AddRectFilled(
		bb.Min, bb.Max, ColorConvertFloat4ToU32(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_ZoomBarBg)), 10.0f);

	const float baseWidth =
		bb.GetSize().x -
		imStyle.ItemInnerSpacing.x; // There is just half spacing applied, doing it normally makes big gap on sides

	const float sliderHeight = bb.GetSize().y - imStyle.ItemInnerSpacing.y;
	const float sliderWidth = baseWidth / context.Zoom;
	const ImVec2 sliderMin = bb.Min + imStyle.ItemInnerSpacing / 2.0f;
	const float sliderMaxWidth = baseWidth;

	const unsigned int totalFrames = (*end - *start);
	const float singleFrameWidthOffset = sliderMaxWidth / (float)totalFrames;
	const float zoomSliderOffset = singleFrameWidthOffset * (float)context.OffsetFrame;
	const ImVec2 sliderStart = sliderMin + ImVec2{zoomSliderOffset, 0};
	const float sideSize = sliderHeight;
	const ImRect finalSliderBB{sliderStart, sliderStart + ImVec2{sliderWidth, sliderHeight}};
	const ImRect finalSliderInteractBB = {finalSliderBB.Min + ImVec2{sideSize, 0},
										  finalSliderBB.Max - ImVec2{sideSize, 0}};

	const bool resBG = ItemAdd(bb, 0);

	const uint32_t viewWidth = (uint32_t)((float)totalFrames / context.Zoom);

	if (resBG) {
		if (IsItemHovered()) {
			SetItemUsingMouseWheel();
			const float currentScroll = GetIO().MouseWheel;

			context.Zoom = ImClamp(context.Zoom + currentScroll, 1.0f, (float)viewWidth);
			const uint32_t newZoomWidth = (uint32_t)((float)totalFrames / context.Zoom);

			if (*start + context.OffsetFrame + newZoomWidth > *end)
				context.OffsetFrame = ImMax(0U, totalFrames - viewWidth);
		}

		if (context.HoldingZoomSlider) {
			if (IsMouseDragging(ImGuiMouseButton_Left, 0.01f)) {
				const float currentX = GetMousePos().x;
				const float v = currentX - bb.Min.x;		// Subtract min
				const float normalized = v / bb.GetWidth(); // Divide by width to remap to 0 - 1 range
				const float sliderWidthNormalized = 1.0f / context.Zoom;
				const float singleFrameWidthOffsetNormalized = singleFrameWidthOffset / bb.GetWidth();

				uint32_t finalFrame =
					(uint32_t)((float)(normalized - sliderWidthNormalized / 2.0f) / singleFrameWidthOffsetNormalized);

				if (normalized - sliderWidthNormalized / 2.0f < 0.0f) {
					finalFrame = 0;
				}

				if (normalized + sliderWidthNormalized / 2.0f > 1.0f) {
					finalFrame = totalFrames - viewWidth;
				}

				context.OffsetFrame = finalFrame;
			}

			if (!IsMouseDown(ImGuiMouseButton_Left)) {
				context.HoldingZoomSlider = false;
			}
		}

		if (IsItemClicked()) {
			context.HoldingZoomSlider = true;
		}
	}

	const bool res = ItemAdd(finalSliderInteractBB, 0);
	const unsigned int viewStart = *start + (uint32_t)context.OffsetFrame;
	const unsigned int viewEnd = viewStart + viewWidth;

	if (res) {
		ImVec4 sliderColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_ZoomBarSlider);

		if (IsItemHovered()) {
			sliderColor = GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_ZoomBarSliderHovered);
		}

		// Render bar
		drawList->AddRectFilled(finalSliderBB.Min, finalSliderBB.Max, ColorConvertFloat4ToU32(sliderColor), 10.0f);

		const ImVec2 sliderCenter = finalSliderBB.GetCenter();
		char overlayTextBuffer[128];
		SDL_snprintf(overlayTextBuffer, sizeof(overlayTextBuffer), "%d - %d", viewStart, viewEnd);

		const ImVec2 overlaySize = CalcTextSize(overlayTextBuffer);
		drawList->AddText(sliderCenter - overlaySize / 2.0f, IM_COL32_WHITE, overlayTextBuffer);
	}
}

const ImVec4 &GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol idx) {
	return GetNeoSequencerStyle().Colors[idx];
}

ImGuiNeoSequencerStyle &GetNeoSequencerStyle() {
	return style;
}

bool BeginNeoSequencer(const char *idin, uint32_t *frame, uint32_t *startFrame, uint32_t *endFrame, const ImVec2 &size,
					   ImGuiNeoSequencerFlags flags) {
	IM_ASSERT(!inSequencer && "Called when while in other NeoSequencer, that won't work, call End!");
	IM_ASSERT(*startFrame < *endFrame && "Start frame must be smaller than end frame");

	ImGuiWindow *window = GetCurrentWindow();
	const ImGuiStyle &imStyle = GetStyle();

	if (inSequencer) {
		return false;
	}

	if (window->SkipItems) {
		return false;
	}

	ImDrawList *drawList = GetWindowDrawList();

	const ImVec2 cursor = GetCursorScreenPos();
	const ImVec2 area = ImGui::GetContentRegionAvail();

	PushID(idin);
	const ImGuiID id = window->IDStack[window->IDStack.size() - 1];

	inSequencer = true;

	ImGuiNeoSequencerInternalData &context = sequencerData[id];

	ImVec2 realSize = ImFloor(size);
	if (realSize.x <= 0.0f) {
		realSize.x = ImMax(4.0f, area.x);
	}
	if (realSize.y <= 0.0f) {
		realSize.y = ImMax(4.0f, context.FilledHeight);
	}

	const bool showZoom = !(flags & ImGuiNeoSequencerFlags_HideZoom);

	context.StartCursor = cursor;
	// If Zoom is shown, we offset it by height of Zoom bar + padding
	context.TopBarStartCursor =
		showZoom ? cursor + ImVec2{0, GetFontSize() * style.ZoomHeightScale + imStyle.FramePadding.y} : cursor;
	context.StartFrame = *startFrame;
	context.EndFrame = *endFrame;
	context.Size = realSize;

	currentSequencer = id;

	RenderNeoSequencerBackground(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_Bg), context.StartCursor,
								 context.Size, drawList, style.SequencerRounding);

	RenderNeoSequencerTopBarBackground(GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_TopBarBg),
									   context.TopBarStartCursor, context.TopBarSize, drawList,
									   style.SequencerRounding);

	RenderNeoSequencerTopBarOverlay(context.Zoom, context.ValuesWidth, context.StartFrame, context.EndFrame,
									context.OffsetFrame, context.TopBarStartCursor, context.TopBarSize, drawList,
									style.TopBarShowFrameLines, style.TopBarShowFrameTexts);

	if (showZoom) {
		processAndRenderZoom(context, flags & ImGuiNeoSequencerFlags_AllowLengthChanging, startFrame, endFrame);
	}

	context.TopBarSize = ImVec2(context.Size.x, style.TopBarHeight);

	if (context.TopBarSize.y <= 0.0f) {
		context.TopBarSize.y = CalcTextSize("100").y + imStyle.FramePadding.y * 2.0f;
	}

	if (context.Size.y < context.FilledHeight) {
		context.Size.y = context.FilledHeight;
	}

	context.FilledHeight = context.TopBarSize.y + style.TopBarSpacing +
						   (showZoom ? imStyle.FramePadding.y + style.ZoomHeightScale + GetFontSize() : 0.0f);

	context.StartValuesCursor = context.TopBarStartCursor + ImVec2{0, context.TopBarSize.y + style.TopBarSpacing};
	context.ValuesCursor = context.StartValuesCursor;

	processCurrentFrame(frame, context);

	return true;
}

void EndNeoSequencer() {
	IM_ASSERT(inSequencer && "Called end sequencer when BeginSequencer didnt return true or wasn't called at all!");
	IM_ASSERT(sequencerData.count(currentSequencer) != 0 && "Ended sequencer has no context!");

	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];

	renderCurrentFrame(context);

	inSequencer = false;

	const ImVec2 min = {0, 0};
	context.Size.y = context.FilledHeight;
	const ImVec2 max = context.Size;

	ItemSize({min, max});
	PopID();
	resetID();
}

IMGUI_API bool BeginNeoGroup(const char *label, bool *open) {
	return BeginNeoTimeline(label, nullptr, 0, open, ImGuiNeoTimelineFlags_Group);
}

IMGUI_API void EndNeoGroup() {
	return EndNeoTimeLine();
}

static bool groupBehaviour(const ImGuiID id, bool *open, const ImVec2 &labelSize) {
	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];
	ImGuiWindow *window = GetCurrentWindow();

	const bool closable = open != nullptr;

	ImDrawList *drawList = ImGui::GetWindowDrawList();
	const float arrowWidth = drawList->_Data->FontSize;
	const ImVec2 arrowSize = {arrowWidth, arrowWidth};
	const ImRect arrowBB = {context.ValuesCursor, context.ValuesCursor + arrowSize};
	const ImVec2 groupBBMin = {context.ValuesCursor + ImVec2{arrowSize.x, 0.0f}};
	const ImRect groupBB = {groupBBMin, groupBBMin + labelSize};
	const ImGuiID arrowID = window->GetID(generateID());
	const bool addArrowRes = ItemAdd(arrowBB, arrowID);
	if (addArrowRes) {
		if (IsItemClicked() && closable) {
			(*open) = !(*open);
		}
	}

	const bool addGroupRes = ItemAdd(groupBB, id);
	if (addGroupRes) {
		if (IsItemClicked()) {
			context.SelectedTimeline = context.SelectedTimeline == id ? 0 : id;
		}
	}
	const float width = groupBB.Max.x - arrowBB.Min.x;
	context.ValuesWidth = core_max(context.ValuesWidth, width); // Make left panel wide enough
	return addGroupRes && addArrowRes;
}

static bool timelineBehaviour(const ImGuiID id, const ImVec2 &labelSize) {
	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];

	const ImRect groupBB{context.ValuesCursor, context.ValuesCursor + labelSize};

	const bool addGroupRes = ItemAdd(groupBB, id);
	if (addGroupRes) {
		if (IsItemClicked()) {
			context.SelectedTimeline = context.SelectedTimeline == id ? 0 : id;
		}
	}
	const float width = groupBB.Max.x - groupBB.Min.x;
	context.ValuesWidth = core_max(context.ValuesWidth, width); // Make left panel wide enough

	return addGroupRes;
}

bool BeginNeoTimeline(const char *label, uint32_t **keyframes, uint32_t keyframeCount, bool *open,
					  ImGuiNeoTimelineFlags flags) {
	IM_ASSERT(inSequencer && "Not in active sequencer!");

	const bool closable = open != nullptr;
	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];
	const ImGuiStyle &imStyle = GetStyle();
	ImGuiWindow *window = GetCurrentWindow();
	const ImGuiID id = window->GetID(label);
	ImVec2 labelSize = CalcTextSize(label);

	labelSize.y += imStyle.FramePadding.y * 2 + style.ItemSpacing.y * 2;
	labelSize.x +=
		imStyle.FramePadding.x * 2 + style.ItemSpacing.x * 2 + (float)currentTimelineDepth * style.DepthItemSpacing;

	const bool isGroup = flags & ImGuiNeoTimelineFlags_Group && closable;
	bool addRes = false;
	if (isGroup) {
		labelSize.x += imStyle.ItemSpacing.x + GetFontSize();
		addRes = groupBehaviour(id, open, labelSize);
	} else {
		addRes = timelineBehaviour(id, labelSize);
	}

	if (currentTimelineDepth > 0) {
		context.ValuesCursor = {context.TopBarStartCursor.x, context.ValuesCursor.y};
	}

	currentTimelineHeight = labelSize.y;
	context.FilledHeight += currentTimelineHeight;

	if (addRes) {
		RenderNeoTimelane(id == context.SelectedTimeline, context.ValuesCursor + ImVec2{context.ValuesWidth, 0},
						  ImVec2{context.Size.x - context.ValuesWidth, currentTimelineHeight},
						  GetStyleNeoSequencerColorVec4(ImGuiNeoSequencerCol_SelectedTimeline));

		ImVec4 color = GetStyleColorVec4(ImGuiCol_Text);
		if (IsItemHovered()) {
			color.w *= 0.7f;
		}

		RenderNeoTimelineLabel(label,
							   context.ValuesCursor + imStyle.FramePadding +
								   ImVec2{(float)currentTimelineDepth * style.DepthItemSpacing, 0},
							   labelSize, color, isGroup, isGroup && (*open));
	}

	for (uint32_t i = 0; i < keyframeCount; i++) {
		/*bool keyframeRes = */ createKeyframe(keyframes[i]);
	}

	context.ValuesCursor.x += imStyle.FramePadding.x + (float)currentTimelineDepth * style.DepthItemSpacing;
	context.ValuesCursor.y += currentTimelineHeight;

	const bool result = !closable || (*open);
	if (result) {
		currentTimelineDepth++;
	} else {
		finishPreviousTimeline(context);
	}
	return result;
}

void EndNeoTimeLine() {
	ImGuiNeoSequencerInternalData &context = sequencerData[currentSequencer];
	finishPreviousTimeline(context);
	currentTimelineDepth--;
}

bool NeoBeginCreateKeyframe(uint32_t *frame) {
	return false;
}

void PushNeoSequencerStyleColor(ImGuiNeoSequencerCol idx, ImU32 col) {
	ImGuiColorMod backup;
	backup.Col = idx;
	backup.BackupValue = style.Colors[idx];
	sequencerColorStack.push_back(backup);
	style.Colors[idx] = ColorConvertU32ToFloat4(col);
}

void PushNeoSequencerStyleColor(ImGuiNeoSequencerCol idx, const ImVec4 &col) {
	ImGuiColorMod backup;
	backup.Col = idx;
	backup.BackupValue = style.Colors[idx];
	sequencerColorStack.push_back(backup);
	style.Colors[idx] = col;
}

void PopNeoSequencerStyleColor(int count) {
	while (count > 0) {
		ImGuiColorMod &backup = sequencerColorStack.back();
		style.Colors[backup.Col] = backup.BackupValue;
		sequencerColorStack.pop_back();
		count--;
	}
}

} // namespace ImGui

ImGuiNeoSequencerStyle::ImGuiNeoSequencerStyle() {
	Colors[ImGuiNeoSequencerCol_Bg] = ImVec4{0.31f, 0.31f, 0.31f, 1.00f};
	Colors[ImGuiNeoSequencerCol_TopBarBg] = ImVec4{0.22f, 0.22f, 0.22f, 0.84f};
	Colors[ImGuiNeoSequencerCol_SelectedTimeline] = ImVec4{0.98f, 0.706f, 0.322f, 0.88f};
	Colors[ImGuiNeoSequencerCol_TimelinesBg] = Colors[ImGuiNeoSequencerCol_TopBarBg];
	Colors[ImGuiNeoSequencerCol_TimelineBorder] = Colors[ImGuiNeoSequencerCol_Bg] * ImVec4{0.5f, 0.5f, 0.5f, 1.0f};

	Colors[ImGuiNeoSequencerCol_FramePointer] = ImVec4{0.98f, 0.24f, 0.24f, 0.50f};
	Colors[ImGuiNeoSequencerCol_FramePointerHovered] = ImVec4{0.98f, 0.15f, 0.15f, 1.00f};
	Colors[ImGuiNeoSequencerCol_FramePointerPressed] = ImVec4{0.98f, 0.08f, 0.08f, 1.00f};

	Colors[ImGuiNeoSequencerCol_Keyframe] = ImVec4{0.59f, 0.59f, 0.59f, 0.50f};
	Colors[ImGuiNeoSequencerCol_KeyframeHovered] = ImVec4{0.98f, 0.39f, 0.36f, 1.00f};
	Colors[ImGuiNeoSequencerCol_KeyframePressed] = ImVec4{0.98f, 0.39f, 0.36f, 1.00f};

	Colors[ImGuiNeoSequencerCol_FramePointerLine] = ImVec4{0.98f, 0.98f, 0.98f, 0.8f};

	Colors[ImGuiNeoSequencerCol_ZoomBarBg] = ImVec4{0.59f, 0.59f, 0.59f, 0.90f};
	Colors[ImGuiNeoSequencerCol_ZoomBarSlider] = ImVec4{0.8f, 0.8f, 0.8f, 0.60f};
	Colors[ImGuiNeoSequencerCol_ZoomBarSliderHovered] = ImVec4{0.98f, 0.98f, 0.98f, 0.80f};
	Colors[ImGuiNeoSequencerCol_ZoomBarSliderEnds] = ImVec4{0.59f, 0.59f, 0.59f, 0.90f};
	Colors[ImGuiNeoSequencerCol_ZoomBarSliderEndsHovered] = ImVec4{0.93f, 0.93f, 0.93f, 0.93f};
}
