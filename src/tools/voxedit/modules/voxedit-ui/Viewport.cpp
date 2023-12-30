/**
 * @file
 */

#include "Viewport.h"
#include "Gizmo.h"
#include "DragAndDropPayload.h"
#include "ui/IconsLucide.h"
#include "app/App.h"
#include "core/ArrayLength.h"
#include "core/Color.h"
#include "core/Common.h"
#include "core/Log.h"
#include "core/Var.h"
#include "image/Image.h"
#include "io/FileStream.h"
#include "io/Filesystem.h"
#include "scenegraph/SceneGraphNode.h"
#include "ui/IMGUIApp.h"
#include "ui/IMGUIEx.h"
#include "ui/ScopedStyle.h"
#include "ui/dearimgui/ImGuizmo.h"
#include "video/Camera.h"
#include "video/Renderer.h"
#include "video/WindowedApp.h"
#include "voxedit-util/Config.h"
#include "voxedit-util/SceneManager.h"
#include "voxedit-util/modifier/ModifierType.h"
#include "voxel/RawVolume.h"
#include "voxel/Voxel.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/vector_relational.hpp>

namespace voxedit {

core::String Viewport::viewportId(int id) {
	return core::string::format("###viewport%i", id);
}

Viewport::Viewport(int id, bool sceneMode, bool detailedTitle)
	: _id(id), _uiId(viewportId(id)), _detailedTitle(detailedTitle) {
	_renderContext.sceneMode = sceneMode;
}

Viewport::~Viewport() {
	shutdown();
}

bool Viewport::init() {
	_rotationSpeed = core::Var::getSafe(cfg::ClientMouseRotationSpeed);
	_cursorDetails = core::Var::getSafe(cfg::VoxEditCursorDetails);
	_showAxisVar = core::Var::getSafe(cfg::VoxEditShowaxis);
	_gizmoOperations = core::Var::getSafe(cfg::VoxEditGizmoOperations);
	_gizmoAllowAxisFlip = core::Var::getSafe(cfg::VoxEditGizmoAllowAxisFlip);
	_gizmoSnap = core::Var::getSafe(cfg::VoxEditGizmoSnap);
	_modelGizmo = core::Var::getSafe(cfg::VoxEditModelGizmo);
	_viewDistance = core::Var::getSafe(cfg::VoxEditViewdistance);
	_simplifiedView = core::Var::getSafe(cfg::VoxEditSimplifiedView);
	_pivotMode = core::Var::getSafe(cfg::VoxEditGizmoPivot);
	_hideInactive = core::Var::getSafe(cfg::VoxEditHideInactive);
	if (!_renderContext.init(video::getWindowSize())) {
		return false;
	}

	resetCamera();

	return true;
}

void Viewport::resetCamera(float distance, const glm::ivec3 &center, const glm::ivec3 &size) {
	_camera.setRotationType(video::CameraRotationType::Target);
	_camera.setAngles(0.0f, 0.0f, 0.0f);
	_camera.setFarPlane(_viewDistance->floatVal());
	_camera.setTarget(center);
	_camera.setTargetDistance(distance);
	if (_camMode == SceneCameraMode::Free) {
		_camera.setWorldPosition(glm::vec3(-distance, (float)size.y + distance, -distance));
	} else if (_camMode == SceneCameraMode::Top) {
		_camera.setWorldPosition(glm::vec3(center.x, center.y + size.y, center.z));
	} else if (_camMode == SceneCameraMode::Bottom) {
		_camera.setWorldPosition(glm::vec3(center.x, center.y - size.y, center.z));
	} else if (_camMode == SceneCameraMode::Left) {
		_camera.setWorldPosition(glm::vec3(center.x + size.x, center.y, center.z));
	} else if (_camMode == SceneCameraMode::Right) {
		_camera.setWorldPosition(glm::vec3(center.x - size.x, center.y, center.z));
	} else if (_camMode == SceneCameraMode::Front) {
		_camera.setWorldPosition(glm::vec3(center.x, center.y, center.z + size.z));
	} else if (_camMode == SceneCameraMode::Back) {
		_camera.setWorldPosition(glm::vec3(center.x, center.y, center.z - size.z));
	}
}

void Viewport::resize(const glm::ivec2 &frameBufferSize) {
	const ui::IMGUIApp *app = imguiApp();
	const glm::vec2 windowSize(app->windowDimension());
	const glm::vec2 windowFrameBufferSize(app->frameBufferDimension());
	const glm::vec2 scale = windowFrameBufferSize / windowSize;
	const glm::ivec2 cameraSize((float)frameBufferSize.x * scale.x, (float)frameBufferSize.y * scale.y);
	_camera.setSize(cameraSize);
	_renderContext.resize(frameBufferSize);
}

bool Viewport::isFixedCamera() const {
	return _camMode != SceneCameraMode::Free;
}

void Viewport::move(bool pan, bool rotate, int x, int y) {
	if (rotate) {
		if (!isFixedCamera()) {
			const float yaw = (float)(x - _mouseX);
			const float pitch = (float)(y - _mouseY);
			const float s = _rotationSpeed->floatVal();
			_camera.turn(yaw * s);
			_camera.setPitch(pitch * s);
		}
	} else if (pan) {
		_camera.pan(x - _mouseX, y - _mouseY);
	}
	_mouseX = x;
	_mouseY = y;
}

void Viewport::updateViewportTrace(float headerSize) {
	const ImVec2 windowPos = ImGui::GetWindowPos();
	const int mouseX = (int)(ImGui::GetIO().MousePos.x - windowPos.x);
	const int mouseY = (int)((ImGui::GetIO().MousePos.y - windowPos.y) - headerSize);
	const bool rotate = sceneMgr().cameraRotate();
	const bool pan = sceneMgr().cameraPan();
	move(pan, rotate, mouseX, mouseY);
	sceneMgr().setMousePos(_mouseX, _mouseY);
	sceneMgr().setActiveCamera(&camera());
	sceneMgr().trace(_renderContext.sceneMode);
}

void Viewport::dragAndDrop(float headerSize) {
	if (ImGui::BeginDragDropTarget()) {
		if (!isSceneMode()) {
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dragdrop::ImagePayload)) {
				const image::ImagePtr &image = *(const image::ImagePtr *)payload->Data;
				updateViewportTrace(headerSize);
				sceneMgr().fillPlane(image);
			}
		}
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dragdrop::PaletteIndexPayload)) {
			const int dragPalIdx = (int)(intptr_t)payload->Data;
			const int nodeId = sceneMgr().sceneGraph().activeNode();
			if (scenegraph::SceneGraphNode *node = sceneMgr().sceneGraphNode(nodeId)) {
				if (node->visible() && node->isModelNode()) {
					updateViewportTrace(headerSize);
					ModifierFacade &modifier = sceneMgr().modifier();
					modifier.setCursorVoxel(voxel::createVoxel(node->palette(), dragPalIdx));
					modifier.start();
					auto callback = [nodeId](const voxel::Region &region, ModifierType type, bool markUndo) {
						if (type != ModifierType::Select && type != ModifierType::ColorPicker) {
							sceneMgr().modified(nodeId, region, markUndo);
						}
					};
					modifier.execute(sceneMgr().sceneGraph(), *node, callback);
					modifier.stop();
				}
			}
		}
		if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(dragdrop::ModelPayload)) {
			const core::String &filename = *(core::String *)payload->Data;
			sceneMgr().import(filename);
		}

		ImGui::EndDragDropTarget();
	}
}

