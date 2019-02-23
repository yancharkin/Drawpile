/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2008-2019 Calle Laakkonen

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

#include "session.h"
#include "client.h"
#include "serverconfig.h"
#include "inmemoryhistory.h"
#include "serverlog.h"

#include "../net/control.h"
#include "../net/meta.h"
#include "../record/writer.h"
#include "../util/filename.h"
#include "../util/passwordhash.h"
#include "../util/networkaccess.h"

#include "config.h"

#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace server {

using protocol::MessagePtr;

Session::Session(SessionHistory *history, ServerConfig *config, QObject *parent)
	: QObject(parent),
	m_config(config),
	m_state(Initialization),
	m_initUser(-1),
	m_recorder(nullptr),
	m_history(history),
	m_resetstreamsize(0),
	m_closed(false),
	m_authOnly(false),
	m_autoResetRequestStatus(AutoResetState::NotSent)
{
	m_history->setParent(this);
	m_history->setSizeLimit(config->getConfigSize(config::SessionSizeLimit));
	m_history->setAutoResetThreshold(config->getConfigSize(config::AutoresetThreshold));

	m_lastEventTime.start();
	m_lastStatusUpdate.start();

	if(history->sizeInBytes()>0) {
		m_state = Running;

		// Reset history to match current state
		m_history->addMessage(protocol::MessagePtr(new protocol::SessionOwner(0, QList<uint8_t>())));
		sendUpdatedSessionProperties();
	}

	// Session announcements
	m_refreshTimer = new QTimer(this);
	m_refreshTimer->setSingleShot(true);
	m_refreshTimer->setTimerType(Qt::VeryCoarseTimer);
	connect(m_refreshTimer, &QTimer::timeout, this, &Session::refreshAnnouncements);

	for(const QString &announcement : m_history->announcements())
		makeAnnouncement(QUrl(announcement), false);
}

static protocol::MessagePtr makeLogMessage(const Log &log)
{
	protocol::ServerReply sr {
		protocol::ServerReply::LOG,
		log.message(),
		log.toJson(Log::NoPrivateData|Log::NoSession)
	};
	return protocol::MessagePtr(new protocol::Command(0, sr));
}

void Session::switchState(State newstate)
{
	if(newstate==Initialization) {
		qFatal("Illegal state change to Initialization from %d", m_state);

	} else if(newstate==Running) {
		if(m_state!=Initialization && m_state!=Reset)
			qFatal("Illegal state change to Running from %d", m_state);

		m_initUser = -1;
		bool success = true;

		if(m_state==Reset && !m_resetstream.isEmpty()) {
			// Reset buffer uploaded. Now perform the reset before returning to
			// normal running state.

			// Add list of currently logged in users to reset snapshot
			QList<uint8_t> owners;
			QList<uint8_t> trusted;
			for(const Client *c : m_clients) {
				m_resetstream.prepend(c->joinMessage());
				if(c->isOperator())
					owners << c->id();
				if(c->isTrusted())
					trusted << c->id();
			}
			if(!trusted.isEmpty())
				m_resetstream.prepend(protocol::MessagePtr(new protocol::TrustedUsers(0, trusted)));
			m_resetstream.prepend(protocol::MessagePtr(new protocol::SessionOwner(0, owners)));

			// Send reset snapshot
			if(!m_history->reset(m_resetstream)) {
				// This shouldn't normally happen, as the size limit should be caught while
				// still uploading the reset.
				messageAll("Session reset failed!", true);
				success = false;

			} else {
				protocol::ServerReply resetcmd;
				resetcmd.type = protocol::ServerReply::RESET;
				resetcmd.reply["state"] = "reset";
				resetcmd.message = "Session reset!";
				directToAll(MessagePtr(new protocol::Command(0, resetcmd)));

				protocol::ServerReply catchup;
				catchup.type = protocol::ServerReply::CATCHUP;
				catchup.reply["count"] = m_history->lastIndex() - m_history->firstIndex();
				directToAll(MessagePtr(new protocol::Command(0, catchup)));

				m_autoResetRequestStatus = AutoResetState::NotSent;

				sendUpdatedSessionProperties();
			}

			m_resetstream.clear();
			m_resetstreamsize = 0;
		}

		if(success && !m_recordingFile.isEmpty())
			restartRecording();

		for(Client *c : m_clients)
			c->enqueueHeldCommands();

	} else if(newstate==Reset) {
		if(m_state!=Running)
			qFatal("Illegal state change to Reset from %d", m_state);

		m_resetstream.clear();
		m_resetstreamsize = 0;
		messageAll("Preparing for session reset!", true);
	}

	m_state = newstate;
}

void Session::assignId(Client *user)
{
	uint8_t id = m_history->idQueue().getIdForName(user->username());

	int loops=256;
	while(loops>0 && (id==0 || getClientById(id))) {
		id = m_history->idQueue().nextId();
		  --loops;
	}
	Q_ASSERT(loops>0); // shouldn't happen, since we don't let new users in if the session is full
	user->setId(id);
}

