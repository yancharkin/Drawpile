/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2019 Calle Laakkonen

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

#include "sessionfilterproxymodel.h"
#include "../net/sessionlistingmodel.h"
#include "../net/loginsessions.h"

SessionFilterProxyModel::SessionFilterProxyModel(QObject *parent)
	: QSortFilterProxyModel(parent),
	m_showPassworded(true),
	m_showNsfw(true)
{
}

void SessionFilterProxyModel::setShowPassworded(bool show)
{
	if(m_showPassworded != show) {
		m_showPassworded = show;
		invalidateFilter();
	}
}

void SessionFilterProxyModel::setShowNsfw(bool show)
{
	if(m_showNsfw != show) {
		m_showNsfw = show;
		invalidateFilter();
	}
}

bool SessionFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
	const QModelIndex i = sourceModel()->index(source_row, 0);
	int nsfwRole=0, pwRole=0;

	if(sourceModel()->inherits(SessionListingModel::staticMetaObject.className())) {
		nsfwRole = SessionListingModel::IsNsfwRole;
		pwRole = SessionListingModel::IsPasswordedRole;

	} else if(sourceModel()->inherits(net::LoginSessionModel::staticMetaObject.className())) {
		nsfwRole = net::LoginSessionModel::NsfmRole;
		pwRole = net::LoginSessionModel::NeedPasswordRole;
	}

	if(!m_showNsfw && nsfwRole) {
		if(i.data(nsfwRole).toBool())
			return false;
	}

	if(!m_showPassworded && pwRole) {
		if(i.data(pwRole).toBool())
			return false;
	}

	return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
}

