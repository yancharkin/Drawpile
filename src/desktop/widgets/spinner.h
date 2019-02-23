/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2018 Calle Laakkonen

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

#ifndef WIDGET_SPINNER_H
#define WIDGET_SPINNER_H

#include <QWidget>

#ifndef DESIGNER_PLUGIN
namespace widgets {
#define PLUGIN_EXPORT
#else
#include <QtUiPlugin/QDesignerExportWidget>
#define PLUGIN_EXPORT QDESIGNER_WIDGET_EXPORT
#endif

class Spinner : public QWidget {
	Q_OBJECT
	Q_PROPERTY(int dots READ dots WRITE setDots)
public:
	Spinner(QWidget *parent=nullptr);

	int dots() const { return m_dots; }
	void setDots(int dots) { m_dots = qBound(2, dots, 32); }

protected:
	void paintEvent(QPaintEvent *);
	void timerEvent(QTimerEvent *);

private:
	int m_dots;
	int m_currentDot;
};

#ifndef DESIGNER_PLUGIN
}
#endif

#endif