void Session::joinUser(Client *user, bool host)
{
	user->setSession(this);
	m_clients.append(user);

	connect(user, &Client::loggedOff, this, &Session::removeUser);
	connect(history(), &SessionHistory::newMessagesAvailable, user, &Client::sendNextHistoryBatch);

	// Send session log history to the new client
	{
		QList<Log> log = m_config->logger()->query().session(id()).atleast(Log::Level::Info).get();
		// Note: the query returns the log entries in latest first, but we send
		// new entries to clients as they occur, so we reverse the list before sending it
		for(int i=log.size()-1;i>=0;--i) {
			user->sendDirectMessage(makeLogMessage(log.at(i)));
		}
	}

	if(host) {
		Q_ASSERT(m_state == Initialization);
		m_initUser = user->id();
	} else {
		// Notify the client how many messages to expect (at least)
		// The client can use this information to display a progress bar during the login phase
		protocol::ServerReply catchup;
		catchup.type = protocol::ServerReply::CATCHUP;
		catchup.reply["count"] = m_history->lastIndex() - m_history->firstIndex();
		user->sendDirectMessage(protocol::MessagePtr(new protocol::Command(0, catchup)));
	}

	const QString welcomeMessage = m_config->getConfigString(config::WelcomeMessage);
	if(!welcomeMessage.isEmpty()) {
		user->sendSystemChat(welcomeMessage);
	}

	addToHistory(user->joinMessage());

	if(user->isOperator() || m_history->isOperator(user->username()))
		changeOpStatus(user->id(), true, "the server");

	if(m_history->isTrusted(user->username()))
		changeTrustedStatus(user->id(), true, "the server");

	ensureOperatorExists();

	// Make sure everyone is up to date
	sendUpdatedAnnouncementList();
	sendUpdatedBanlist();
	sendUpdatedMuteList();

	m_history->idQueue().setIdForName(user->id(), user->username());

	user->log(Log().about(Log::Level::Info, Log::Topic::Join).message("Joined session"));
	emit userConnected(this, user);
}

void Session::removeUser(Client *user)
{
	if(!m_clients.removeOne(user))
		return;

	Q_ASSERT(user->session() == this);
	user->log(Log().about(Log::Level::Info, Log::Topic::Leave).message("Left session"));
	user->setSession(nullptr);

	disconnect(user, &Client::loggedOff, this, &Session::removeUser);
	disconnect(m_history, &SessionHistory::newMessagesAvailable, user, &Client::sendNextHistoryBatch);

	if(user->id() == m_initUser && m_state == Reset) {
		// Whoops, the resetter left before the job was done!
		// We simply cancel the reset in that case and go on
		abortReset();
	}

	addToHistory(MessagePtr(new protocol::UserLeave(user->id())));
	m_history->idQueue().reserveId(user->id()); // Try not to reuse the ID right away

	ensureOperatorExists();

	// Reopen the session when the last user leaves
	if(m_clients.isEmpty()) {
		setClosed(false);
	}

	historyCacheCleanup();

	emit userDisconnected(this);
}

void Session::abortReset()
{
	m_initUser = -1;
	m_resetstream.clear();
	m_resetstreamsize = 0;
	switchState(Running);
	messageAll("Session reset cancelled.", true);
}

Client *Session::getClientById(int id)
{
	for(Client *c : m_clients) {
		if(c->id() == id)
			return c;
	}
	return nullptr;
}

Client *Session::getClientByUsername(const QString &username)
{
	for(Client *c : m_clients) {
		if(c->username().compare(username, Qt::CaseInsensitive)==0)
			return c;
	}
	return nullptr;
}

void Session::addBan(const Client *target, const QString &bannedBy)
{
	Q_ASSERT(target);
	if(m_history->addBan(target->username(), target->peerAddress(), target->extAuthId(), bannedBy)) {
		target->log(Log().about(Log::Level::Info, Log::Topic::Ban).message("Banned by " + bannedBy));
		sendUpdatedBanlist();
	}
}

void Session::removeBan(int entryId, const QString &removedBy)
{
	QString unbanned = m_history->removeBan(entryId);
	if(!unbanned.isEmpty()) {
		log(Log().about(Log::Level::Info, Log::Topic::Unban).message(unbanned + " unbanned by " + removedBy));
		sendUpdatedBanlist();
	}
}

void Session::setClosed(bool closed)
{
	if(m_closed != closed) {
		m_closed = closed;
		sendUpdatedSessionProperties();
	}
}

void Session::setAuthOnly(bool authOnly)
{
	if(m_authOnly != authOnly) {
		m_authOnly = authOnly;
		sendUpdatedSessionProperties();
	}
}

// In Qt 5.7 we can just use Flags.setFlag(flag, true/false);
// Remove this once we can drop support for older Qt versions
template<class F, class Ff> static void setFlag(F &flags, Ff f, bool set)
{
	if(set)
		flags |= f;
	else
		flags &= ~f;
}

