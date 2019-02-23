/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2006-2018 Calle Laakkonen

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
#ifndef TOOLS_SHAPETOOLS_H
#define TOOLS_SHAPETOOLS_H

#include "tool.h"
#include "brushes/brushengine.h"

namespace tools {

/**
 * \brief Base class for tools that draw a shape (as opposed to freehand tools)
 */
class ShapeTool : public Tool {
public:
	ShapeTool(ToolController &owner, Type type, QCursor cursor) : Tool(owner, type, cursor) {}

	void begin(const paintcore::Point& point, bool right, float zoom) override;
	void motion(const paintcore::Point& point, bool constrain, bool center) override;
	void end() override;

protected:
	virtual paintcore::PointVector pointVector() const = 0;
	void updatePreview();
	QRectF rect() const { return QRectF(m_p1, m_p2).normalized(); }

	QPointF m_start, m_p1, m_p2;
};

/**
 * \brief Line tool
 *
 * The line tool draws straight lines.
 */
class Line : public ShapeTool {
public:
	Line(ToolController &owner);

	void motion(const paintcore::Point& point, bool constrain, bool center);

protected:
	virtual paintcore::PointVector pointVector() const;
};

/**
 * \brief Rectangle drawing tool
 *
 * This tool is used for drawing squares and rectangles
 */
class Rectangle : public ShapeTool {
public:
	Rectangle(ToolController &owner);

protected:
	virtual paintcore::PointVector pointVector() const;
};

/**
 * \brief Ellipse drawing tool
 *
 * This tool is used for drawing circles and ellipses
 */
class Ellipse : public ShapeTool {
public:
	Ellipse(ToolController &owner);

protected:
	virtual paintcore::PointVector pointVector() const;
};

}

#endif

