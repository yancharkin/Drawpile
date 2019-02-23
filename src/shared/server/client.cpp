/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2013-2018 Calle Laakkonen

   Drawpile is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Drawpile is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Drawpile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "client.h"
#include "session.h"
#include "sessionhistory.h"
#include "opcommands.h"
#include "serverlog.h"
#include "serverconfig.h"

#include "../net/messagequeue.h"
#include "../net/control.h"
#include "../net/meta.h"

#include <QSslSocket>
#include <QStringList>
#include <QPointer>

namespace server {

using protocol::MessagePtr;

struct Client::Private {
	QPointer<Session> session;
	QTcpSocket *socket;
	ServerLog *logger;

	protocol::MessageQueue *msgqueue;
	QList<MessagePtr> holdqueue;
	int historyPosition;

	int id;
	QString username;
	QString extAuthId;
	QByteArray avatar;

	bool isOperator;
	bool isModerator;
	bool isTrusted;
	bool isAuthenticated;
	bool isMuted;

	Private(QTcpSocket *socket, ServerLog *logger)
		: socket(socket), logger(logger), msgqueue(nullptr),
		historyPosition(-1), id(0),
		isOperator(false), isModerator(false), isTrusted(false), isAuthenticated(false), isMuted(false)
	{
		Q_ASSERT(socket);
		Q_ASSERT(logger);
	}
};

Client::Client(QTcpSocket *socket, ServerLog *logger, QObject *parent)
	: QObject(parent), d(new Private(socket, logger))
{
	d->msgqueue = new protocol::MessageQueue(socket, this);
	d->socket->setParent(this);

	connect(d->socket, &QAbstractSocket::disconnected, this, &Client::socketDisconnect);
	connect(d->socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
	connect(d->msgqueue, &protocol::MessageQueue::messageAvailable, this, &Client::receiveMessages);
	connect(d->msgqueue, &protocol::MessageQueue::badData, this, &Client::gotBadData);
}

Client::~Client()
{
	delete d;
}

protocol::MessagePtr Client::joinMessage() const
{
	return protocol::MessagePtr(new protocol::UserJoin(
			id(),
			(isAuthenticated() ? protocol::UserJoin::FLAG_AUTH : 0) | (isModerator() ? protocol::UserJoin::FLAG_MOD : 0),
			username(),
			avatar()
	));
}

QJsonObject Client::description(bool includeSession) const
{
	QJsonObject u;
	u["id"] = id();
	u["name"] = username();
	u["ip"] = peerAddress().toString();
	u["auth"] = isAuthenticated();
	u["op"] = isOperator();
	u["muted"] = isMuted();
	u["mod"] = isModerator();
	u["tls"] = isSecure();
	if(includeSession && d->session)
		u["session"] = d->session->idString();
	return u;
}

JsonApiResult Client::callJsonApi(JsonApiMethod method, const QStringList &path, const QJsonObject &request)
{
	if(!path.isEmpty())
		return JsonApiNotFound();

	if(method == JsonApiMethod::Delete) {
		disconnectKick("server operator");
		QJsonObject o;
		o["status"] = "ok";
		return JsonApiResult{JsonApiResult::Ok, QJsonDocument(o)};

	} else if(method == JsonApiMethod::Update) {
		QString msg = request["message"].toString();
		if(!msg.isEmpty())
			sendSystemChat(msg);

		if(request.contains("op")) {
			const bool op = request["op"].toBool();
			if(d->isOperator != op && d->session) {
				d->session->changeOpStatus(id(), op, "the server administrator");
			}
		}
		return JsonApiResult { JsonApiResult::Ok, QJsonDocument(description()) };

	} else if(method == JsonApiMethod::Get) {
		return JsonApiResult { JsonApiResult::Ok, QJsonDocument(description()) };

	} else {
		return JsonApiBadMethod();
	}
}

void Client::setSession(Session *session)
{
	d->session = session;
	d->historyPosition = -1;

	// Enqueue the next batch (if available) when upload queue is empty
	if(session)
		connect(d->msgqueue, &protocol::MessageQueue::allSent, this, &Client::sendNextHistoryBatch);
	else
		disconnect(d->msgqueue, &protocol::MessageQueue::allSent, this, &Client::sendNextHistoryBatch);
}

Session *Client::session()
{
	return d->session.data();
}

void Client::setId(int id)
{
	Q_ASSERT(d->id==0 && id != 0); // ID is only assigned ance
	d->id = id;
}

int Client::id() const
{
	return d->id;
}

void Client::setUsername(const QString &username)
{
	d->username = username;
}

const QString &Client::username() const
{
	return d->username;
}

void Client::setAvatar(const QByteArray &avatar)
{
	d->avatar = avatar;
}

const QByteArray &Client::avatar() const
{
	return d->avatar;
}

const QString &Client::extAuthId() const
{
	return d->extAuthId;
}

void Client::setExtAuthId(const QString &extAuthId)
{
	d->extAuthId = extAuthId;
}

void Client::setOperator(bool op)
{
	d->isOperator = op;
}

bool Client::isOperator() const
{
	return d->isOperator || d->isModerator;
}

bool Client::isDeputy() const
{
	return !isOperator() && isTrusted() && d->session && d->session->isDeputies();
}

void Client::setModerator(bool mod)
{
	d->isModerator = mod;
}

bool Client::isModerator() const
{
	return d->isModerator;
}

bool Client::isTrusted() const
{
	return d->isTrusted;
}

void Client::setTrusted(bool trusted)
{
	d->isTrusted = trusted;
}

void Client::setAuthenticated(bool auth)
{
	d->isAuthenticated = auth;
}

bool Client::isAuthenticated() const
{
	return d->isAuthenticated;
}

void Client::setMuted(bool m)
{
	d->isMuted = m;
}

bool Client::isMuted() const
{
	return d->isMuted;
}

int Client::historyPosition() const
{
	return d->historyPosition;
}

void Client::setHistoryPosition(int newpos)
{
	d->historyPosition = newpos;
}

void Client::setConnectionTimeout(int timeout)
{
	d->msgqueue->setIdleTimeout(timeout);
}

#ifndef NDEBUG
void Client::setRandomLag(uint lag)
{
	d->msgqueue->setRandomLag(lag);
}
#endif

QHostAddress Client::peerAddress() const
{
	return d->socket->peerAddress();
}

void Client::sendNextHistoryBatch()
{
	// Only enqueue messages for uploading when upload queue is empty
	// and session is in a normal running state.
	// (We'll get another messagesAvailable signal when ready)
	if(d->session == nullptr || d->msgqueue->isUploading() || d->session->state() != Session::Running)
		return;

	d->session->historyCacheCleanup();

	QList<protocol::MessagePtr> batch;
	int batchLast;
	std::tie(batch, batchLast) = d->session->history()->getBatch(d->historyPosition);
	d->historyPosition = batchLast;
	d->msgqueue->send(batch);
}

void Client::sendDirectMessage(protocol::MessagePtr msg)
{
	d->msgqueue->send(msg);
}

void Client::sendSystemChat(const QString &message)
{
	protocol::ServerReply msg {
		protocol::ServerReply::MESSAGE,
		message,
		QJsonObject()
	};

	d->msgqueue->send(MessagePtr(new protocol::Command(0, msg.toJson())));
}

void Client::receiveMessages()
{
	while(d->msgqueue->isPending()) {
		MessagePtr msg = d->msgqueue->getPending();

		if(d->session == nullptr) {
			// No session? We must be in the login phase
			if(msg->type() == protocol::MSG_COMMAND)
				emit loginMessage(msg);
			else
				log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(
					QString("Got non-login message (type=%1) in login state").arg(msg->type())
					));

		} else {
			handleSessionMessage(msg);
		}
	}
}

void Client::gotBadData(int len, int type)
{
	log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(
		QString("Received unknown message type %1 of length %2").arg(type).arg(len)
		));
	d->socket->abort();
}