void Session::setSessionConfig(const QJsonObject &conf, Client *changedBy)
{
	QStringList changes;

	if(conf.contains("closed")) {
		m_closed = conf["closed"].toBool();
		changes << (m_closed ? "closed" : "opened");
	}

	if(conf.contains("authOnly")) {
		const bool authOnly = conf["authOnly"].toBool();
		// The authOnly flag can only be set by an authenticated user.
		// Otherwise it would be possible for users to accidentally lock themselves out.
		if(!authOnly || !changedBy || changedBy->isAuthenticated()) {
			m_authOnly = authOnly;
			changes << (authOnly ? "blocked guest logins" : "permitted guest logins");
		}
	}

	SessionHistory::Flags flags = m_history->flags();

	if(conf.contains("persistent")) {
		setFlag(flags, SessionHistory::Persistent, conf["persistent"].toBool() && m_config->getConfigBool(config::EnablePersistence));
		changes << (conf["persistent"].toBool() ? "made persistent" : "made nonpersistent");
	}

	if(conf.contains("title")) {
		m_history->setTitle(conf["title"].toString().mid(0, 100));
		changes << "changed title";
	}

	if(conf.contains("maxUserCount")) {
		m_history->setMaxUsers(conf["maxUserCount"].toInt());
		changes << "changed max. user count";
	}

	if(conf.contains("resetThreshold")) {
		m_history->setAutoResetThreshold(conf["resetThreshold"].toInt());
		changes << "changed autoreset threshold";
	}

	if(conf.contains("password")) {
		setPassword(conf["password"].toString());
		changes << "changed password";
	}

	if(conf.contains("opword")) {
		m_history->setOpwordHash(passwordhash::hash(conf["opword"].toString()));
		changes << "changed opword";
	}

	// Note: this bit is only relayed by the server: it informs
	// the client whether to send preserved/recorded chat messages
	// by default.
	if(conf.contains("preserveChat")) {
		setFlag(flags, SessionHistory::PreserveChat, conf["preserveChat"].toBool());
		changes << (conf["preserveChat"].toBool() ? "preserve chat" : "don't preserve chat");
	}

	if(conf.contains("nsfm")) {
		setFlag(flags, SessionHistory::Nsfm, conf["nsfm"].toBool());
		changes << (conf["nsfm"].toBool() ? "tagged NSFM" : "removed NSFM tag");
	}

	if(conf.contains("deputies")) {
		setFlag(flags, SessionHistory::Deputies, conf["deputies"].toBool());
		changes << (conf["deputies"].toBool() ? "enabled deputies" : "disabled deputies");
	}

	m_history->setFlags(flags);

	if(!changes.isEmpty()) {
		sendUpdatedSessionProperties();
		QString logmsg = changes.join(", ");
		logmsg[0] = logmsg[0].toUpper();

		Log l = Log().about(Log::Level::Info, Log::Topic::Status).message(logmsg);
		if(changedBy)
			changedBy->log(l);
		else
			log(l);
	}
}

bool Session::checkPassword(const QString &password) const
{
	return passwordhash::check(password, m_history->passwordHash());
}

QList<uint8_t> Session::updateOwnership(QList<uint8_t> ids, const QString &changedBy)
{
	QList<uint8_t> truelist;
	Client *kickResetter = nullptr;
	for(Client *c : m_clients) {
		const bool op = ids.contains(c->id()) | c->isModerator();
		if(op != c->isOperator()) {
			if(!op && c->id() == m_initUser && m_state == Reset) {
				// OP status removed mid-reset! The user probably has at least part
				// of the reset image still queued for upload, which will messs up
				// the session once we're out of reset mode. Kicking the client
				// is the easiest workaround.
				// TODO for 2.1: send a cancel command to the client and ignore
				// all further input until ack is received.
				kickResetter = c;
			}

			c->setOperator(op);
			QString msg;
			if(op) {
				msg = "Made operator by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Op).message(msg));
			} else {
				msg = "Operator status revoked by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Deop).message(msg));
			}
			messageAll(c->username() + " " + msg, false);
			if(c->isAuthenticated() && !c->isModerator())
				m_history->setAuthenticatedOperator(c->username(), op);

		}
		if(c->isOperator())
			truelist << c->id();
	}

	if(kickResetter)
		kickResetter->disconnectError("De-opped while resetting");

	return truelist;
}

void Session::changeOpStatus(int id, bool op, const QString &changedBy)
{
	QList<uint8_t> ids;
	Client *kickResetter = nullptr;

	for(Client *c : m_clients) {
		if(c->id() == id && c->isOperator() != op) {

			if(!op && c->id() == m_initUser && m_state == Reset) {
				// See above for explanation
				kickResetter = c;
			}

			c->setOperator(op);
			QString msg;
			if(op) {
				msg = "Made operator by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Op).message(msg));
			} else {
				msg = "Operator status revoked by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Deop).message(msg));
			}
			messageAll(c->username() + " " + msg, false);
			if(c->isAuthenticated() && !c->isModerator())
				m_history->setAuthenticatedOperator(c->username(), op);
		}

		if(c->isOperator())
			ids << c->id();
	}

	addToHistory(protocol::MessagePtr(new protocol::SessionOwner(0, ids)));

	if(kickResetter)
		kickResetter->disconnectError("De-opped while resetting");
}

QList<uint8_t> Session::updateTrustedUsers(QList<uint8_t> ids, const QString &changedBy)
{
	QList<uint8_t> truelist;
	for(Client *c : m_clients) {
		const bool trusted = ids.contains(c->id());
		if(trusted != c->isTrusted()) {
			c->setTrusted(trusted);
			QString msg;
			if(trusted) {
				msg = "Trusted by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Trust).message(msg));
			} else {
				msg = "Untrusted by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Untrust).message(msg));
			}
			messageAll(c->username() + " " + msg, false);
			if(c->isAuthenticated())
				m_history->setAuthenticatedTrust(c->username(), trusted);

		}
		if(c->isTrusted())
			truelist << c->id();
	}

	return truelist;
}

