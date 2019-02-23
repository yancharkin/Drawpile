/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2008-2015 Calle Laakkonen

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
#ifndef LAYERLISTMODEL_H
#define LAYERLISTMODEL_H

#include <QAbstractListModel>
#include <QItemDelegate>

namespace canvas {
	struct LayerListItem;
}

namespace protocol {
	class MessagePtr;
}
namespace docks {

/**
 * \brief A custom item delegate for displaying layer names and editing layer settings.
 */
class LayerListDelegate : public QItemDelegate {
Q_OBJECT
public:
	LayerListDelegate(QObject *parent=0);

	void paint(QPainter *painter, const QStyleOptionViewItem &option,
			const QModelIndex &index) const;
	QSize sizeHint(const QStyleOptionViewItem & option,
			const QModelIndex & index ) const;

	void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem & option, const QModelIndex & index ) const;
	void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex& index) const;
	bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index);

	void setShowNumbers(bool show);

signals:
	void toggleVisibility(int layerId, bool visible);
	void layerCommand(protocol::MessagePtr msg);

private:
	void drawOpacityGlyph(const QRectF& rect, QPainter *painter, float value, bool hidden, bool censored) const;

	QPixmap m_visibleIcon;
	QPixmap m_censoredIcon;
	QPixmap m_hiddenIcon;

	bool m_showNumbers;
};

}

#endif