void Client::socketError(QAbstractSocket::SocketError error)
{
	if(error != QAbstractSocket::RemoteHostClosedError) {
		log(Log().about(Log::Level::Warn, Log::Topic::Status).message("Socket error: " + d->socket->errorString()));
		d->socket->abort();
	}
}

void Client::socketDisconnect()
{
	emit loggedOff(this);
	this->deleteLater();
}

/**
 * @brief Handle messages in normal session mode
 *
 * This one is pretty simple. The message is validated to make sure
 * the client is authorized to send it, etc. and it is added to the
 * main message stream, from which it is distributed to all connected clients.
 * @param msg the message received from the client
 */
void Client::handleSessionMessage(MessagePtr msg)
{
	Q_ASSERT(d->session);

	// Filter away server-to-client-only messages
	switch(msg->type()) {
	using namespace protocol;
	case MSG_USER_JOIN:
	case MSG_USER_LEAVE:
	case MSG_SOFTRESET:
		log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message("Received server-to-user only command " + msg->messageName()));
		return;
	case MSG_DISCONNECT:
		// we don't do anything with disconnect notifications from the client
		return;
	default: break;
	}

	// Enforce origin context ID (except when uploading a snapshot)
	if(d->session->initUserId() != d->id)
		msg->setContextId(d->id);

	// Some meta commands affect the server too
	switch(msg->type()) {
		case protocol::MSG_COMMAND: {
			protocol::ServerCommand cmd = msg.cast<protocol::Command>().cmd();
			handleClientServerCommand(this, cmd.cmd, cmd.args, cmd.kwargs);
			return;
		}
		case protocol::MSG_SESSION_OWNER: {
			if(!isOperator()) {
				log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message("Tried to change session ownership"));
				return;
			}

			QList<uint8_t> ids = msg.cast<protocol::SessionOwner>().ids();
			ids.append(d->id);
			ids = d->session->updateOwnership(ids, username());
			msg.cast<protocol::SessionOwner>().setIds(ids);
			break;
		}
		case protocol::MSG_CHAT: {
			if(isMuted())
				return;
			if(msg.cast<protocol::Chat>().isBypass()) {
				d->session->directToAll(msg);
				return;
			}
			break;
		}
		case protocol::MSG_PRIVATE_CHAT: {
			const protocol::PrivateChat &chat = msg.cast<protocol::PrivateChat>();
			if(chat.target()>0) {
				Client *c = d->session->getClientById(chat.target());
				if(c) {
					this->sendDirectMessage(msg);
					c->sendDirectMessage(msg);
				}
			}
			return;
		}
		case protocol::MSG_TRUSTED_USERS: {
			if(!isOperator()) {
				log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message("Tried to change trusted user list"));
				return;
			}

			QList<uint8_t> ids = msg.cast<protocol::TrustedUsers>().ids();
			ids = d->session->updateTrustedUsers(ids, username());
			msg.cast<protocol::TrustedUsers>().setIds(ids);
			break;
	}
		default: break;
	}

	// Rest of the messages are added to session history
	if(d->session->initUserId() == d->id)
		d->session->addToInitStream(msg);
	else if(isHoldLocked()) {
		if(!d->session->history()->isOutOfSpace())
			d->holdqueue.append(msg);
	}
	else
		d->session->addToHistory(msg);
}