void Session::changeTrustedStatus(int id, bool trusted, const QString &changedBy)
{
	QList<uint8_t> ids;

	for(Client *c : m_clients) {
		if(c->id() == id && c->isTrusted() != trusted) {
			c->setTrusted(trusted);
			QString msg;
			if(trusted) {
				msg = "Trusted by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Trust).message(msg));
			} else {
				msg = "Untrusted by " + changedBy;
				c->log(Log().about(Log::Level::Info, Log::Topic::Untrust).message(msg));
			}
			messageAll(c->username() + " " + msg, false);
			if(c->isAuthenticated())
				m_history->setAuthenticatedTrust(c->username(), trusted);
		}

		if(c->isTrusted())
			ids << c->id();
	}

	addToHistory(protocol::MessagePtr(new protocol::TrustedUsers(0, ids)));
}

void Session::sendUpdatedSessionProperties()
{
	protocol::ServerReply props;
	props.type = protocol::ServerReply::SESSIONCONF;
	QJsonObject	conf;
	conf["closed"] = m_closed; // this refers specifically to the closed flag, not the general status
	conf["authOnly"] = m_authOnly;
	conf["persistent"] = isPersistent();
	conf["title"] = title();
	conf["maxUserCount"] = m_history->maxUsers();
	conf["resetThreshold"] = int(m_history->autoResetThreshold());
	conf["resetThresholdBase"] = int(m_history->autoResetThresholdBase());
	conf["preserveChat"] = m_history->flags().testFlag(SessionHistory::PreserveChat);
	conf["nsfm"] = m_history->flags().testFlag(SessionHistory::Nsfm);
	conf["deputies"] = m_history->flags().testFlag(SessionHistory::Deputies);
	conf["hasPassword"] = hasPassword();
	conf["hasOpword"] = hasOpword();
	props.reply["config"] = conf;

	addToHistory(protocol::MessagePtr(new protocol::Command(0, props)));
	emit sessionAttributeChanged(this);
}

void Session::sendUpdatedBanlist()
{
	// The banlist is not usually included in the sessionconf.
	// Moderators and local users get to see the actual IP addresses too
	protocol::ServerReply msg;
	msg.type = protocol::ServerReply::SESSIONCONF;
	QJsonObject conf;
	conf["banlist"] = banlist().toJson(false);
	msg.reply["config"] = conf;

	// Normal users don't get to see the actual IP addresses
	protocol::MessagePtr normalVersion(new protocol::Command(0, msg));

	// But moderators and local users do
	conf["banlist"] = banlist().toJson(true);
	msg.reply["config"] = conf;
	protocol::MessagePtr modVersion(new protocol::Command(0, msg));

	for(Client *c : m_clients) {
		if(c->isModerator() || c->peerAddress().isLoopback())
			c->sendDirectMessage(modVersion);
		else
			c->sendDirectMessage(normalVersion);
	}
}

void Session::sendUpdatedAnnouncementList()
{
	// The announcement list is not usually included in the sessionconf.
	protocol::ServerReply msg;
	msg.type = protocol::ServerReply::SESSIONCONF;
	QJsonArray list;
	for(const sessionlisting::Announcement &a : announcements()) {
		QJsonObject o;
		o["url"] = a.apiUrl.toString();
		o["roomcode"] = a.roomcode;
		o["private"] = a.isPrivate;
		list.append(o);
	}

	QJsonObject conf;
	conf["announcements"]= list;
	msg.reply["config"] = conf;
	directToAll(protocol::MessagePtr(new protocol::Command(0, msg)));
}

void Session::sendUpdatedMuteList()
{
	// The mute list is not usually included in the sessionconf.
	protocol::ServerReply msg;
	msg.type = protocol::ServerReply::SESSIONCONF;
	QJsonArray muted;
	for(const Client *c : m_clients) {
		if(c->isMuted())
			muted.append(c->id());
	}

	QJsonObject conf;
	conf["muted"]= muted;
	msg.reply["config"] = conf;
	directToAll(protocol::MessagePtr(new protocol::Command(0, msg)));
}

void Session::addToHistory(const protocol::MessagePtr &msg)
{
	if(m_state == Shutdown)
		return;

	// Add message to history (if there is space)
	if(!m_history->addMessage(msg)) {
		const Client *shame = getClientById(msg->contextId());
		messageAll("History size limit reached!", false);
		messageAll((shame ? shame->username() : QString("user #%1").arg(msg->contextId())) + " broke the camel's back. Session must be reset to continue drawing.", false);
		return;
	}


	// The hosting user must skip the history uploaded during initialization
	// (since they originated it), but we still want to send them notifications.
	if(m_state == Initialization) {
		Client *origin = getClientById(m_initUser);
		Q_ASSERT(origin);
		if(origin) {
			origin->setHistoryPosition(m_history->lastIndex());
			if(!msg->isCommand())
				origin->sendDirectMessage(msg);
		}
	}

	// Add message to recording
	if(m_recorder)
		m_recorder->recordMessage(msg);
	m_lastEventTime.start();

	// Request auto-reset when threshold is crossed.
	const uint autoResetThreshold = m_history->effectiveAutoResetThreshold();
	if(autoResetThreshold>0 && m_autoResetRequestStatus == AutoResetState::NotSent && m_history->sizeInBytes() > autoResetThreshold) {
		log(Log().about(Log::Level::Info, Log::Topic::Status).message(
			QString("Autoreset threshold (%1, effectively %2 MB) reached.")
				.arg(m_history->autoResetThreshold()/(1024.0*1024.0), 0, 'g', 1)
				.arg(autoResetThreshold/(1024.0*1024.0), 0, 'g', 1)
		));

		// Legacy alert for Drawpile 2.0.x versions
		protocol::ServerReply warning;
		warning.type = protocol::ServerReply::SIZELIMITWARNING;
		warning.reply["size"] = int(m_history->sizeInBytes());
		warning.reply["maxSize"] = int(autoResetThreshold);

		directToAll(protocol::MessagePtr(new protocol::Command(0, warning)));

		// New style for Drawpile 2.1.0 and newer
		// Autoreset request: send an autoreset query to each logged in operator.
		// The user that responds first gets to perform the reset.
		protocol::ServerReply resetRequest;
		resetRequest.type = protocol::ServerReply::RESETREQUEST;
		resetRequest.reply["maxSize"] = int(m_history->sizeLimit());
		resetRequest.reply["query"] = true;
		protocol::MessagePtr reqMsg { new protocol::Command(0, resetRequest )};

		for(Client *c : m_clients) {
			if(c->isOperator())
				c->sendDirectMessage(reqMsg);
		}

		m_autoResetRequestStatus = AutoResetState::Queried;
	}

	// Regular history size status updates
	if(m_lastStatusUpdate.elapsed() > 10 * 1000) {
		protocol::ServerReply status;
		status.type = protocol::ServerReply::STATUS;
		status.reply["size"] = int(m_history->sizeInBytes());
		directToAll(protocol::MessagePtr(new protocol::Command(0, status)));
		m_lastStatusUpdate.start();
	}
}

