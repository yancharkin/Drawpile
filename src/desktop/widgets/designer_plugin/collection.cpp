/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2014-2017 Calle Laakkonen

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

#include "collection.h"
#include "colorbutton_plugin.h"
#include "brushpreview_plugin.h"
#include "groupedtoolbutton_plugin.h"
#include "filmstrip_plugin.h"
#include "resizer_plugin.h"
#include "tablettester_plugin.h"
#include "spinner_plugin.h"

DrawpileWidgetCollection::DrawpileWidgetCollection(QObject *parent) :
	QObject(parent)
{
    widgets
		<< new ColorButtonPlugin(this)
		<< new BrushPreviewPlugin(this)
		<< new GroupedToolButtonPlugin(this)
		<< new FilmstripPlugin(this)
		<< new ResizerPlugin(this)
		<< new TabletTesterPlugin(this)
		<< new SpinnerPlugin(this)
		;
}

QList<QDesignerCustomWidgetInterface *> DrawpileWidgetCollection::customWidgets() const
{
    return widgets;
}
