/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2015-2018 Calle Laakkonen

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
#ifndef SESSIONLISTINGMODEL_H
#define SESSIONLISTINGMODEL_H

#include "../../shared/util/announcementapi.h"

#include <QAbstractTableModel>
#include <QUrl>

/**
 * @brief List of sessions received from a listing server
 */
class SessionListingModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	enum SessionListingRoles {
		SortKeyRole = Qt::UserRole,
		UrlRole,
		IsPasswordedRole,
		IsNsfwRole
	};

	SessionListingModel(QObject *parent=nullptr);

	int rowCount(const QModelIndex &parent=QModelIndex()) const;
	int columnCount(const QModelIndex &parent=QModelIndex()) const;
	QVariant data(const QModelIndex &index, int role=Qt::DisplayRole) const;
	QVariant headerData(int section, Qt::Orientation orientation, int role=Qt::DisplayRole) const;
	Qt::ItemFlags flags(const QModelIndex &index) const;

	//int nsfmCount() const { return m_nsfmCount; }

public slots:
	void setList(const QList<sessionlisting::Session> sessions);

private:
	QList<sessionlisting::Session> m_sessions;
};

#endif