void Session::addToInitStream(protocol::MessagePtr msg)
{
	Q_ASSERT(m_state == Initialization || m_state == Reset || m_state == Shutdown);

	if(m_state == Initialization) {
		addToHistory(msg);

	} else if(m_state == Reset) {
		m_resetstreamsize += msg->length();
		m_resetstream.append(msg);

		// Well behaved clients should be aware of the history limit and not exceed it.
		if(m_history->sizeLimit()>0 && m_resetstreamsize > m_history->sizeLimit()) {
			Client *resetter = getClientById(m_initUser);
			if(resetter)
				resetter->disconnectError("History limit exceeded");
		}
	}
}

void Session::readyToAutoReset(int ctxId)
{
	Client *c = getClientById(ctxId);
	if(!c) {
		// Shouldn't happen
		log(Log().about(Log::Level::Error, Log::Topic::RuleBreak).message(QString("Non-existent user %1 sent ready-to-autoreset").arg(ctxId)));
		return;
	}

	if(!c->isOperator()) {
		// Unlikely to happen normally, but possible if connection is
		// really slow and user is deopped at just the right moment
		log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(QString("User %1 is not an operator, but sent ready-to-autoreset").arg(ctxId)));
		return;
	}

	if(m_autoResetRequestStatus != AutoResetState::Queried) {
		// Only the first response in handled
		log(Log().about(Log::Level::Debug, Log::Topic::Status).message(QString("User %1 was late to respond to an autoreset request").arg(ctxId)));
		return;
	}

	log(Log().about(Log::Level::Info, Log::Topic::Status).message(QString("User %1 responded to autoreset request first").arg(ctxId)));

	protocol::ServerReply resetRequest;
	resetRequest.type = protocol::ServerReply::RESETREQUEST;
	resetRequest.reply["maxSize"] = int(m_history->sizeLimit());
	resetRequest.reply["query"] = false;
	c->sendDirectMessage(protocol::MessagePtr { new protocol::Command(0, resetRequest )});

	m_autoResetRequestStatus = AutoResetState::Requested;
}

void Session::handleInitBegin(int ctxId)
{
	Client *c = getClientById(ctxId);
	if(!c) {
		// Shouldn't happen
		log(Log().about(Log::Level::Error, Log::Topic::RuleBreak).message(QString("Non-existent user %1 sent init-begin").arg(ctxId)));
		return;
	}

	if(ctxId != m_initUser) {
		c->log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(QString("Sent init-begin, but init user is #%1").arg(m_initUser)));
		return;
	}

	c->log(Log().about(Log::Level::Debug, Log::Topic::Status).message("init-begin"));

	// It's possible that regular non-reset commands were still in the upload buffer
	// when the client started sending the reset snapshot. The init-begin indicates
	// the start of the true reset snapshot, so we can clear out the buffer here.
	// For backward-compatibility, sending the init-begin command is optional.
	if(m_resetstreamsize>0) {
		c->log(Log().about(Log::Level::Debug, Log::Topic::Status).message(QStringLiteral("%1 extra messages cleared by init-begin").arg(m_resetstream.size())));
		m_resetstream.clear();
		m_resetstreamsize = 0;
	}
}

void Session::handleInitComplete(int ctxId)
{
	Client *c = getClientById(ctxId);
	if(!c) {
		// Shouldn't happen
		log(Log().about(Log::Level::Error, Log::Topic::RuleBreak).message(QString("Non-existent user %1 sent init-complete").arg(ctxId)));
		return;
	}

	if(ctxId != m_initUser) {
		c->log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(QString("Sent init-complete, but init user is #%1").arg(m_initUser)));
		return;
	}

	c->log(Log().about(Log::Level::Debug, Log::Topic::Status).message("init-complete"));

	switchState(Running);
}