void Client::disconnectKick(const QString &kickedBy)
{
	log(Log().about(Log::Level::Info, Log::Topic::Kick).message("Kicked by " + kickedBy));
	emit loggedOff(this);
	d->msgqueue->sendDisconnect(protocol::Disconnect::KICK, kickedBy);
}

void Client::disconnectError(const QString &message)
{
	emit loggedOff(this);
	log(Log().about(Log::Level::Warn, Log::Topic::Leave).message("Disconnected due to error: " + message));
	d->msgqueue->sendDisconnect(protocol::Disconnect::ERROR, message);
}

void Client::disconnectShutdown()
{
	emit loggedOff(this);
	d->msgqueue->sendDisconnect(protocol::Disconnect::SHUTDOWN, QString());
}

bool Client::isHoldLocked() const
{
	Q_ASSERT(d->session);

	return d->session->state() != Session::Running;
}

void Client::enqueueHeldCommands()
{
	if(!d->session || isHoldLocked())
		return;

	for(MessagePtr msg : d->holdqueue)
		d->session->addToHistory(msg);
	d->holdqueue.clear();
}

bool Client::hasSslSupport() const
{
	return d->socket->inherits("QSslSocket");
}

bool Client::isSecure() const
{
	QSslSocket *socket = qobject_cast<QSslSocket*>(d->socket);
	return socket && socket->isEncrypted();
}

void Client::startTls()
{
	QSslSocket *socket = qobject_cast<QSslSocket*>(d->socket);
	Q_ASSERT(socket);
	socket->startServerEncryption();
}

void Client::log(Log entry) const
{
	entry.user(d->id, d->socket->peerAddress(), d->username);
	if(d->session)
		d->session->log(entry);
	else
		d->logger->logMessage(entry);
}

}

