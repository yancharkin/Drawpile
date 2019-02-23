/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2006-2016 Calle Laakkonen

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
#ifndef TOOLSETTINGS_COLORPICKER_H
#define TOOLSETTINGS_COLORPICKER_H

#include "toolsettings.h"
#include "utils/palette.h"

namespace widgets {
	class PaletteWidget;
}
class QCheckBox;
class QSpinBox;

namespace tools {

/**
 * @brief Color picker history
 */
class ColorPickerSettings : public ToolSettings {
Q_OBJECT
public:
	ColorPickerSettings(ToolController *ctrl, QObject *parent=nullptr);
	~ColorPickerSettings();

	QString toolType() const override { return QStringLiteral("picker"); }

	ToolProperties saveToolSettings() override;
	void restoreToolSettings(const ToolProperties &cfg) override;

	void setForeground(const QColor &color) override { Q_UNUSED(color); }
	void quickAdjust1(float adjustment) override;

	int getSize() const override;
	bool getSubpixelMode() const override { return false; }

public slots:
	void addColor(const QColor &color);
	void pushSettings() override;

signals:
	void colorSelected(const QColor &color);

protected:
	QWidget *createUiWidget(QWidget *parent) override;

private:
	Palette m_palette;
	widgets::PaletteWidget *m_palettewidget;
	QCheckBox *m_layerpick;
	QSpinBox *m_size;
};

}

#endif