void Session::handleInitCancel(int ctxId)
{
	Client *c = getClientById(ctxId);
	if(!c) {
		// Shouldn't happen
		log(Log().about(Log::Level::Error, Log::Topic::RuleBreak).message(QString("Non-existent user %1 sent init-complete").arg(ctxId)));
		return;
	}

	if(ctxId != m_initUser) {
		c->log(Log().about(Log::Level::Warn, Log::Topic::RuleBreak).message(QString("Sent init-cancel, but init user is #%1").arg(m_initUser)));
		return;
	}

	c->log(Log().about(Log::Level::Debug, Log::Topic::Status).message("init-cancel"));
	abortReset();
}

void Session::resetSession(int resetter)
{
	Q_ASSERT(m_state == Running);
	Q_ASSERT(getClientById(resetter));

	m_initUser = resetter;
	switchState(Reset);

	protocol::ServerReply resetRequest;
	resetRequest.type = protocol::ServerReply::RESET;
	resetRequest.reply["state"] = "init";
	resetRequest.message = "Prepared to receive session data";

	getClientById(resetter)->sendDirectMessage(protocol::MessagePtr(new protocol::Command(0, resetRequest)));
}

void Session::killSession(bool terminate)
{
	if(m_state == Shutdown)
		return;

	switchState(Shutdown);
	unlistAnnouncement("*", false);
	stopRecording();

	for(Client *c : m_clients) {
		c->disconnectShutdown();
		c->setSession(nullptr);
	}
	m_clients.clear();

	if(terminate)
		m_history->terminate();

	this->deleteLater();
}

void Session::directToAll(protocol::MessagePtr msg)
{
	for(Client *c : m_clients) {
		c->sendDirectMessage(msg);
	}
}

void Session::messageAll(const QString &message, bool alert)
{
	if(message.isEmpty())
		return;

	directToAll(protocol::MessagePtr(new protocol::Command(0,
		(protocol::ServerReply {
			alert ? protocol::ServerReply::ALERT : protocol::ServerReply::MESSAGE,
			message,
			QJsonObject()
		}).toJson()))
	);
}

void Session::ensureOperatorExists()
{
	// If there is a way to gain OP status without being explicitly granted,
	// it's OK for the session to not have any operators for a while.
	if(!m_history->opwordHash().isEmpty() || m_history->isAuthenticatedOperators())
		return;

	bool hasOp=false;
	for(const Client *c : m_clients) {
		if(c->isOperator()) {
			hasOp=true;
			break;
		}
	}

	if(!hasOp && !m_clients.isEmpty()) {
		changeOpStatus(m_clients.first()->id(), true, "the server");
	}
}

void Session::restartRecording()
{
	if(m_recorder) {
		m_recorder->close();
		delete m_recorder;
	}

	// Start recording
	QString filename = utils::makeFilenameUnique(m_recordingFile, ".dprec");
	qDebug("Starting session recording %s", qPrintable(filename));

	m_recorder = new recording::Writer(filename, this);
	if(!m_recorder->open()) {
		qWarning("Couldn't write session recording to %s: %s", qPrintable(filename), qPrintable(m_recorder->errorString()));
		delete m_recorder;
		m_recorder = nullptr;
		return;
	}

	QJsonObject metadata;
	metadata["server-recording"] = true;
	metadata["version"] = m_history->protocolVersion().asString();

	m_recorder->writeHeader(metadata);
	m_recorder->setAutoflush();

	int lastBatchIndex=0;
	do {
		QList<protocol::MessagePtr> history;
		std::tie(history, lastBatchIndex) = m_history->getBatch(lastBatchIndex);
		for(const MessagePtr &m : history)
			m_recorder->recordMessage(m);

	} while(lastBatchIndex<m_history->lastIndex());
}

void Session::stopRecording()
{
	if(m_recorder) {
		m_recorder->close();
		delete m_recorder;
		m_recorder = nullptr;
	}
}

QString Session::uptime() const
{
	qint64 up = (QDateTime::currentMSecsSinceEpoch() - m_history->startTime().toMSecsSinceEpoch()) / 1000;

	int days = up / (60*60*24);
	up -= days * (60*60*24);

	int hours = up / (60*60);
	up -= hours * (60*60);

	int minutes = up / 60;

	QString uptime;
	if(days==1)
		uptime = "one day, ";
	else if(days>1)
		uptime = QString::number(days) + " days, ";

	if(hours==1)
		uptime += "1 hour and ";
	else
		uptime += QString::number(hours) + " hours and ";

	if(minutes==1)
		uptime += "1 minute";
	else
		uptime += QString::number(minutes) + " minutes.";

	return uptime;
}

QStringList Session::userNames() const
{
	QStringList lst;
	for(const Client *c : m_clients)
		lst << c->username();
	return lst;
}

