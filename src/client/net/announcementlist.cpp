/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2017 Calle Laakkonen

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

#include "announcementlist.h"
#include "utils/listservermodel.h"

namespace net {

AnnouncementListModel::AnnouncementListModel(QObject *parent)
	: QAbstractTableModel(parent)
{
	refreshKnownServers();
}

void AnnouncementListModel::refreshKnownServers()
{
	// Get list of known servers (url -> {icon, name})
	const auto servers = sessionlisting::ListServerModel::listServers();
	for(const auto &s : servers) {
		m_knownServers[s.url] = QPair<QIcon,QString>(s.icon, s.name);
	}
}

QVariant AnnouncementListModel::data(const QModelIndex& index, int role) const
{
	if(index.isValid() && index.row() >= 0 && index.row() < m_announcements.size()) {
		if(role == Qt::DisplayRole) {
			const Announcement &a = m_announcements.at(index.row());
			switch(index.column()) {
			case 0:
				// If this is a known server, show the server's name instead of the URL
				if(m_knownServers.contains(a.url))
					return m_knownServers[a.url].second;
				else
					return a.url;
			case 1: return a.roomcode;
			case 2: return a.isPrivate ? tr("Private") : tr("Public");
			}

		} else if(role == Qt::DecorationRole) {
			if(index.column()==0) {
				// Show icon for known servers
				const Announcement &a = m_announcements.at(index.row());
				if(m_knownServers.contains(a.url))
					return m_knownServers[a.url].first;
			}
		} else if(role == Qt::UserRole) {
			return m_announcements.at(index.row()).url;
		}
	}

	return QVariant();
}

QVariant AnnouncementListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();

	switch(section) {
	case 0: return tr("Server");
	case 1: return tr("Room code");
	case 2: return tr("Mode");
	}

	return QVariant();
}


int AnnouncementListModel::rowCount(const QModelIndex& parent) const
{
	if(parent.isValid())
		return 0;
	return m_announcements.size();
}

int AnnouncementListModel::columnCount(const QModelIndex &parent) const
{
	if(parent.isValid())
		return 0;
	// Columns:
	// 0 - URL
	// 1 - Room code
	// 2 - Private mode
	return 3;
}

void AnnouncementListModel::addAnnouncement(const Announcement &a)
{
	// Check if the announcement is listed already
	for(int i=0;i<m_announcements.count();++i) {
		if(m_announcements.at(i).url == a.url) {
			m_announcements[i] = a;
			emit dataChanged(index(i, 0), index(i, columnCount()));
			return;
		}
	}

	// Append to the list
	const int pos = m_announcements.size();
	beginInsertRows(QModelIndex(), pos, pos);
	m_announcements.append(a);
	endInsertRows();
}

void AnnouncementListModel::removeAnnouncement(const QString &url)
{
	for(int pos=0;pos<m_announcements.count();++pos) {
		if(m_announcements.at(pos).url == url) {
			beginRemoveRows(QModelIndex(), pos, pos);
			m_announcements.remove(pos);
			endRemoveRows();
			return;
		}
	}
}

void AnnouncementListModel::clear()
{
	beginRemoveRows(QModelIndex(), 0, m_announcements.size()-1);
	m_announcements.clear();
	endRemoveRows();
}

}