void Viewport::renderViewportImage(const glm::ivec2 &contentSize) {
	// use the uv coords here to take a potential fb flip into account
	const glm::vec4 &uv = _renderContext.frameBuffer.uv();
	const glm::vec2 uva(uv.x, uv.y);
	const glm::vec2 uvc(uv.z, uv.w);
	const video::TexturePtr &texture = _renderContext.frameBuffer.texture(video::FrameBufferAttachment::Color0);
	ImGui::Image(texture->handle(), contentSize, uva, uvc);
}

void Viewport::renderCursor() {
	if (_renderContext.sceneMode) {
		return;
	}

	const SceneManager &mgr = sceneMgr();
	const ModifierFacade &modifier = mgr.modifier();
	if (modifier.isMode(ModifierType::ColorPicker)) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}

	const int cursorDetailsLevel = _cursorDetails->intVal();
	if (cursorDetailsLevel == 0) {
		return;
	}

	const glm::ivec3 &cursorPos = modifier.cursorPosition();
	if (cursorDetailsLevel == 1) {
		ImGui::TooltipText("%i:%i:%i", cursorPos.x, cursorPos.y, cursorPos.z);
		return;
	}

	const int activeNode = mgr.sceneGraph().activeNode();
	if (const voxel::RawVolume *v = mgr.volume(activeNode)) {
		const glm::ivec3 &mins = v->region().getLowerCorner();
		const glm::ivec3 &size = v->region().getDimensionsInVoxels();
		if (mins.x == 0 && mins.y == 0 && mins.z == 0) {
			ImGui::TooltipText("pos: %i:%i:%i\nsize: %i:%i:%i\nabsolute: %i:%i:%i\n", mins.x, mins.y, mins.z, size.x,
							   size.y, size.z, cursorPos.x, cursorPos.y, cursorPos.z);
		} else {
			ImGui::TooltipText("pos: %i:%i:%i\nsize: %i:%i:%i\nabsolute: %i:%i:%i\nrelative: %i:%i:%i", mins.x, mins.y,
							   mins.z, size.x, size.y, size.z, cursorPos.x, cursorPos.y, cursorPos.z,
							   cursorPos.x - mins.x, cursorPos.y - mins.y, cursorPos.z - mins.z);
		}
	}
}