void Session::makeAnnouncement(const QUrl &url, bool privateListing)
{
	if(!url.isValid() || !m_config->isAllowedAnnouncementUrl(url)) {
		log(Log().about(Log::Level::Warn, Log::Topic::PubList).message("Announcement API URL not allowed: " + url.toString()));
		return;
	}

	// Don't announce twice at the same server
	for(sessionlisting::Announcement &a : m_publicListings) {
		if(a.apiUrl == url) {
			// Refresh announcement if privacy type was changed
			if(a.isPrivate != privateListing) {
				a.isPrivate = privateListing;
				sendUpdatedAnnouncementList();
				Q_ASSERT(m_refreshTimer);
				if(m_refreshTimer)
					m_refreshTimer->start(0);
			}
			return;
		}
	}

	const bool privateUserList = m_config->getConfigBool(config::PrivateUserList);

	const sessionlisting::Session s {
		m_config->internalConfig().localHostname,
		m_config->internalConfig().getAnnouncePort(),
		aliasOrId(),
		protocolVersion(),
		title(),
		userCount(),
		(hasPassword() || privateUserList) ? QStringList() : userNames(),
		hasPassword(),
		isNsfm(),
		privateListing ? sessionlisting::PrivacyMode::Private : sessionlisting::PrivacyMode::Public,
		founder(),
		sessionStartTime()
	};

	const QString apiUrl = url.toString();
	log(Log().about(Log::Level::Info, Log::Topic::PubList).message("Announcing session at at " + apiUrl));
	auto *response = sessionlisting::announceSession(url, s);

	connect(response, &sessionlisting::AnnouncementApiResponse::finished, this, [apiUrl, response, this](const QVariant &result, const QString &message, const QString &error) {
		response->deleteLater();
		if(!error.isEmpty()) {
			log(Log().about(Log::Level::Warn, Log::Topic::PubList).message(apiUrl + ": announcement failed: " + error));
			messageAll(error, false);
			return;
		}

		if(!message.isEmpty()) {
			log(Log().about(Log::Level::Info, Log::Topic::PubList).message(message));
			messageAll(message, false);
		}

		const sessionlisting::Announcement announcement = result.value<sessionlisting::Announcement>();

		// Make sure there are no double announcements
		for(const sessionlisting::Announcement &a : m_publicListings) {
			if(a.apiUrl == announcement.apiUrl) {
				log(Log().about(Log::Level::Warn, Log::Topic::PubList).message("Double announcement at: " + announcement.apiUrl.toString()));
				return;
			}
		}

		log(Log().about(Log::Level::Info, Log::Topic::PubList).message("Announced at: " + announcement.apiUrl.toString()));
		if(!announcement.isPrivate)
			m_history->addAnnouncement(announcement.apiUrl.toString());
		m_publicListings << announcement;
		sendUpdatedAnnouncementList();

		int timeout = announcement.refreshInterval * 60 * 1000;
		if(!m_refreshTimer->isActive() || m_refreshTimer->remainingTime() > timeout)
			m_refreshTimer->start(timeout);
	});
}

void Session::unlistAnnouncement(const QString &url, bool terminate, bool removeOnly)
{
	QMutableListIterator<sessionlisting::Announcement> i = m_publicListings;
	bool changed = false;
	while(i.hasNext()) {
		const sessionlisting::Announcement &a = i.next();
		if(a.apiUrl == url || url == QStringLiteral("*")) {
			if(!removeOnly) {
				log(Log().about(Log::Level::Info, Log::Topic::PubList).message(QStringLiteral("Unlisting announcement at ") + url));

				auto *response = sessionlisting::unlistSession(a);
				connect(response, &sessionlisting::AnnouncementApiResponse::finished, this, [response, this](const QVariant &result, const QString &message, const QString &error) {
					Q_UNUSED(result);
					Q_UNUSED(message);
					response->deleteLater();
					if(!error.isEmpty()) {
						log(Log().about(Log::Level::Warn, Log::Topic::PubList).message("Session unlisting failed"));
					}
				});
			}

			if(terminate)
				m_history->removeAnnouncement(a.apiUrl.toString());

			i.remove();
			changed = true;
		}
	}

	if(changed)
		sendUpdatedAnnouncementList();
}

void Session::refreshAnnouncements()
{
	const bool privateUserList = m_config->getConfigBool(config::PrivateUserList);
	int timeout = 0;

	for(const sessionlisting::Announcement &a : m_publicListings) {
		auto *response = sessionlisting::refreshSession(a, {
			QString(), // cannot change
			0, // cannot change
			QString(), // cannot change
			protocol::ProtocolVersion(), // cannot change
			title(),
			userCount(),
			hasPassword() || privateUserList ? QStringList() : userNames(),
			hasPassword(),
			isNsfm(),
			a.isPrivate ? sessionlisting::PrivacyMode::Private : sessionlisting::PrivacyMode::Public,
			founder(),
			sessionStartTime()
		});
		timeout = qMax(timeout, a.refreshInterval);

		const QString apiUrl = a.apiUrl.toString();

		connect(response, &sessionlisting::AnnouncementApiResponse::finished, [this, response, apiUrl](const QVariant &result, const QString &message, const QString &error) {
			Q_UNUSED(result);

			if(!message.isEmpty()) {
				log(Log().about(Log::Level::Info, Log::Topic::PubList).message(message));
				this->messageAll(message, false);
			}

			response->deleteLater();
			if(!error.isEmpty()) {
				// Remove listing on error
				log(Log().about(Log::Level::Warn, Log::Topic::PubList).message(apiUrl + ": announcement error: " + error));
				unlistAnnouncement(apiUrl, true, true);
				this->messageAll(error, false);
			}
		});
	}

	if(timeout > 0) {
		m_refreshTimer->start(timeout * 60 * 1000);
	}
}

void Session::historyCacheCleanup()
{
	int minIdx = m_history->lastIndex();
	for(const Client *c : m_clients) {
		minIdx = qMin(c->historyPosition(), minIdx);
	}
	m_history->cleanupBatches(minIdx);
}

