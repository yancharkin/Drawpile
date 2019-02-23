/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2017-2018 Calle Laakkonen

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
#ifndef TOOLS_BEZIER_H
#define TOOLS_BEZIER_H

#include "tool.h"

namespace tools {

/**
 * \brief A bezier curve tool
 */
class BezierTool : public Tool {
public:
	BezierTool(ToolController &owner);

	void begin(const paintcore::Point& point, bool right, float zoom) override;
	void motion(const paintcore::Point& point, bool constrain, bool center) override;
	void hover(const QPointF& point) override;
	void end() override;
	void finishMultipart() override;
	void cancelMultipart() override;
	void undoMultipart() override;
	bool isMultipart() const override { return !m_points.isEmpty(); }

private:
	void updatePreview();
	paintcore::PointVector calculateBezierCurve() const;

	struct ControlPoint {
		QPointF point;
		QPointF cp; // second control point, relative to the main point
	};

	QVector<ControlPoint> m_points;
	QPointF m_beginPoint;
	bool m_rightButton;
};

}

#endif

