/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2006-2019 Calle Laakkonen

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
#ifndef TOOLS_TOOL_H
#define TOOLS_TOOL_H

#include "core/point.h"

#include <QCursor>

/**
 * @brief Tools
 *
 * Tools translate commands from the local user into messages that
 * can be sent over the network.
 * Read-only tools can access the canvas directly.
 */
namespace tools {

class ToolController;

/**
 * @brief Base class for all tools
 * Tool classes interpret mouse/pen commands into editing actions.
 */
class Tool
{
public:
	enum Type {
		FREEHAND, ERASER, LINE, RECTANGLE, ELLIPSE, BEZIER,
		FLOODFILL, ANNOTATION,
		PICKER, LASERPOINTER,
		SELECTION, POLYGONSELECTION,
		ZOOM,
		_LASTTOOL};

	Tool(ToolController &owner, Type type, const QCursor &cursor)
		: owner(owner), m_type(type), m_cursor(cursor)
		{}
	virtual ~Tool() {}

	Type type() const { return m_type; }
	const QCursor &cursor() const { return m_cursor; }

	/**
	 * @brief Start a new stroke
	 * @param point starting point
	 * @param right is the right mouse/pen button pressed instead of the left one
	 * @param zoom the current view zoom factor
	 */
	virtual void begin(const paintcore::Point& point, bool right, float zoom) = 0;

	/**
	 * @brief Continue a stroke
	 * @param point new point
	 * @param constrain is the "constrain motion" button pressed
	 * @param cener is the "center on start point" button pressed
	 */
	virtual void motion(const paintcore::Point& point, bool constrain, bool center) = 0;

	/**
	 * @brief Tool hovering over the canvas
	 * @param point tool position
	 */
	virtual void hover(const QPointF &point) { Q_UNUSED(point); }

	//! End stroke
	virtual void end() = 0;

	//! Finish and commit a multipart stroke
	virtual void finishMultipart() { }

	//! Cancel the current multipart stroke (if any)
	virtual void cancelMultipart() { }

	//! Undo the latest step of a multipart stroke. Undoing the first part should cancel the stroke
	virtual void undoMultipart() { }

	//! Is there a multipart stroke in progress at the moment?
	virtual bool isMultipart() const { return false; }

	//! Does this tool allow stroke smoothing to be used?
	virtual bool allowSmoothing() const { return false; }

protected:
	ToolController &owner;

private:
	const Type m_type;
	const QCursor m_cursor;
};

}

#endif