void Session::sendAbuseReport(const Client *reporter, int aboutUser, const QString &message)
{
	Q_ASSERT(reporter);

	reporter->log(Log().about(Log::Level::Info, Log::Topic::Status).message(QString("Abuse report about user %1 received: %2").arg(aboutUser).arg(message)));

	const QUrl url = m_config->internalConfig().reportUrl;
	if(!url.isValid()) {
		// This shouldn't happen normally. If the URL is not configured,
		// the server does not advertise the capability to receive reports.
		log(Log().about(Log::Level::Warn, Log::Topic::Status).message("Cannot send abuse report: server URL not configured!"));
		return;
	}

	QJsonObject o;
	o["session"] = idString();
	o["sessionTitle"] = title();
	o["user"] = reporter->username();
	o["auth"] = reporter->isAuthenticated();
	o["ip"] = reporter->peerAddress().toString();
	if(aboutUser>0)
		o["perp"] = aboutUser;

	o["message"] = message;
	o["offset"] = int(m_history->sizeInBytes());
	QJsonArray users;
	for(const Client *c : m_clients) {
		QJsonObject u;
		u["name"] = c->username();
		u["auth"] = c->isAuthenticated();
		u["op"] = c->isOperator();
		u["ip"] = c->peerAddress().toString();
		u["id"] = c->id();
		users.append(u);
	}
	o["users"] = users;

	const QString authToken = m_config->getConfigString(config::ReportToken);

	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if(!authToken.isEmpty())
		req.setRawHeader("Authorization", "Token " + authToken.toUtf8());
	QNetworkReply *reply = networkaccess::getInstance()->post(req, QJsonDocument(o).toJson());
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if(reply->error() != QNetworkReply::NoError) {
			log(Log().about(Log::Level::Warn, Log::Topic::Status).message("Unable to send abuse report: " + reply->errorString()));
		}
	});
	connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

QJsonObject Session::getDescription(bool full) const
{
	// The basic description contains just the information
	// needed for the login session listing
	QJsonObject o {
		{"id", idString()},
		{"alias", idAlias()},
		{"protocol", protocolVersion().asString()},
		{"userCount", userCount()},
		{"maxUserCount", maxUsers()},
		{"founder", founder()},
		{"title", title()},
		{"hasPassword", hasPassword()},
		{"closed", isClosed()},
		{"authOnly", isAuthOnly()},
		{"nsfm", isNsfm()},
		{"startTime", sessionStartTime().toUTC().toString(Qt::ISODate)},
		{"size", int(m_history->sizeInBytes())}
	};

	if(m_config->getConfigBool(config::EnablePersistence))
		o["persistent"] = isPersistent();

	if(full) {
		// Full descriptions includes detailed info for server admins.
		o["maxSize"] = int(m_history->sizeLimit());
		o["resetThreshold"] = int(m_history->autoResetThreshold());
		o["deputies"] = m_history->flags().testFlag(SessionHistory::Deputies);

		QJsonArray users;
		for(const Client *user : m_clients) {
			users << user->description(false);
		}
		o["users"] = users;

		QJsonArray listings;
		for(const sessionlisting::Announcement &a : m_publicListings) {
			listings << QJsonObject {
				{"id", a.listingId},
				{"url", a.apiUrl.toString()},
				{"roomcode", a.roomcode},
				{"private", a.isPrivate}
			};
		}
		o["listings"] = listings;
	}

	return o;
}

JsonApiResult Session::callJsonApi(JsonApiMethod method, const QStringList &path, const QJsonObject &request)
{
	if(!path.isEmpty()) {
		QString head;
		QStringList tail;
		std::tie(head, tail) = popApiPath(path);

		if(head == "listing")
			return callListingsJsonApi(method, tail, request);

		int userId = head.toInt();
		if(userId>0) {
			Client *c = getClientById(userId);
			if(c)
				return c->callJsonApi(method, tail, request);
		}

		return JsonApiNotFound();
	}

	if(method == JsonApiMethod::Update) {
		setSessionConfig(request, nullptr);

		if(request.contains("message"))
			messageAll(request["message"].toString(), false);
		if(request.contains("alert"))
			messageAll(request["alert"].toString(), true);

	} else if(method == JsonApiMethod::Delete) {
		killSession();
		return JsonApiResult{ JsonApiResult::Ok, QJsonDocument(QJsonObject{ { "status", "ok "} }) };
	}

	return JsonApiResult{JsonApiResult::Ok, QJsonDocument(getDescription(true))};
}

JsonApiResult Session::callListingsJsonApi(JsonApiMethod method, const QStringList &path, const QJsonObject &request)
{
	Q_UNUSED(request);
	if(path.length() != 1)
		return JsonApiNotFound();
	const int id = path.at(0).toInt();

	for(const sessionlisting::Announcement &a : m_publicListings) {
		if(a.listingId == id) {
			if(method == JsonApiMethod::Delete) {
				unlistAnnouncement(a.apiUrl.toString());
				return JsonApiResult{ JsonApiResult::Ok, QJsonDocument(QJsonObject{ { "status", "ok "} }) };

			} else {
				return JsonApiBadMethod();
			}
		}
	}
	return JsonApiNotFound();
}

void Session::log(const Log &log)
{
	Log entry = log;
	entry.session(id());
	m_config->logger()->logMessage(entry);

	if(entry.level() < Log::Level::Debug) {
		directToAll(makeLogMessage(entry));
	}
}

}

