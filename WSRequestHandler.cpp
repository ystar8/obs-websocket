/*
obs-websocket
Copyright (C) 2016	Stéphane Lepin <stephane.lepin@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "WSRequestHandler.h"
#include "obs-websocket.h"
#include "Config.h"
#include "Utils.h"
#include <QMainWindow>

WSRequestHandler::WSRequestHandler(QWebSocket *client) :
	_authenticated(false),
	_messageId(0),
	_requestType(""),
	_requestData(nullptr)
{
	_client = client;

	messageMap["GetVersion"] = WSRequestHandler::HandleGetVersion;
	messageMap["GetAuthRequired"] = WSRequestHandler::HandleGetAuthRequired;
	messageMap["Authenticate"] = WSRequestHandler::HandleAuthenticate;

	messageMap["SetCurrentScene"] = WSRequestHandler::HandleSetCurrentScene;
	messageMap["GetCurrentScene"] = WSRequestHandler::HandleGetCurrentScene;
	messageMap["GetSceneList"] = WSRequestHandler::HandleGetSceneList;

	messageMap["SetSourceRender"] = WSRequestHandler::HandleSetSourceRender;
	messageMap["SetSceneItemRender"] = WSRequestHandler::HandleSetSourceRender;
	messageMap["SetSceneItemPosition"] = WSRequestHandler::HandleSetSceneItemPosition;
	messageMap["SetSceneItemTransform"] = WSRequestHandler::HandleSetSceneItemTransform;

	messageMap["GetStreamingStatus"] = WSRequestHandler::HandleGetStreamingStatus;
	messageMap["StartStopStreaming"] = WSRequestHandler::HandleStartStopStreaming;
	messageMap["StartStopRecording"] = WSRequestHandler::HandleStartStopRecording;

	messageMap["GetTransitionList"] = WSRequestHandler::HandleGetTransitionList;
	messageMap["GetCurrentTransition"] = WSRequestHandler::HandleGetCurrentTransition;
	messageMap["SetCurrentTransition"] = WSRequestHandler::HandleSetCurrentTransition;

	messageMap["SetVolume"] = WSRequestHandler::HandleSetVolume;
	messageMap["GetVolume"] = WSRequestHandler::HandleGetVolume;
	messageMap["ToggleMute"] = WSRequestHandler::ToggleMute;
	messageMap["SetMute"] = WSRequestHandler::SetMute;

	//my self
	messageMap["SetCurrentProfile"] = WSRequestHandler::HandleSetCurrentProfile;
	messageMap["GetCurrentProfile"] = WSRequestHandler::HandleGetCurrentProfile;
	messageMap["UpdateService"] = WSRequestHandler::HandleUpdateService;
	messageMap["StartStreaming"] = WSRequestHandler::HandleStartStreaming;
	messageMap["StopStreaming"] = WSRequestHandler::HandleStopStreaming;
	messageMap["UpdateSourceString"] = WSRequestHandler::HandleUpdateSourceString;
	messageMap["UpdateSourceInt"] = WSRequestHandler::HandleUpdateSourceInt;
	messageMap["UpdateSourceBool"] = WSRequestHandler::HandleUpdateSourceBool;
	messageMap["UpdateSourceDouble"] = WSRequestHandler::HandleUpdateSourceDouble;
	
	authNotRequired.insert("GetVersion");
	authNotRequired.insert("GetAuthRequired");
	authNotRequired.insert("Authenticate");

	QByteArray client_ip = _client->peerAddress().toString().toUtf8();
	blog(LOG_INFO, "[obs-websockets] new client connection from %s:%d", client_ip.constData(), _client->peerPort());

	connect(_client, &QWebSocket::textMessageReceived, this, &WSRequestHandler::processTextMessage);
	connect(_client, &QWebSocket::disconnected, this, &WSRequestHandler::socketDisconnected);
}

void WSRequestHandler::processTextMessage(QString textMessage) {
	QByteArray msgData = textMessage.toUtf8();
	const char *msg = msgData;

	_requestData = obs_data_create_from_json(msg);
	if (!_requestData) {
		if (!msg) {
			msg = "<null pointer>";
		}

		blog(LOG_ERROR, "[obs-websockets] invalid JSON payload received for '%s'", msg);
		SendErrorResponse("invalid JSON payload");
		return;
	}

	_requestType = obs_data_get_string(_requestData, "request-type");
	_messageId = obs_data_get_string(_requestData, "message-id");

	if (Config::Current()->AuthRequired 
		&& !_authenticated 
		&& authNotRequired.find(_requestType) == authNotRequired.end()) 
	{
		SendErrorResponse("Not Authenticated");
		return;
	}

	void (*handlerFunc)(WSRequestHandler*) = (messageMap[_requestType]);

	if (handlerFunc != NULL) {
		handlerFunc(this);
	}
	else {
		SendErrorResponse("invalid request type");
	}
	
	obs_data_release(_requestData);
}

void WSRequestHandler::socketDisconnected() {
	QByteArray client_ip = _client->peerAddress().toString().toUtf8();
	blog(LOG_INFO, "[obs-websockets] client %s:%d disconnected", client_ip.constData(), _client->peerPort());

	_authenticated = false;
	_client->deleteLater();
	emit disconnected();
}

void WSRequestHandler::sendTextMessage(QString textMessage) {
	_client->sendTextMessage(textMessage);
}

bool WSRequestHandler::isAuthenticated() {
	return _authenticated;
}

WSRequestHandler::~WSRequestHandler() {
	if (_requestData != NULL) {
		obs_data_release(_requestData);
	}
}

void WSRequestHandler::SendOKResponse(obs_data_t *additionalFields) {
	obs_data_t *response = obs_data_create();
	obs_data_set_string(response, "status", "ok");
	obs_data_set_string(response, "message-id", _messageId);

	if (additionalFields != NULL) {
		obs_data_apply(response, additionalFields);
	}

	_client->sendTextMessage(obs_data_get_json(response));

	obs_data_release(response);
}

void WSRequestHandler::SendErrorResponse(const char *errorMessage) {
	obs_data_t *response = obs_data_create();
	obs_data_set_string(response, "status", "error");
	obs_data_set_string(response, "error", errorMessage);
	obs_data_set_string(response, "message-id", _messageId);

	_client->sendTextMessage(obs_data_get_json(response));

	obs_data_release(response);
}

void WSRequestHandler::HandleGetVersion(WSRequestHandler *owner) {
	obs_data_t *data = obs_data_create();
	obs_data_set_double(data, "version", 1.1);
	obs_data_set_string(data, "obs-websocket-version", OBS_WEBSOCKET_VERSION);
	//obs_data_set_string(data, "obs-studio-version", OBS_VERSION); // Wrong

	owner->SendOKResponse(data);

	obs_data_release(data);
}

void WSRequestHandler::HandleGetAuthRequired(WSRequestHandler *owner) {
	bool authRequired = Config::Current()->AuthRequired;

	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "authRequired", authRequired);

	if (authRequired) {
		obs_data_set_string(data, "challenge", Config::Current()->SessionChallenge);
		obs_data_set_string(data, "salt", Config::Current()->Salt);
	}

	owner->SendOKResponse(data);

	obs_data_release(data);
}

void WSRequestHandler::HandleAuthenticate(WSRequestHandler *owner) {
	const char *auth = obs_data_get_string(owner->_requestData, "auth");
	if (!auth || strlen(auth) < 1) {
		owner->SendErrorResponse("auth not specified!");
		return;
	}

	if (!(owner->_authenticated) && Config::Current()->CheckAuth(auth)) {
		owner->_authenticated = true;
		owner->SendOKResponse();
	}
	else {
		owner->SendErrorResponse("Authentication Failed.");
	}
}

void WSRequestHandler::HandleSetCurrentScene(WSRequestHandler *owner) {
	const char *sceneName = obs_data_get_string(owner->_requestData, "scene-name");
	obs_source_t *source = obs_get_source_by_name(sceneName);

	if (source) {
		obs_frontend_set_current_scene(source);
		owner->SendOKResponse();
	}
	else {
		owner->SendErrorResponse("requested scene does not exist");
	}

	obs_source_release(source);
}

void WSRequestHandler::HandleGetCurrentScene(WSRequestHandler *owner) {
	obs_source_t *current_scene = obs_frontend_get_current_scene();
	const char *name = obs_source_get_name(current_scene);

	obs_data_array_t *scene_items = Utils::GetSceneItems(current_scene);

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "name", name);
	obs_data_set_array(data, "sources", scene_items);

	owner->SendOKResponse(data);

	obs_data_release(data);
	obs_data_array_release(scene_items);
	obs_source_release(current_scene);
}

void WSRequestHandler::HandleGetSceneList(WSRequestHandler *owner) {
	obs_source_t *current_scene = obs_frontend_get_current_scene();
	obs_data_array_t *scenes = Utils::GetScenes();

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "current-scene", obs_source_get_name(current_scene));
	obs_data_set_array(data, "scenes", scenes);

	owner->SendOKResponse(data);

	obs_data_release(data);
	obs_data_array_release(scenes);
	obs_source_release(current_scene);
}

void WSRequestHandler::HandleSetSourceRender(WSRequestHandler *owner) {
	const char *itemName = obs_data_get_string(owner->_requestData, "source");
	bool isVisible = obs_data_get_bool(owner->_requestData, "render");
	if (itemName == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_t* currentScene = obs_frontend_get_current_scene();
	
	obs_sceneitem_t *sceneItem = Utils::GetSceneItemFromName(currentScene, itemName);
	if (sceneItem != NULL) {
		obs_sceneitem_set_visible(sceneItem, isVisible);
		obs_sceneitem_release(sceneItem);
		owner->SendOKResponse();
	}
	else {
		owner->SendErrorResponse("specified scene item doesn't exist");
	}

	obs_source_release(currentScene);
}

void WSRequestHandler::HandleSetSceneItemPosition(WSRequestHandler *owner)
{
	const char *item_name = obs_data_get_string(owner->_requestData, "item");
	if (!item_name)
	{
		owner->SendErrorResponse("invalid request parameters");
	}

	vec2 item_position = { 0 };
	item_position.x = obs_data_get_double(owner->_requestData, "x");
	item_position.y = obs_data_get_double(owner->_requestData, "y");

	obs_source_t* current_scene = obs_frontend_get_current_scene();
	obs_sceneitem_t *scene_item = Utils::GetSceneItemFromName(current_scene, item_name);

	if (scene_item)
	{
		obs_sceneitem_set_pos(scene_item, &item_position);

		obs_sceneitem_release(scene_item);
		owner->SendOKResponse();
	}
	else
	{
		owner->SendErrorResponse("specified scene item doesn't exist");
	}

	obs_source_release(current_scene);
}

void WSRequestHandler::HandleSetSceneItemTransform(WSRequestHandler *owner)
{
	const char *item_name = obs_data_get_string(owner->_requestData, "item");
	if (!item_name)
	{
		owner->SendErrorResponse("invalid request parameters");
	}

	vec2 scale;
	scale.x = obs_data_get_double(owner->_requestData, "x-scale");
	scale.y = obs_data_get_double(owner->_requestData, "y-scale");

	float rotation = obs_data_get_double(owner->_requestData, "rotation");

	obs_source_t* current_scene = obs_frontend_get_current_scene();
	obs_sceneitem_t *scene_item = Utils::GetSceneItemFromName(current_scene, item_name);

	if (scene_item)
	{
		obs_sceneitem_set_scale(scene_item, &scale);
		obs_sceneitem_set_rot(scene_item, rotation);

		obs_sceneitem_release(scene_item);
		owner->SendOKResponse();
	}
	else
	{
		owner->SendErrorResponse("specified scene item doesn't exist");
	}

	obs_source_release(current_scene);
}

void WSRequestHandler::HandleGetStreamingStatus(WSRequestHandler *owner) {
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "streaming", obs_frontend_streaming_active());
	obs_data_set_bool(data, "recording", obs_frontend_recording_active());
	obs_data_set_bool(data, "preview-only", false);

	owner->SendOKResponse(data);
	obs_data_release(data);
}

void WSRequestHandler::HandleStartStopStreaming(WSRequestHandler *owner) {
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
	}
	else {
		obs_frontend_streaming_start();
	}

	owner->SendOKResponse();
}

void WSRequestHandler::HandleStartStopRecording(WSRequestHandler *owner) {
	if (obs_frontend_recording_active()) {
		obs_frontend_recording_stop();
	}
	else {
		obs_frontend_recording_start();
	}

	owner->SendOKResponse();
}

void WSRequestHandler::HandleGetTransitionList(WSRequestHandler *owner) {
	obs_source_t *current_transition = obs_frontend_get_current_transition();	
	obs_frontend_source_list transitionList = {};
	obs_frontend_get_transitions(&transitionList);

	obs_data_array_t* transitions = obs_data_array_create();
	for (size_t i = 0; i < transitionList.sources.num; i++) {
		obs_source_t* transition = transitionList.sources.array[i];
		
		obs_data_t *obj = obs_data_create();
		obs_data_set_string(obj, "name", obs_source_get_name(transition));
		
		obs_data_array_push_back(transitions, obj);
		obs_data_release(obj);
	}
	obs_frontend_source_list_free(&transitionList);

	obs_data_t *response = obs_data_create();
	obs_data_set_string(response, "current-transition", obs_source_get_name(current_transition));
	obs_data_set_array(response, "transitions", transitions);

	owner->SendOKResponse(response);

	obs_data_release(response);
	obs_data_array_release(transitions);
	obs_source_release(current_transition);
}

void WSRequestHandler::HandleGetCurrentTransition(WSRequestHandler *owner) {
	obs_source_t *current_transition = obs_frontend_get_current_transition();

	obs_data_t *response = obs_data_create();
	obs_data_set_string(response, "name", obs_source_get_name(current_transition));

	owner->SendOKResponse(response);

	obs_data_release(response);
	obs_source_release(current_transition);
}

void WSRequestHandler::HandleSetCurrentTransition(WSRequestHandler *owner) {
	const char *name = obs_data_get_string(owner->_requestData, "transition-name");
	obs_source_t *transition = Utils::GetTransitionFromName(name);

	if (transition) {
		obs_frontend_set_current_transition(transition);
		owner->SendOKResponse();

		obs_source_release(transition);
	}
	else {
		owner->SendErrorResponse("requested transition does not exist");
	}
}

void WSRequestHandler::HandleSetVolume(WSRequestHandler *owner) {
	const char *item_name = obs_data_get_string(owner->_requestData, "source");
	float item_volume = obs_data_get_double(owner->_requestData, "volume");

	if (item_name == NULL || item_volume < 0.0 || item_volume > 1.0) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_t* item = obs_get_source_by_name(item_name);
	if (!item) {
		owner->SendErrorResponse("specified source doesn't exist");
		return;
	}
	obs_source_set_volume(item, item_volume);
	owner->SendOKResponse();

	obs_source_release(item);
}

void WSRequestHandler::HandleGetVolume(WSRequestHandler *owner) {
	const char *item_name = obs_data_get_string(owner->_requestData, "source");
	if (item_name == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_t* item = obs_get_source_by_name(item_name);
	
	obs_data_t* response = obs_data_create();
	obs_data_set_string(response, "name", item_name);
	obs_data_set_double(response, "volume", obs_source_get_volume(item));

	owner->SendOKResponse(response);

	obs_data_release(response);
	obs_source_release(item);
}
void WSRequestHandler::ToggleMute(WSRequestHandler *owner) {
	const char *item_name = obs_data_get_string(owner->_requestData, "source");
	if (item_name == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_t* item = obs_get_source_by_name(item_name);
	if (!item) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_set_muted(item, !obs_source_muted(item));
	owner->SendOKResponse();

	obs_source_release(item);
}

void WSRequestHandler::SetMute(WSRequestHandler *owner) {
	const char *item_name = obs_data_get_string(owner->_requestData, "source");
	bool mute = obs_data_get_bool(owner->_requestData, "mute");
	if (item_name == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}

	obs_source_t* item = obs_get_source_by_name(item_name);
	if (!item) {
		owner->SendErrorResponse("specified source doesn't exist");
		return;
	}

	obs_source_set_muted(item, mute);
	owner->SendOKResponse();

	obs_source_release(item);
}

//my self 
void WSRequestHandler::HandleSetCurrentProfile(WSRequestHandler *owner) {
	const char *profileName = obs_data_get_string(owner->_requestData, "profile-name");
	obs_frontend_set_current_profile(profileName);
	owner->SendOKResponse();
}

void WSRequestHandler::HandleGetCurrentProfile(WSRequestHandler *owner) {
	const char *current_profile = obs_frontend_get_current_profile();
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "profile-name", current_profile);
	owner->SendOKResponse(data);
	obs_data_release(data);
}

void WSRequestHandler::HandleStartStreaming(WSRequestHandler *owner) {
	if (!obs_frontend_streaming_active()) {
		obs_frontend_streaming_start();
	}
	owner->SendOKResponse();
}

void WSRequestHandler::HandleStopStreaming(WSRequestHandler *owner) {
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
	}
	owner->SendOKResponse();
}

void WSRequestHandler::HandleUpdateService(WSRequestHandler *owner) {
	const char *key = obs_data_get_string(owner->_requestData, "key");
	const char *server = obs_data_get_string(owner->_requestData, "server");
	if (key == NULL || server ==NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}
	obs_service_t *service = obs_get_service_by_name("default_service");
	obs_data_t *settings = obs_service_get_settings(service);
	obs_data_set_string(settings, "key", key);
	obs_data_set_string(settings, "server", server);
	obs_service_update(service, settings);
	//char full_path[512];
	//config_t *conf = obs_frontend_get_profile_config();
	//char *ver_file = obs_module_config_path("version.ini");
	//obs_data_save_json_safe(settings, full_path,"tmp", "bak");
	obs_service_release(service);
	obs_data_release(settings);
	owner->SendOKResponse();
}

void WSRequestHandler::HandleUpdateSourceString(WSRequestHandler *owner) {
	const char *source_name = obs_data_get_string(owner->_requestData, "source-name");
	const char *source_setting_key = obs_data_get_string(owner->_requestData, "source-setting-key");
	const char *source_setting_val = obs_data_get_string(owner->_requestData, "source-setting-val");
	if (source_name == NULL || source_setting_key ==NULL || source_setting_val == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}
	obs_source_t* item = obs_get_source_by_name(source_name);
	obs_data_t *settings = obs_source_get_settings(item);
	obs_data_set_string(settings, source_setting_key, source_setting_val);
	obs_source_update(item, settings);
	obs_data_release(settings);
	obs_source_release(item);
}

void WSRequestHandler::HandleUpdateSourceInt(WSRequestHandler *owner) {
	const char *source_name = obs_data_get_string(owner->_requestData, "source-name");
	const char *source_setting_key = obs_data_get_string(owner->_requestData, "source-setting-key");
	int source_setting_val = obs_data_get_int(owner->_requestData, "source-setting-val");
	if (source_name == NULL || source_setting_key == NULL || source_setting_val == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}
	obs_source_t* item = obs_get_source_by_name(source_name);
	obs_data_t *settings = obs_source_get_settings(item);
	obs_data_set_int(settings, source_setting_key, source_setting_val);
	obs_source_update(item, settings);
	obs_data_release(settings);
	obs_source_release(item);
}

void WSRequestHandler::HandleUpdateSourceBool(WSRequestHandler *owner) {
	const char *source_name = obs_data_get_string(owner->_requestData, "source-name");
	const char *source_setting_key = obs_data_get_string(owner->_requestData, "source-setting-key");
	bool source_setting_val = obs_data_get_bool(owner->_requestData, "source-setting-val");
	if (source_name == NULL || source_setting_key == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}
	obs_source_t* item = obs_get_source_by_name(source_name);
	obs_data_t *settings = obs_source_get_settings(item);
	obs_data_set_bool(settings, source_setting_key, source_setting_val);
	obs_source_update(item, settings);
	obs_data_release(settings);
	obs_source_release(item);
}
void WSRequestHandler::HandleUpdateSourceDouble(WSRequestHandler *owner) {
	const char *source_name = obs_data_get_string(owner->_requestData, "source-name");
	const char *source_setting_key = obs_data_get_string(owner->_requestData, "source-setting-key");
	double source_setting_val = obs_data_get_double(owner->_requestData, "source-setting-val");
	if (source_name == NULL || source_setting_key == NULL || source_setting_val == NULL) {
		owner->SendErrorResponse("invalid request parameters");
		return;
	}
	obs_source_t* item = obs_get_source_by_name(source_name);
	obs_data_t *settings = obs_source_get_settings(item);
	obs_data_set_double(settings, source_setting_key, source_setting_val);
	obs_source_update(item, settings);
	obs_data_release(settings);
	obs_source_release(item);
}

void WSRequestHandler::ErrNotImplemented(WSRequestHandler *owner) {
	owner->SendErrorResponse("not implemented");
}
