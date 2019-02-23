/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2007-2018 Calle Laakkonen

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

#include "userlist.h"
#include "../shared/net/meta.h"
#include "../shared/net/meta2.h"

#include <QDebug>
#include <QJsonArray>
#include <QPixmap>

namespace canvas {

UserListModel::UserListModel(QObject *parent)
	: QAbstractListModel(parent)
{
}


QVariant UserListModel::data(const QModelIndex& index, int role) const
{
	if(index.isValid() && index.row() >= 0 && index.row() < m_users.size()) {
		const User &u = m_users.at(index.row());
		switch(role) {
			case IdRole: return u.id;
			case Qt::DisplayRole:
			case NameRole: return u.name;
			case Qt::DecorationRole:
			case AvatarRole: return u.avatar;
			case IsOpRole: return u.isOperator;
			case IsTrustedRole: return u.isTrusted;
			case IsModRole: return u.isMod;
			case IsAuthRole: return u.isAuth;
			case IsBotRole: return u.isBot;
			case IsLockedRole: return u.isLocked;
			case IsMutedRole: return u.isMuted;
		}
	}

	return QVariant();
}

int UserListModel::rowCount(const QModelIndex& parent) const
{
	if(parent.isValid())
		return 0;
	return m_users.count();
}

void UserListModel::addUser(const User &user)
{
	// Check that the user doesn't exist already
	for(int i=0;i<m_users.count();++i) {
		User &u = m_users[i];
		if(u.id == user.id) {
			qWarning() << "replacing user" << u.id << u.name << "with" << user.name;
			u.name = user.name;
			u.avatar = user.avatar;
			u.isLocal = user.isLocal;
			u.isAuth = user.isAuth;
			u.isMod = user.isMod;
			u.isBot = user.isBot;
			u.isMuted = user.isMuted;

			QModelIndex idx = index(i);
			emit dataChanged(idx, idx);
			return;
		}
	}

	int pos = m_users.count();
	beginInsertRows(QModelIndex(),pos,pos);
	m_users.append(user);
	endInsertRows();
}

void UserListModel::updateOperators(const QList<uint8_t> ids)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool op = ids.contains(u.id);
		if(op != u.isOperator) {
			u.isOperator = op;
			QModelIndex idx = index(i);
			emit dataChanged(idx, idx);
		}
	}
}

void UserListModel::updateTrustedUsers(const QList<uint8_t> trustedIds)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool trusted = trustedIds.contains(u.id);
		if(trusted != u.isTrusted) {
			u.isTrusted = trusted;
			QModelIndex idx = index(i);
			emit dataChanged(idx, idx);
		}
	}
}

void UserListModel::updateLocks(const QList<uint8_t> ids)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool lock = ids.contains(u.id);
		if(lock != u.isLocked) {
			u.isLocked = lock;
			QModelIndex idx = index(i);
			emit dataChanged(idx, idx);
		}
	}
}

void UserListModel::updateMuteList(const QJsonArray &mutedUserIds)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];
		const bool mute = mutedUserIds.contains(u.id);
		if(u.isMuted != mute) {
			u.isMuted = mute;
			QModelIndex idx = index(i);
			emit dataChanged(idx, idx);
		}
	}
}

QList<uint8_t> UserListModel::operatorList() const
{
	QList<uint8_t> ops;
	for(int i=0;i<m_users.size();++i) {
		if(m_users.at(i).isOperator || m_users.at(i).isMod)
			ops << m_users.at(i).id;
	}
	return ops;
}

QList<uint8_t> UserListModel::lockList() const
{
	QList<uint8_t> locks;
	for(int i=0;i<m_users.size();++i) {
		if(m_users.at(i).isLocked)
			locks << m_users.at(i).id;
	}
	return locks;
}

QList<uint8_t> UserListModel::trustedList() const
{
	QList<uint8_t> ids;
	for(int i=0;i<m_users.size();++i) {
		if(m_users.at(i).isTrusted)
			ids << m_users.at(i).id;
	}
	return ids;
}

int UserListModel::getPrimeOp() const
{
	int lowest = 255;
	for(const User &u : m_users) {
		if(u.isOperator && u.id < lowest)
			lowest = u.id;
	}
	return lowest;
}

void UserListModel::removeUser(int id)
{
	for(int pos=0;pos<m_users.count();++pos) {
		if(m_users.at(pos).id == id) {
			beginRemoveRows(QModelIndex(),pos,pos);
			User u = m_users[pos];
			m_users.remove(pos);
			endRemoveRows();
			m_pastUsers[u.id] = u;
			return;
		}
	}
}

void UserListModel::clearUsers()
{
	beginRemoveRows(QModelIndex(), 0, m_users.count()-1);
	for(const User &u : m_users)
		m_pastUsers[u.id] = u;
	m_users.clear();
	endRemoveRows();
}

User UserListModel::getUserById(int id) const
{
	// Try active users first
	for(const User &u : m_users)
		if(u.id == id)
			return u;

	// Then the past users
	if(m_pastUsers.contains(id))
		return m_pastUsers[id];

	// Nothing found
	return User();
}

QString UserListModel::getUsername(int id) const
{
	// Special case: id 0 is reserved for the server
	if(id==0)
		return tr("Server");

	// Try active users first
	for(const User &u : m_users)
		if(u.id == id)
			return u.name;

	// Then the past users
	if(m_pastUsers.contains(id))
		return m_pastUsers[id].name;

	// Not found
	return tr("User #%1").arg(id);
}

protocol::MessagePtr UserListModel::getLockUserCommand(int localId, int userId, bool lock) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> ids = lockList();
	if(lock) {
		if(!ids.contains(userId))
			ids.append(userId);
	} else {
		ids.removeAll(userId);
	}

	return protocol::MessagePtr(new protocol::UserACL(localId, ids));
}

protocol::MessagePtr UserListModel::getOpUserCommand(int localId, int userId, bool op) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> ops = operatorList();
	if(op) {
		if(!ops.contains(userId))
			ops.append(userId);
	} else {
		ops.removeOne(userId);
	}

	return protocol::MessagePtr(new protocol::SessionOwner(localId, ops));
}

protocol::MessagePtr UserListModel::getTrustUserCommand(int localId, int userId, bool trust) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> trusted = trustedList();
	if(trust) {
		if(!trusted.contains(userId))
			trusted.append(userId);
	} else {
		trusted.removeOne(userId);
	}

	return protocol::MessagePtr(new protocol::TrustedUsers(localId, trusted));
}

}
