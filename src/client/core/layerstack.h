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
#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "annotationmodel.h"
#include "tile.h"

#include <cstdint>

#include <QObject>
#include <QList>
#include <QImage>
#include <QBitArray>

class QDataStream;

namespace paintcore {

class Layer;
class EditableLayer;
class EditableLayerStack;
class Tile;
class Savepoint;
struct LayerInfo;

/**
 * \brief A stack of layers.
 */
class LayerStack : public QObject {
	Q_PROPERTY(AnnotationModel* annotations READ annotations CONSTANT)
	Q_OBJECT
	friend class EditableLayerStack;
public:
	enum ViewMode {
		NORMAL,   // show all layers normally
		SOLO,     // show only the view layer
		ONIONSKIN // show view layer + few layers below it with decreasing opacity
	};

	LayerStack(QObject *parent=nullptr);
	~LayerStack();

	//! Return a copy of this LayerStack
	LayerStack *clone(QObject *newParent=nullptr) const { return new LayerStack(this, newParent); }

	//! Get the background tile
	Tile background() const { return m_backgroundTile; }

	//! Get the number of layers in the stack
	int layerCount() const { return m_layers.count(); }

	//! Get a read only layer by its index
	const Layer *getLayerByIndex(int index) const;

	//! Get a read only layer by its ID
	const Layer *getLayer(int id) const;

	//! Get this layer stack's annotations
	const AnnotationModel *annotations() const { return m_annotations; }
	AnnotationModel *annotations() { return m_annotations; }

	//! Get the index of the specified layer
	int indexOf(int id) const;

	//! Get the width of the layer stack
	int width() const { return m_width; }

	//! Get the height of the layer stack
	int height() const { return m_height; }

	//! Get the width and height of the layer stack
	QSize size() const { return QSize(m_width, m_height); }

	//! Paint all changed tiles in the given area
	void paintChangedTiles(const QRect& rect, QPaintDevice *target, bool clean=true);

	//! Return the topmost visible layer with a color at the point
	const Layer *layerAt(int x, int y) const;

	//! Get the merged color value at the point
	QColor colorAt(int x, int y, int dia=0) const;

	//! Return a flattened image of the layer stack
	QImage toFlatImage(bool includeAnnotations, bool includeBackground) const;

	//! Return a single layer merged with the background
	QImage flatLayerImage(int layerIdxr) const;

	//! Get a merged tile
	Tile getFlatTile(int x, int y) const;

	//! Mark the tiles under the area dirty
	void markDirty(const QRect &area);

	//! Mark all tiles as dirty
	void markDirty();

	//! Mark the tile at the given index as dirty
	void markDirty(int x, int y);

	//! Mark the tile at the given index as dirty
	void markDirty(int index);

	//! Create a new savepoint
	Savepoint *makeSavepoint();

	//! Get the current view rendering mode
	ViewMode viewMode() const { return m_viewmode; }

	//! Are layers tagged for censoring actually censored?
	bool isCensored() const { return m_censorLayers; }

	/**
	 * @brief Find a layer with a sublayer with the given ID and return its change bounds
	 * @param contextid
	 * @return Layer ID, Change bounds pair
	 */
	QPair<int,QRect> findChangeBounds(int contextid);

	//! Start a layer stack editing sequence
	inline EditableLayerStack editor();

signals:
	//! Emitted when the visible layers are edited
	void areaChanged(const QRect &area);

	//! Layer width/height changed
	void resized(int xoffset, int yoffset, const QSize &oldsize);

private:
	LayerStack(const LayerStack *orig, QObject *parent);

	// Emission of areaChanged is suppressed during an active write sequence
	void beginWriteSequence();
	void endWriteSequence();

	void flattenTile(quint32 *data, int xindex, int yindex) const;

	bool isVisible(int idx) const;
	int layerOpacity(int idx) const;
	quint32 layerTint(int idx) const;

	int m_width, m_height;
	int m_xtiles, m_ytiles;
	QList<Layer*> m_layers;
	AnnotationModel *m_annotations;
	Tile m_backgroundTile;
	Tile m_paintBackgroundTile;

	QBitArray m_dirtytiles;
	QRect m_dirtyrect;

	ViewMode m_viewmode;
	int m_viewlayeridx;
	int m_onionskinsBelow, m_onionskinsAbove;
	int m_openEditors;
	bool m_onionskinTint;
	bool m_censorLayers;
};

/// Layer stack savepoint for undo use
class Savepoint {
	friend class LayerStack;
	friend class EditableLayerStack;
public:
	~Savepoint();

	void toDatastream(QDataStream &out) const;
	static Savepoint *fromDatastream(QDataStream &in);

private:
	Savepoint() {}
	QList<Layer*> layers;
	QList<Annotation> annotations;
	Tile background;
	int width, height;
};

/**
 * @brief A wrapper class for editing a LayerStack
 */
class EditableLayerStack {
public:
	explicit EditableLayerStack(LayerStack *layerstack)
		: d(layerstack)
	{
		Q_ASSERT(d);
		d->beginWriteSequence();
	}
	EditableLayerStack(EditableLayerStack &&other)
	{
		d = other.d;
		other.d = nullptr;
	}

	EditableLayerStack(const EditableLayerStack&) = delete;
	EditableLayerStack &operator=(const EditableLayerStack&) = delete;

	~EditableLayerStack()
	{
		if(d)
			d->endWriteSequence();
	}

	//! Adjust layer stack size
	void resize(int top, int right, int bottom, int left);

	//! Set the background tile
	void setBackground(const Tile &tile);

	//! Create a new layer
	EditableLayer createLayer(int id, int source, const QColor &color, bool insert, bool copy, const QString &name);

	//! Delete a layer
	bool deleteLayer(int id);

	//! Merge the layer to the one below it
	void mergeLayerDown(int id);

	//! Re-order the layer stack
	void reorderLayers(const QList<uint16_t> &neworder);

	//! Get a layer by its index
	EditableLayer getEditableLayerByIndex(int index);

	//! Get a layer wrapped in EditableLayer
	EditableLayer getEditableLayer(int id);

	//! Clear the entire layer stack
	void reset();

	//! Remove all preview layers (ephemeral sublayers)
	void removePreviews();

	//! Merge all sublayers with the given ID
	void mergeSublayers(int id);

	//! Merge all sublayers with positive IDs
	void mergeAllSublayers();

	//! Set layer view mode
	void setViewMode(LayerStack::ViewMode mode);

	//! Set the selected layer (used by view modes other than NORMAL)
	void setViewLayer(int id);

	//! Set onionskin view mode parameters
	void setOnionskinMode(int below, int above, bool tint);

	//! Enable/disable censoring of layers
	void setCensorship(bool censor);

	//! Restore layer stack to a previous savepoint
	void restoreSavepoint(const Savepoint *savepoint);

	const LayerStack *layerStack() const { return d; }

	const LayerStack *operator ->() const { return d; }

private:
	LayerStack *d;
};

EditableLayerStack LayerStack::editor() { return EditableLayerStack(this); }

}

#endif

