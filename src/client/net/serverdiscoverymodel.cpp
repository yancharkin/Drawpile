/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2015 Calle Laakkonen

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

#include "serverdiscoverymodel.h"

#include "config.h"

#include <QGuiApplication>
#include <QUrl>
#include <QDebug>

#include <KDNSSD/DNSSD/ServiceBrowser>

ServerDiscoveryModel::ServerDiscoveryModel(QObject *parent)
	: QAbstractTableModel(parent), m_browser(nullptr)
{
}

int ServerDiscoveryModel::rowCount(const QModelIndex &parent) const
{
	if(parent.isValid())
		return 0;
	return m_servers.size();
}

int ServerDiscoveryModel::columnCount(const QModelIndex &parent) const
{
	if(parent.isValid())
		return 0;

	// Columns:
	// 0 - title
	// 1 - server name
	// 2 - age
	return 3;
}

static QString ageString(const qint64 seconds)
{
	const int minutes = seconds / 60;
	return QGuiApplication::tr("%1h %2m").arg(minutes/60).arg(minutes%60);
}

QVariant ServerDiscoveryModel::data(const QModelIndex &index, int role) const
{
	if(index.row()<0 || index.row()>=m_servers.size())
		return QVariant();

	const DiscoveredServer &s = m_servers.at(index.row());

	if(role == Qt::DisplayRole) {
		switch(index.column()) {
		case 0: return s.title.isEmpty() ? tr("(untitled)") : s.title;
		case 1: return s.name;
		case 2: return ageString(s.started.msecsTo(QDateTime::currentDateTime()) / 1000);
		}

	} else if(role == SortKeyRole) {
		switch(index.column()) {
		case 0: return s.title;
		case 1: return s.name;
		case 2: return s.started;
		}

	} else if(role == UrlRole) {
		return s.url;
	}

	return QVariant();
}

QVariant ServerDiscoveryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();

	switch(section) {
	case 0: return tr("Title");
	case 1: return tr("Server");
	case 2: return tr("Age");
	}

	return QVariant();
}

Qt::ItemFlags ServerDiscoveryModel::flags(const QModelIndex &index) const
{
	if(index.row()<0 || index.row()>=m_servers.size())
		return Qt::NoItemFlags;

	const DiscoveredServer &s = m_servers.at(index.row());
	if(s.protocol.isCurrent())
		return QAbstractTableModel::flags(index);
	else
		return Qt::NoItemFlags;
}

void ServerDiscoveryModel::discover()
{
	if(!m_browser) {
		m_browser = new KDNSSD::ServiceBrowser("_drawpile._tcp", true, "local");
		m_browser->setParent(this);

		connect(m_browser, &KDNSSD::ServiceBrowser::serviceAdded, this, &ServerDiscoveryModel::addService);
		connect(m_browser, &KDNSSD::ServiceBrowser::serviceRemoved, this, &ServerDiscoveryModel::removeService);

		m_browser->startBrowse();
	}
}

void ServerDiscoveryModel::addService(KDNSSD::RemoteService::Ptr service)
{
	QUrl url;
	url.setScheme("drawpile");
	QHostAddress hostname = KDNSSD::ServiceBrowser::resolveHostName(service->hostName());
	url.setHost(hostname.isNull() ? service->hostName() : hostname.toString());
	if(service->port() != DRAWPILE_PROTO_DEFAULT_PORT)
		url.setPort(service->port());

	QDateTime started = QDateTime::fromString(service->textData()["started"], Qt::ISODate);
	started.setTimeSpec(Qt::UTC);

	DiscoveredServer s {
		url,
		service->serviceName(),
		service->textData()["title"],
		protocol::ProtocolVersion::fromString(service->textData()["protocol"]),
		started
	};

	beginInsertRows(QModelIndex(), m_servers.size(), m_servers.size());
	m_servers.append(s);
	endInsertRows();
}

void ServerDiscoveryModel::removeService(KDNSSD::RemoteService::Ptr service)
{
	for(int i=0;i<m_servers.size();++i) {
		if(m_servers.at(i).name == service->serviceName()) {
			beginRemoveRows(QModelIndex(), i, i);
			m_servers.removeAt(i);
			endRemoveRows();
			return;
		}
	}
}