void Viewport::renderViewport() {
	core_trace_scoped(Viewport);
	glm::ivec2 contentSize = ImGui::GetContentRegionAvail();
	const float headerSize = ImGui::GetCursorPosY();
	if (setupFrameBuffer(contentSize)) {
		_camera.update(imguiApp()->deltaFrameSeconds());

		renderToFrameBuffer();
		renderViewportImage(contentSize);
		const bool modifiedRegion = renderGizmo(camera(), headerSize, contentSize);

		if (sceneMgr().isLoading()) {
			const float radius = ImGui::GetFontSize() * 12.0f;
			ImGui::LoadingIndicatorCircle("Loading", radius, core::Color::White(), core::Color::Gray());
		} else if (ImGui::IsItemHovered() && !modifiedRegion) {
			renderCursor();
			updateViewportTrace(headerSize);
			_hovered = true;
		}

		dragAndDrop(headerSize);
	}
}

void Viewport::menuBarCameraProjection() {
	static const char *modes[] = {"Perspective", "Orthogonal"};
	static_assert(lengthof(modes) == (int)video::CameraMode::Max, "Array size doesn't match enum values");
	const int currentMode = (int)camera().mode();
	const float modeMaxWidth = ImGui::CalcComboBoxWidth(modes[currentMode]);
	ImGui::SetNextItemWidth(modeMaxWidth);
	if (ImGui::BeginCombo("##cameraproj", modes[currentMode])) {
		for (int n = 0; n < lengthof(modes); n++) {
			const bool isSelected = (currentMode == n);
			if (ImGui::Selectable(modes[n], isSelected)) {
				camera().setMode((video::CameraMode)n);
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

void Viewport::menuBarCameraMode() {
	const int currentMode = (int)_camMode;
	const float modeMaxWidth = ImGui::CalcComboBoxWidth(SceneCameraModeStr[currentMode]);
	ImGui::SetNextItemWidth(modeMaxWidth);
	if (ImGui::BeginCombo("##cameramode", SceneCameraModeStr[currentMode])) {
		for (int n = 0; n < lengthof(SceneCameraModeStr); n++) {
			const bool isSelected = (currentMode == n);
			if (ImGui::Selectable(SceneCameraModeStr[n], isSelected)) {
				_camMode = (SceneCameraMode)n;
				resetCamera();
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

bool Viewport::isSceneMode() const {
	return _renderContext.sceneMode;
}

void Viewport::toggleScene() {
	if (_simplifiedView->boolVal()) {
		return;
	}
	if (_renderContext.sceneMode) {
		_renderContext.sceneMode = false;
	} else {
		_renderContext.sceneMode = true;
	}
}

void Viewport::toggleVideoRecording() {
	if (!_avi.isRecording()) {
		video::WindowedApp::getInstance()->saveDialog(
			[this](const core::String &file, const io::FormatDescription *desc) {
				const glm::ivec2 &dim = _renderContext.frameBuffer.dimension();
				_avi.startRecording(file.c_str(), dim.x, dim.y);
			},
			{}, nullptr, "video.avi");
	} else {
		Log::debug("Stop recording");
		_avi.stopRecording();
	}
}

void Viewport::menuBarView(command::CommandExecutionListener *listener) {
	if (ImGui::BeginMenu(ICON_LC_EYE " View")) {
		ImGui::CommandMenuItem(ICON_LC_VIDEO " Reset camera", "resetcamera", true, listener);

		glm::vec3 omega = _camera.omega();
		if (ImGui::InputFloat("Camera rotation", &omega.y)) {
			_camera.setOmega(omega);
		}

		const core::String command = core::string::format("screenshot %i", _id);
		ImGui::CommandMenuItem(ICON_LC_CAMERA " Screenshot", command.c_str(), listener);

		if (ImGui::MenuItem(_avi.isRecording() ? ICON_LC_STOP_CIRCLE " Video" : ICON_LC_CLAPPERBOARD " Video")) {
			toggleVideoRecording();
		}
		const uint32_t pendingFrames = _avi.pendingFrames();
		if (pendingFrames > 0u) {
			ImGui::TooltipText("Pending frames: %u", pendingFrames);
		} else {
			ImGui::TooltipText("You can control the fps of the video with the cvar %s\nPending frames: %u",
							   cfg::CoreMaxFPS, pendingFrames);
		}

		if (!isFixedCamera()) {
			static const char *camRotTypes[] = {"Reference Point", "Eye"};
			static_assert(lengthof(camRotTypes) == (int)video::CameraRotationType::Max,
						  "Array size doesn't match enum values");
			const int currentCamRotType = (int)camera().rotationType();
			if (ImGui::BeginCombo("Camera movement##referencepoint", camRotTypes[currentCamRotType])) {
				for (int n = 0; n < lengthof(camRotTypes); n++) {
					const bool isSelected = (currentCamRotType == n);
					if (ImGui::Selectable(camRotTypes[n], isSelected)) {
						camera().setRotationType((video::CameraRotationType)n);
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}

		static const char *polygonModes[] = {"Points", "Lines", "Solid"};
		static_assert(lengthof(polygonModes) == (int)video::PolygonMode::Max, "Array size doesn't match enum values");
		const int currentPolygonMode = (int)camera().polygonMode();
		if (ImGui::BeginCombo("Render mode##polygonmode", polygonModes[currentPolygonMode])) {
			for (int n = 0; n < lengthof(polygonModes); n++) {
				const bool isSelected = (currentPolygonMode == n);
				if (ImGui::Selectable(polygonModes[n], isSelected)) {
					camera().setPolygonMode((video::PolygonMode)n);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndMenu();
	}
}

void Viewport::renderMenuBar(command::CommandExecutionListener *listener) {
	if (ImGui::BeginMenuBar()) {
		const MementoHandler &mementoHandler = sceneMgr().mementoHandler();
		ImGui::CommandMenuItem(ICON_LC_ROTATE_CCW " Undo", "undo", mementoHandler.canUndo(), listener);
		ImGui::CommandMenuItem(ICON_LC_ROTATE_CW " Redo", "redo", mementoHandler.canRedo(), listener);
		ImGui::Dummy(ImVec2(20, 0));
		menuBarCameraProjection();
		menuBarCameraMode();
		if (!_simplifiedView->boolVal()) {
			ImGui::Checkbox("Scene Mode", &_renderContext.sceneMode);
		}
		menuBarView(listener);

		ImGui::EndMenuBar();
	}
}

void Viewport::update(command::CommandExecutionListener *listener) {
	_camera.setFarPlane(_viewDistance->floatVal());

	_hovered = false;
	_visible = false;
	ui::ScopedStyle style;
	style.setWindowRounding(0.0f);
	style.setWindowBorderSize(0.0f);
	style.setWindowPadding(ImVec2(0.0f, 0.0f));
	const int sceneWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
								 ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoFocusOnAppearing;
	const char *modeStr = isSceneMode() ? "SceneMode" : "EditMode";

	core::String name;
	if (_detailedTitle) {
		name = core::string::format("%s %s%s", SceneCameraModeStr[(int)_camMode], modeStr, _uiId.c_str());
	} else {
		name = core::string::format("%s%s", modeStr, _uiId.c_str());
	}
	if (ImGui::Begin(name.c_str(), nullptr, sceneWindowFlags)) {
		_visible = true;
		renderMenuBar(listener);
		renderViewport();
	}
	ImGui::End();

	if (_avi.isRecording()) {
		_avi.enqueueFrame(renderToImage("**video**"));
	} else if (_avi.hasFinished()) {
		_avi.flush();
	}
}

void Viewport::shutdown() {
	_renderContext.shutdown();
	_avi.abort();
}

image::ImagePtr Viewport::renderToImage(const char *imageName) {
	_renderContext.frameBuffer.bind(true);
	sceneMgr().render(_renderContext, camera(), SceneManager::RenderScene);
	_renderContext.frameBuffer.unbind();
	return _renderContext.frameBuffer.image(imageName, video::FrameBufferAttachment::Color0);
}

bool Viewport::saveImage(const char *filename) {
	const image::ImagePtr &image = renderToImage(filename);
	if (!image) {
		Log::error("Failed to read texture");
		return false;
	}
	const io::FilePtr &file = io::filesystem()->open(image->name(), io::FileMode::SysWrite);
	io::FileStream stream(file);
	if (!stream.valid()) {
		return false;
	}
	return image->writePng(stream);
}

void Viewport::resetCamera() {
	const scenegraph::SceneGraph &sceneGraph = sceneMgr().sceneGraph();
	const voxel::Region &sceneRegion = sceneGraph.region();
	const int activeNode = sceneGraph.activeNode();
	const voxel::RawVolume *v = sceneMgr().volume(activeNode);
	const voxel::Region &region = v != nullptr ? v->region() : sceneRegion;
	glm::ivec3 size = region.getDimensionsInVoxels();
	glm::ivec3 center = region.getCenter();

	if (_renderContext.sceneMode) {
		if (_hideInactive->boolVal()) {
			if (scenegraph::SceneGraphNode *node = sceneMgr().sceneGraphNode(activeNode)) {
				scenegraph::KeyFrameIndex keyFrameIndex = node->keyFrameForFrame(sceneMgr().currentFrame());
				const scenegraph::SceneGraphTransform &transform = node->transform(keyFrameIndex);
				center = transform.worldTranslation() + glm::vec3(region.getCenter());
			} else {
				center = sceneGraph.center();
				size = sceneRegion.getDimensionsInVoxels();
			}
		} else {
			center = sceneGraph.center();
			size = sceneRegion.getDimensionsInVoxels();
		}
	}

	const float maxDim = (float)glm::max(size.x, glm::max(size.y, size.z));
	const float distance = maxDim * 2.0f;
	resetCamera(distance, center, size);
}

bool Viewport::setupFrameBuffer(const glm::ivec2 &frameBufferSize) {
	if (frameBufferSize.x <= 0 || frameBufferSize.y <= 0) {
		return false;
	}
	if (_renderContext.frameBuffer.dimension() == frameBufferSize) {
		return true;
	}
	resize(frameBufferSize);
	return true;
}

void Viewport::reset() {
	if (_transformMementoLocked) {
		Log::debug("Unlock memento state in reset()");
		sceneMgr().mementoHandler().unlock();
		sceneMgr().modifier().unlock();
		_transformMementoLocked = false;
	}
}

void Viewport::unlock(const scenegraph::SceneGraphNode &node, scenegraph::KeyFrameIndex keyFrameIdx) {
	if (!_transformMementoLocked) {
		return;
	}
	Log::debug("Unlock memento state");
	sceneMgr().mementoHandler().unlock();
	sceneMgr().modifier().unlock();
	if (keyFrameIdx == InvalidKeyFrame) {
		// there is no valid key frame idx given in edit mode
		sceneMgr().mementoHandler().markModification(node, node.region());
	} else {
		// we have a valid key frame idx in scene mode
		sceneMgr().mementoHandler().markNodeTransform(node, keyFrameIdx);
	}
	_transformMementoLocked = false;
}

void Viewport::lock(const scenegraph::SceneGraphNode &node, scenegraph::KeyFrameIndex keyFrameIdx) {
	if (_transformMementoLocked) {
		return;
	}
	Log::debug("Lock memento state");
	if (keyFrameIdx != InvalidKeyFrame) {
		sceneMgr().mementoHandler().markNodeTransform(node, keyFrameIdx);
	}
	sceneMgr().mementoHandler().lock();
	sceneMgr().modifier().lock();
	_transformMementoLocked = true;
}

void Viewport::updateGizmoValues(const scenegraph::SceneGraphNode &node, scenegraph::KeyFrameIndex keyFrameIdx,
								 const glm::mat4 &matrix) {
	if (ImGuizmo::IsUsing()) {
		lock(node, keyFrameIdx);
		glm::vec3 translate;
		glm::vec3 rotation;
		glm::vec3 scale;
		ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), glm::value_ptr(translate),
											  glm::value_ptr(rotation), glm::value_ptr(scale));
		if (glm::all(glm::greaterThan(scale, glm::vec3(0)))) {
			_bounds.maxs = _boundsNode.maxs * scale;
		}
	} else if (_transformMementoLocked) {
		unlock(node, keyFrameIdx);
		const voxel::Region &region = node.region();
		const voxel::Region newRegion(region.getLowerCorner(),
									  region.getLowerCorner() + glm::ivec3(glm::ceil(_bounds.maxs)) - 1);
		if (newRegion.isValid() && region != newRegion) {
			sceneMgr().resize(node.id(), newRegion);
			updateBounds(node);
		}
	}
}

bool Viewport::wantGizmo() const {
	if (_renderContext.sceneMode) {
		return true;
	}
	if (_modelGizmo->boolVal()) {
		return true;
	}
	return false;
}

bool Viewport::createReference(const scenegraph::SceneGraphNode &node) const {
	if (!isSceneMode()) {
		return false;
	}
	if (!node.isModelNode()) {
		return false;
	}
	if (!ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
		return false;
	}
	if (!ImGuizmo::IsOver()) {
		return false;
	}
	if (!ImGui::IsKeyPressed(ImGuiKey_MouseLeft)) {
		return false;
	}
	return true;
}

uint32_t Viewport::gizmoOperation(const scenegraph::SceneGraphNode &node) const {
	if (isSceneMode() && !_pivotMode->boolVal()) {
		// create reference mode - only allow translation
		if (node.isModelNode() && ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
			return ImGuizmo::TRANSLATE;
		}

		const uint32_t mask = _gizmoOperations->intVal();
		uint32_t operation = 0;
		if (mask & GizmoOperation_Translate) {
			operation |= ImGuizmo::TRANSLATE;
		}
		if (mask & GizmoOperation_Bounds) {
			operation |= ImGuizmo::BOUNDS;
		}
		if (mask & GizmoOperation_Scale) {
			operation |= ImGuizmo::SCALE;
		}
		if (mask & GizmoOperation_Rotate) {
			operation |= ImGuizmo::ROTATE;
		}
		return operation;
	}
	return ImGuizmo::TRANSLATE;
}

glm::mat4 Viewport::gizmoMatrix(const scenegraph::SceneGraphNode &node, scenegraph::KeyFrameIndex &keyFrameIdx) const {
	const scenegraph::SceneGraph &sceneGraph = sceneMgr().sceneGraph();
	if (!isSceneMode()) {
		const voxel::Region &region = sceneGraph.resolveRegion(node);
		return glm::translate(region.getLowerCornerf());
	}
	keyFrameIdx = node.keyFrameForFrame(sceneMgr().currentFrame());
	const scenegraph::SceneGraphTransform &transform = node.transform(keyFrameIdx);
	return transform.worldMatrix();
}

uint32_t Viewport::gizmoMode() const {
	return ImGuizmo::MODE::WORLD;
}

void Viewport::updateBounds(const scenegraph::SceneGraphNode &node) {
	const scenegraph::SceneGraph &sceneGraph = sceneMgr().sceneGraph();
	const voxel::Region &region = sceneGraph.resolveRegion(node);
	_bounds.mins = region.getLowerCornerf();
	_bounds.maxs = region.getUpperCornerf() + 1.0f;
}

const float *Viewport::gizmoBounds(const scenegraph::SceneGraphNode &node) {
	const float *boundsPtr = nullptr;
	if (isSceneMode() && (_gizmoOperations->uintVal() & GizmoOperation_Bounds) != 0) {
		if (!ImGuizmo::IsUsing()) {
			updateBounds(node);
		}
		boundsPtr = glm::value_ptr(_bounds.mins);
	}
	return boundsPtr;
}

bool Viewport::gizmoManipulate(const video::Camera &camera, const float *boundsPtr, glm::mat4 &matrix,
							   glm::mat4 &deltaMatrix, uint32_t operation) const {
	static const float boundsSnap[] = {1.0f, 1.0f, 1.0f};
	float *mPtr = glm::value_ptr(matrix);
	float *dMatPtr = glm::value_ptr(deltaMatrix);
	const ImGuizmo::OPERATION op = (ImGuizmo::OPERATION)operation;
	const ImGuizmo::MODE mode = (ImGuizmo::MODE)gizmoMode();
	const float *vMatPtr = glm::value_ptr(camera.viewMatrix());
	const float *pMatPtr = glm::value_ptr(camera.projectionMatrix());
	const float step = core::Var::getSafe(cfg::VoxEditGridsize)->floatVal();
	const float snap[]{step, step, step};
	const float *snapPtr = _gizmoSnap->boolVal() ? snap : nullptr;
	return ImGuizmo::Manipulate(vMatPtr, pMatPtr, op, mode, mPtr, dMatPtr, snapPtr, boundsPtr, boundsSnap);
}

bool Viewport::runGizmo(const video::Camera &camera) {
	const scenegraph::SceneGraph &sceneGraph = sceneMgr().sceneGraph();
	int activeNode = sceneGraph.activeNode();
	if (activeNode == InvalidNodeId) {
		reset();
		return false;
	}
	const bool sceneMode = isSceneMode();
	scenegraph::SceneGraphNode &node = sceneGraph.node(activeNode);
	if (!sceneMode && !node.isModelNode()) {
		reset();
		return false;
	}

	if (!wantGizmo()) {
		return false;
	}

	scenegraph::KeyFrameIndex keyFrameIdx = InvalidKeyFrame;
	glm::mat4 matrix = gizmoMatrix(node, keyFrameIdx);
	glm::mat4 deltaMatrix(1.0f);
	const float *boundsPtr = gizmoBounds(node);
	const uint32_t operation = gizmoOperation(node);
	const bool manipulated = gizmoManipulate(camera, boundsPtr, matrix, deltaMatrix, operation);
	updateGizmoValues(node, keyFrameIdx, matrix);
	// check to create a reference before we update the node transform
	// otherwise the new reference node will not get the correct transform
	if (createReference(node)) {
		const int newNode = sceneMgr().nodeReference(node.id());
		// we need to activate the node - otherwise we end up in
		// endlessly creating new reference nodes
		if (sceneMgr().nodeActivate(newNode)) {
			activeNode = newNode;
		}
	}
	if (manipulated) {
		if (sceneMode) {
			if (_pivotMode->boolVal()) {
				const scenegraph::SceneGraphTransform &transform = node.transform(keyFrameIdx);
				const glm::vec3 size = node.region().getDimensionsInVoxels();
				const glm::vec3 pivot = (glm::vec3(matrix[3]) - transform.worldTranslation()) / size;
				sceneMgr().nodeUpdatePivot(activeNode, pivot);
			} else {
				sceneMgr().nodeUpdateTransform(activeNode, matrix, &deltaMatrix, keyFrameIdx, false);
			}
		} else {
			const glm::ivec3 shift = glm::vec3(matrix[3]) - node.region().getLowerCornerf();
			sceneMgr().shift(activeNode, shift);
			// only true in edit mode
			return true;
		}
	}
	return false;
}

void Viewport::renderCameraManipulator(video::Camera &camera, float headerSize) {
	if (isFixedCamera()) {
		return;
	}
	ImVec2 position = ImGui::GetWindowPos();
	const ImVec2 size = ImVec2(128, 128);
	const ImVec2 maxSize = ImGui::GetWindowContentRegionMax();
	position.x += maxSize.x - size.x;
	position.y += headerSize;
	const ImU32 backgroundColor = 0;
	const float length = camera.targetDistance();

	glm::mat4 viewMatrix = camera.viewMatrix();
	float *viewPtr = glm::value_ptr(viewMatrix);

	if (_renderContext.sceneMode) {
		ImGuizmo::ViewManipulate(viewPtr, length, position, size, backgroundColor);
	} else {
		const float *projPtr = glm::value_ptr(camera.projectionMatrix());
		const ImGuizmo::OPERATION operation = (ImGuizmo::OPERATION)0;
		glm::mat4 transformMatrix = glm::mat4(1.0f); // not used
		float *matrixPtr = glm::value_ptr(transformMatrix);
		const ImGuizmo::MODE mode = ImGuizmo::MODE::LOCAL;
		ImGuizmo::ViewManipulate(viewPtr, projPtr, operation, mode, matrixPtr, length, position, size, backgroundColor);
	}
	if (viewMatrix != camera.viewMatrix()) {
		glm::vec3 scale;
		glm::vec3 translation;
		glm::quat orientation;
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::decompose(viewMatrix, scale, orientation, translation, skew, perspective);
		camera.setOrientation(orientation);
	}
}

bool Viewport::renderGizmo(video::Camera &camera, float headerSize, const ImVec2 &size) {
	if (!_showAxisVar->boolVal()) {
		return false;
	}

	const bool orthographic = camera.mode() == video::CameraMode::Orthogonal;

	ImGuizmo::SetID(_id);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetWindow();
	const ImVec2 &windowPos = ImGui::GetWindowPos();
	ImGuizmo::Enable(_renderContext.sceneMode || _modelGizmo->boolVal());
	ImGuizmo::AllowAxisFlip(_gizmoAllowAxisFlip->boolVal());
	ImGuizmo::SetRect(windowPos.x, windowPos.y + headerSize, size.x, size.y);
	ImGuizmo::SetOrthographic(orthographic);
	const bool editModeModified = runGizmo(camera);
	renderCameraManipulator(camera, headerSize);
	return editModeModified;
}

void Viewport::renderToFrameBuffer() {
	core_trace_scoped(EditorSceneRenderFramebuffer);
	video::clearColor(core::Color::Clear());
	_renderContext.frameBuffer.bind(true);
	sceneMgr().render(_renderContext, camera());
	_renderContext.frameBuffer.unbind();
}

} // namespace voxedit
