/**
 * @file
 */

#include "User.h"
#include "core/Var.h"
#include "attrib/Attributes.h"
#include "backend/world/Map.h"
#include "network/ServerMessageSender.h"

namespace backend {

User::User(ENetPeer* peer, EntityId id,
		const std::string& name,
		const MapPtr& map,
		const network::ServerMessageSenderPtr& messageSender,
		const core::TimeProviderPtr& timeProvider,
		const attrib::ContainerProviderPtr& containerProvider,
		const cooldown::CooldownProviderPtr& cooldownProvider,
		const persistence::DBHandlerPtr& dbHandler,
		const stock::StockProviderPtr& stockDataProvider) :
		Super(id, map, messageSender, timeProvider, containerProvider),
		_name(name), _dbHandler(dbHandler), _stockMgr(this, stockDataProvider, dbHandler),
		_timeProvider(timeProvider), _cooldownProvider(cooldownProvider),
		_cooldownMgr(this, timeProvider, cooldownProvider, dbHandler, messageSender), _attribMgr(id, _attribs, dbHandler) {
	setPeer(peer);
	_entityType = network::EntityType::PLAYER;
	_userTimeout = core::Var::getSafe(cfg::ServerUserTimeout);
}

User::~User() {
}

void User::init() {
	Super::init();
	_stockMgr.init();
	_cooldownMgr.init();
	_attribMgr.init();
}

void User::shutdown() {
	Super::shutdown();
	_stockMgr.shutdown();
	_cooldownMgr.shutdown();
	_attribMgr.shutdown();
}

ENetPeer* User::setPeer(ENetPeer* peer) {
	ENetPeer* old = _peer;
	_peer = peer;
	if (_peer) {
		_peer->data = this;
	}
	return old;
}

void User::updateLastActionTime() {
	_lastAction = _time;
}

void User::triggerLogout() {
	_cooldownMgr.triggerCooldown(cooldown::Type::LOGOUT);
}

void User::reconnect() {
	Log::trace("reconnect user");
	_attribs.markAsDirty();
	visitVisible([&] (const EntityPtr& e) {
		sendEntitySpawn(e);
	});
}

bool User::update(long dt) {
	if (_disconnect) {
		return false;
	}
	_time += dt;
	if (!Super::update(dt)) {
		return false;
	}

	if (_time - _lastAction > _userTimeout->ulongVal()) {
		triggerLogout();
	}

	_stockMgr.update(dt);
	_cooldownMgr.update();

	if (!isMove(network::MoveDirection::ANY)) {
		return true;
	}

	_lastAction = _time;

	glm::vec3 moveDelta {0.0f};
	const float speed = current(attrib::Type::SPEED) * static_cast<float>(dt) / 1000.0f;
	if (isMove(network::MoveDirection::MOVELEFT)) {
		moveDelta += glm::left * speed;
	} else if (isMove(network::MoveDirection::MOVERIGHT)) {
		moveDelta += glm::right * speed;
	}
	if (isMove(network::MoveDirection::MOVEFORWARD)) {
		moveDelta += glm::forward * speed;
	} else if (isMove(network::MoveDirection::MOVEBACKWARD)) {
		moveDelta += glm::backward * speed;
	}

	_pos += glm::quat(glm::vec3(orientation(), _yaw, 0.0f)) * moveDelta;
	// TODO: if not flying...
	_pos.y = _map->findFloor(_pos);
	Log::trace("move: dt %li, speed: %f p(%f:%f:%f), pitch: %f, yaw: %f", dt, speed, _pos.x, _pos.y, _pos.z, orientation(), _yaw);

	const network::Vec3 pos { _pos.x, _pos.y, _pos.z };
	sendToVisible(_entityUpdateFBB,
			network::ServerMsgType::EntityUpdate,
			network::CreateEntityUpdate(_entityUpdateFBB, id(), &pos, orientation()).Union(), true);

	return true;
}

void User::sendSeed(long seed) const {
	flatbuffers::FlatBufferBuilder fbb;
	_messageSender->sendServerMessage(_peer, fbb, network::ServerMsgType::Seed, network::CreateSeed(fbb, seed).Union());
}

void User::sendUserSpawn() const {
	flatbuffers::FlatBufferBuilder fbb;
	const network::Vec3 pos { _pos.x, _pos.y, _pos.z };
	sendToVisible(fbb, network::ServerMsgType::UserSpawn, network::CreateUserSpawn(fbb, id(), fbb.CreateString(_name), &pos).Union(), true);
}

}
