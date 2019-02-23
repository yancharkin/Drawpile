/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2006-2017 Calle Laakkonen

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
#ifndef TOOLSETTINGS_ANNOTATION_H
#define TOOLSETTINGS_ANNOTATION_H

#include "toolsettings.h"

class Ui_TextSettings;
class QTimer;
class QAction;

namespace tools {

/**
 * @brief Settings for the annotation tool
 *
 * The annotation tool is special because it is used to manipulate
 * annotation objects rather than pixel data.
 */
class AnnotationSettings : public ToolSettings {
Q_OBJECT
public:
	AnnotationSettings(ToolController *ctrl, QObject *parent=nullptr);
	~AnnotationSettings();

	QString toolType() const override { return QStringLiteral("annotation"); }

	/**
	 * @brief Get the ID of the currently selected annotation
	 * @return ID or 0 if none selected
	 */
	uint16_t selected() const { return m_selectionId; }

	/**
	 * @brief Focus content editing box and set cursor position
	 * @param cursorPos cursor position
	 */
	void setFocusAt(int cursorPos);

	void setForeground(const QColor &) override {}
	void quickAdjust1(float) override {}
	int getSize() const override { return 0; }
	bool getSubpixelMode() const override { return false; }

public slots:
	//! Set the currently selected annotation item
	void setSelectionId(uint16_t id);

	//! Focus the content editing box
	void setFocus();

private slots:
	void changeAlignment(const QAction *action);
	void toggleBold(bool bold);
	void toggleStrikethrough(bool strike);
	void updateStyleButtons();
	void setEditorBackgroundColor(const QColor &color);

	void applyChanges();
	void saveChanges();
	void removeAnnotation();
	void bake();

	void updateFontIfUniform();

protected:
	QWidget *createUiWidget(QWidget *parent) override;

private:
	void resetContentFont(bool resetFamily, bool resetSize, bool resetColor);
	void setUiEnabled(bool enabled);

	Ui_TextSettings *_ui;

	uint16_t m_selectionId;

	bool m_noupdate;
	QTimer *m_updatetimer;
};

}

#endif

