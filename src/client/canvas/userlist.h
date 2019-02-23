/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2007-2017 Calle Laakkonen

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
#ifndef DP_NET_USERLISTMODEL_H
#define DP_NET_USERLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QPixmap>

class QJsonArray;

namespace protocol { class MessagePtr; }

namespace canvas {

/**
 * @brief Information about a user
 */
struct User {
	int id;
	QString name;
	QPixmap avatar;
	bool isLocal;
	bool isOperator;
	bool isTrusted;
	bool isMod;
	bool isBot;
	bool isAuth;
	bool isLocked;
	bool isMuted;
};

/**
 * A list model to represent session users.
 */
class UserListModel : public QAbstractListModel {
	Q_OBJECT
public:
	enum UserListRoles {
		IdRole = Qt::UserRole + 1,
		NameRole,
		AvatarRole,
		IsOpRole,
		IsTrustedRole,
		IsModRole,
		IsAuthRole,
		IsBotRole,
		IsLockedRole,
		IsMutedRole
	};

	UserListModel(QObject *parent=nullptr);

	QVariant data(const QModelIndex& index, int role=Qt::DisplayRole) const;
	int rowCount(const QModelIndex& parent=QModelIndex()) const;

	void addUser(const User &user);
	void removeUser(int id);
	void clearUsers();

	/**
	 * @brief Get user info by ID
	 *
	 * This will return info about past users as well.
	 * @param id user id
	 * @return
	 */
	User getUserById(int id) const;

	/**
	 * @brief Get the name of the user with the given context ID
	 *
	 * If no such user exists, "User #X" is returned, where X is the ID number.
	 * @param id
	 * @return user name
	 */
	QString getUsername(int id) const;

	//! Get a list of users with operator privileges
	QList<uint8_t> operatorList() const;

	//! Get a list of users who are locked
	QList<uint8_t> lockList() const;

	//! Get a list of trusted users
	QList<uint8_t> trustedList() const;

	//! Get the ID of the operator with the lowest ID number
	// TODO replace this by serverside auto-resetter selection
	int getPrimeOp() const;

	/**
	 * @brief Get the command for (un)locking a single user
	 * @param localId
	 * @param userId
	 * @param lock
	 * @return
	 */
	protocol::MessagePtr getLockUserCommand(int localId, int userId, bool lock) const;

	/**
	 * @brief Get the command for granting or revoking operator privileges
	 * @param localId
	 * @param userId
	 * @param op
	 * @return
	 */
	protocol::MessagePtr getOpUserCommand(int localId, int userId, bool op) const;

	/**
	 * @brief Get the command for granting or revoking trusted status
	 * @param localId
	 * @param userId
	 * @param trusted
	 * @return
	 */
	protocol::MessagePtr getTrustUserCommand(int localId, int userId, bool op) const;

public slots:
	void updateOperators(const QList<uint8_t> operatorIds);
	void updateTrustedUsers(const QList<uint8_t> trustedIds);
	void updateLocks(const QList<uint8_t> lockedUserIds);
	void updateMuteList(const QJsonArray &mutedUserIds);

private:
	QVector<User> m_users;
	QHash<int,User> m_pastUsers;
};

}

Q_DECLARE_METATYPE(canvas::User)

#endif

