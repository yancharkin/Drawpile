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

#include "brushsettings.h"
#include "tools/toolcontroller.h"
#include "tools/toolproperties.h"
#include "core/brushmask.h"
#include "brushes/brush.h"
#include "brushes/classicbrushpainter.h"

// Work around lack of namespace support in Qt designer (TODO is the problem in our plugin?)
#include "widgets/groupedtoolbutton.h"
#include "widgets/brushpreview.h"
using widgets::BrushPreview;
using widgets::GroupedToolButton;

#include "ui_brushdock.h"

#include <QKeyEvent>
#include <QPainter>
#include <QMimeData>
#include <QSettings>
#include <QStandardItemModel>

namespace tools {

namespace brushprop {
	// Brush properties
	static const ToolProperties::IntValue
		size = {QStringLiteral("size"), 10, 1, 255},
		opacity = {QStringLiteral("opacity"), 100, 1, 100},
		hard = {QStringLiteral("hard"), 100, 1, 100},
		smudge = {QStringLiteral("smudge"), 0, 0, 100},
		resmudge = {QStringLiteral("resmudge"), 3, 0, 255},
		spacing = {QStringLiteral("spacing"), 10, 1, 150},
		brushmode = {QStringLiteral("brushmode"), 0, 0, 3} /* 0: hard edge, 1: square, 2: soft edge, 3: watercolor */
		;
	static const ToolProperties::BoolValue
		sizePressure = {QStringLiteral("sizep"), false},
		opacityPressure = {QStringLiteral("opacityp"), false},
		hardPressure = {QStringLiteral("hardp"), false},
		smudgePressure = {QStringLiteral("smudgep"), false},
		incremental = {QStringLiteral("incremental"), true}
		;

	static const QString
		LABEL = QStringLiteral("label")
		;
}

namespace toolprop {
	// Tool properties
	static const ToolProperties::VariantValue
		color = {QStringLiteral("color"), QColor(Qt::black)}
		;
	static const ToolProperties::IntValue
		blendmode = {QStringLiteral("blendmode"), paintcore::BlendMode::MODE_NORMAL, 0, 255},
		erasemode = {QStringLiteral("erasemode"), paintcore::BlendMode::MODE_ERASE, 0, 255}
		;
	static const ToolProperties::BoolValue
		useEraseMode = {QStringLiteral("use_erasemode"), false}
		;
}

static brushes::ClassicBrush brushFromProps(const ToolProperties &bp, const ToolProperties &tp)
{
	const int brushMode = bp.intValue(brushprop::brushmode);

	brushes::ClassicBrush b;
	b.setSize(bp.intValue(brushprop::size));
	if(bp.boolValue(brushprop::sizePressure))
		b.setSize2(1);
	else
		b.setSize2(b.size1());

	b.setOpacity(bp.intValue(brushprop::opacity) / 100.0);
	if(bp.boolValue(brushprop::opacityPressure))
		b.setOpacity2(0);
	else
		b.setOpacity2(b.opacity1());

	if(brushMode <= 1) {
		// Hard edge mode: hardness at full and no antialiasing
		b.setHardness(1);
		b.setHardness2(1);
		b.setSubpixel(false);

	} else {
		b.setHardness(bp.intValue(brushprop::hard) / 100.0);
		if(bp.boolValue(brushprop::hardPressure))
			b.setHardness2(0);
		else
			b.setHardness2(b.hardness1());
		b.setSubpixel(true);
	}

	if(brushMode == 3) {
		b.setSmudge(bp.intValue(brushprop::smudge) / 100.0);
		if(bp.boolValue(brushprop::smudgePressure))
			b.setSmudge2(0);
		else
			b.setSmudge2(b.smudge1());

		b.setResmudge(bp.intValue(brushprop::resmudge));

		// Watercolor mode requires incremental drawing
		b.setIncremental(true);

	} else {
		b.setSmudge(0);
		b.setSmudge2(0);
		b.setResmudge(0);
		b.setIncremental(bp.boolValue(brushprop::incremental));
	}

	b.setSpacing(bp.intValue(brushprop::spacing));

	b.setColor(tp.value(toolprop::color).value<QColor>());

	const int blendingMode = tp.intValue(tp.boolValue(toolprop::useEraseMode) ? toolprop::erasemode : toolprop::blendmode);
	b.setBlendingMode(paintcore::BlendMode::Mode(blendingMode));

	b.setSquare(brushMode == 1);

	return b;
}

static const int BRUSH_COUNT = 6; // Last is the dedicated eraser slot
static const int ERASER_SLOT = 5; // Index of the dedicated erser slot

struct BrushSettings::Private {
	Ui_BrushDock ui;

	QStandardItemModel *blendModes, *eraseModes;
	BrushSettings *basicSettings;
	BrushPresetModel *presets;

	ToolProperties brushProps[BRUSH_COUNT];
	ToolProperties toolProps[BRUSH_COUNT];
	int current;
	int previousNonEraser;

	bool updateInProgress;

	inline ToolProperties &currentBrush() {
		Q_ASSERT(current >= 0 && current < BRUSH_COUNT);
		return brushProps[current];
	}
	inline ToolProperties &currentTool() {
		Q_ASSERT(current >= 0 && current < BRUSH_COUNT);
		return toolProps[current];
	}

	inline QColor currentColor() {
		return currentTool().value(toolprop::color).value<QColor>();
	}

	Private(BrushSettings *b)
		: current(0), previousNonEraser(0), updateInProgress(false)
	{
		blendModes = new QStandardItemModel(0, 1, b);
		for(const auto bm : paintcore::getBlendModeNames(paintcore::BlendMode::BrushMode)) {
			auto item = new QStandardItem(bm.second);
			item->setData(bm.first, Qt::UserRole);
			blendModes->appendRow(item);
		}

		eraseModes = new QStandardItemModel(0, 1, b);
		auto erase1 = new QStandardItem(QApplication::tr("Erase"));
		erase1->setData(QVariant(paintcore::BlendMode::MODE_ERASE), Qt::UserRole);
		eraseModes->appendRow(erase1);

		auto erase2 = new QStandardItem(QApplication::tr("Color Erase"));
		erase2->setData(QVariant(paintcore::BlendMode::MODE_COLORERASE), Qt::UserRole);
		eraseModes->appendRow(erase2);
	}

	void updateBrush()
	{
		// Update brush object from current properties
		brushes::ClassicBrush b = brushFromProps(currentBrush(), currentTool());

		ui.preview->setBrush(b);
		ui.preview->setColor(currentColor());
	}

	GroupedToolButton *brushSlotButton(int i)
	{
		static_assert (BRUSH_COUNT == 6, "update brushSlottButton");
		switch(i) {
		case 0: return ui.slot1;
		case 1: return ui.slot2;
		case 2: return ui.slot3;
		case 3: return ui.slot4;
		case 4: return ui.slot5;
		case 5: return ui.slotEraser;
		default: qFatal("brushSlotButton(%d): no such button", i);
		}
	}
};

BrushSettings::BrushSettings(ToolController *ctrl, QObject *parent)
	: ToolSettings(ctrl, parent), d(new Private(this))
{
	d->basicSettings = this;
	d->presets = BrushPresetModel::getSharedInstance();
}

BrushSettings::~BrushSettings()
{
	delete d;
}

QWidget *BrushSettings::createUiWidget(QWidget *parent)
{
	QWidget *widget = new QWidget(parent);
	d->ui.setupUi(widget);

	// Outside communication
	connect(d->ui.brushsize, SIGNAL(valueChanged(int)), parent, SIGNAL(sizeChanged(int)));
	connect(d->ui.preview, SIGNAL(requestColorChange()), parent, SLOT(changeForegroundColor()));
	connect(d->ui.preview, &BrushPreview::brushChanged, controller(), &ToolController::setActiveBrush);

	// Internal updates
	connect(d->ui.blendmode, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, &BrushSettings::selectBlendMode);
	connect(d->ui.modeEraser, &QToolButton::clicked, this, &BrushSettings::setEraserMode);

	connect(d->ui.hardedgeMode, &QToolButton::clicked, this, &BrushSettings::updateFromUi);
	connect(d->ui.hardedgeMode, &QToolButton::clicked, this, &BrushSettings::updateUi);
	connect(d->ui.squareMode, &QToolButton::clicked, this, &BrushSettings::updateFromUi);
	connect(d->ui.squareMode, &QToolButton::clicked, this, &BrushSettings::updateUi);
	connect(d->ui.softedgeMode, &QToolButton::clicked, this, &BrushSettings::updateFromUi);
	connect(d->ui.softedgeMode, &QToolButton::clicked, this, &BrushSettings::updateUi);
	connect(d->ui.watercolorMode, &QToolButton::clicked, this, &BrushSettings::updateFromUi);
	connect(d->ui.watercolorMode, &QToolButton::clicked, this, &BrushSettings::updateUi);

	connect(d->ui.brushsize, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.pressureSize, &QToolButton::toggled, this, &BrushSettings::updateFromUi);

	connect(d->ui.brushopacity, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.pressureOpacity, &QToolButton::toggled, this, &BrushSettings::updateFromUi);

	connect(d->ui.brushhardness, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.pressureHardness, &QToolButton::toggled, this, &BrushSettings::updateFromUi);

	connect(d->ui.brushsmudging, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.pressureSmudging, &QToolButton::toggled, this, &BrushSettings::updateFromUi);

	connect(d->ui.colorpickup, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.brushspacing, &QSlider::valueChanged, this, &BrushSettings::updateFromUi);
	connect(d->ui.modeIncremental, &QToolButton::clicked, this, &BrushSettings::updateFromUi);

	// Brush slot buttons
	for(int i=0;i<BRUSH_COUNT;++i) {
		connect(d->brushSlotButton(i), &QToolButton::clicked, this, [this, i]() { selectBrushSlot(i); });
	}

	return widget;
}

void BrushSettings::setCurrentBrushSettings(const ToolProperties &brushProps)
{
	d->currentBrush() = brushProps;
	updateUi();
}

ToolProperties BrushSettings::getCurrentBrushSettings() const
{
	return d->currentBrush();
}

int BrushSettings::currentBrushSlot() const
{
	return d->current;
}
void BrushSettings::selectBrushSlot(int i)
{
	if(i<0 || i>= BRUSH_COUNT) {
		qWarning("selectBrushSlot(%d): invalid slot index!", i);
		return;
	}
	const int previousSlot = d->current;

	d->brushSlotButton(i)->setChecked(true);
	d->current = i;
	updateUi();

	emit colorChanged(d->currentColor());

	if((previousSlot==ERASER_SLOT) != (i==ERASER_SLOT))
		emit eraseModeChanged(i==ERASER_SLOT);
}

void BrushSettings::toggleEraserMode()
{
	if(d->current != ERASER_SLOT) {
		// Eraser mode is fixed in dedicated eraser slot
		d->currentTool().setValue(toolprop::useEraseMode, !d->currentTool().boolValue(toolprop::useEraseMode));
		updateUi();
	}
}

void BrushSettings::setEraserMode(bool erase)
{
	d->currentTool().setValue(toolprop::useEraseMode, erase);
	updateUi();
}

void BrushSettings::selectEraserSlot(bool eraser)
{
	if(eraser) {
		if(!isCurrentEraserSlot()) {
			d->previousNonEraser = d->current;
			selectBrushSlot(ERASER_SLOT);
		}
	} else {
		if(isCurrentEraserSlot()) {
			selectBrushSlot(d->previousNonEraser);
		}
	}
}

bool BrushSettings::isCurrentEraserSlot() const
{
	return d->current == ERASER_SLOT;
}

void BrushSettings::selectBlendMode(int modeIndex)
{
	QString prop;
	if(d->currentTool().boolValue(toolprop::useEraseMode))
		prop = toolprop::erasemode.key;
	else
		prop = toolprop::blendmode.key;
	d->currentTool().setValue(prop, d->ui.blendmode->model()->index(modeIndex,0).data(Qt::UserRole).toInt());
	updateUi();
}

void BrushSettings::updateUi()
{
	// Update the UI to match the currently selected brush
	if(d->updateInProgress)
		return;

	d->updateInProgress = true;

	const ToolProperties &brush = d->currentBrush();
	const ToolProperties &tool = d->currentTool();

	// Select brush type
	const int brushMode = brush.intValue(brushprop::brushmode);
	switch(brushMode) {
	case 1: d->ui.squareMode->setChecked(true); break;
	case 2: d->ui.softedgeMode->setChecked(true); break;
	case 3: d->ui.watercolorMode->setChecked(true); break;
	case 0:
	default: d->ui.hardedgeMode->setChecked(true); break;
	}

	emit subpixelModeChanged(getSubpixelMode(), isSquare());

	// Hide certain features based on the brush type
	d->ui.brushhardness->setVisible(brushMode > 1);
	d->ui.pressureHardness->setVisible(brushMode > 1);
	d->ui.hardnessLabel->setVisible(brushMode > 1);
	d->ui.hardnessBox->setVisible(brushMode > 1);

	d->ui.brushsmudging->setVisible(brushMode == 3);
	d->ui.pressureSmudging->setVisible(brushMode == 3);
	d->ui.smudgingLabel->setVisible(brushMode == 3);
	d->ui.smudgingBox->setVisible(brushMode == 3);

	d->ui.colorpickup->setVisible(brushMode == 3);
	d->ui.colorpickupLabel->setVisible(brushMode == 3);
	d->ui.colorpickupBox->setVisible(brushMode == 3);

	d->ui.modeIncremental->setEnabled(brushMode != 3);

	d->ui.brushsize->setValue(brush.intValue(brushprop::size));
	d->ui.brushopacity->setValue(brush.intValue(brushprop::opacity));

	// Show correct blending mode
	int blendmode;
	const bool erasemode = tool.boolValue(toolprop::useEraseMode);
	if(erasemode) {
		d->ui.blendmode->setModel(d->eraseModes);
		blendmode = tool.intValue(toolprop::erasemode);
	} else {
		d->ui.blendmode->setModel(d->blendModes);
		blendmode = tool.intValue(toolprop::blendmode);
	}
	d->ui.modeEraser->setChecked(erasemode);
	d->ui.modeEraser->setEnabled(d->current != ERASER_SLOT);

	for(int i=0;i<d->ui.blendmode->model()->rowCount();++i) {
		if(d->ui.blendmode->model()->index(i,0).data(Qt::UserRole) == blendmode) {
			d->ui.blendmode->setCurrentIndex(i);
			break;
		}
	}

	// Set values
	d->ui.brushsize->setValue(brush.intValue(brushprop::size));
	d->ui.pressureSize->setChecked(brush.boolValue(brushprop::sizePressure));

	d->ui.brushopacity->setValue(brush.intValue(brushprop::opacity));
	d->ui.pressureOpacity->setChecked(brush.boolValue(brushprop::opacityPressure));

	d->ui.brushhardness->setValue(brush.intValue(brushprop::hard));
	d->ui.pressureHardness->setChecked(brushMode > 1 && brush.boolValue(brushprop::hardPressure));

	d->ui.brushsmudging->setValue(brush.intValue(brushprop::smudge));
	d->ui.pressureSmudging->setChecked(brushMode == 3 && brush.boolValue(brushprop::smudgePressure));

	d->ui.colorpickup->setValue(brush.intValue(brushprop::resmudge));

	d->ui.brushspacing->setValue(brush.intValue(brushprop::spacing));
	d->ui.modeIncremental->setChecked(brush.boolValue(brushprop::incremental));

	d->updateInProgress = false;
	d->updateBrush();
}

void BrushSettings::updateFromUi()
{
	if(d->updateInProgress)
		return;

	// Copy changes from the UI to the brush properties object,
	// then update the brush
	ToolProperties &brush = d->currentBrush();

	if(d->ui.hardedgeMode->isChecked())
		brush.setValue(brushprop::brushmode, 0);
	else if(d->ui.squareMode->isChecked())
		brush.setValue(brushprop::brushmode, 1);
	else if(d->ui.softedgeMode->isChecked())
		brush.setValue(brushprop::brushmode, 2);
	else
		brush.setValue(brushprop::brushmode, 3);

	brush.setValue(brushprop::size, d->ui.brushsize->value());
	brush.setValue(brushprop::sizePressure, d->ui.pressureSize->isChecked());

	brush.setValue(brushprop::opacity, d->ui.brushopacity->value());
	brush.setValue(brushprop::opacityPressure, d->ui.pressureOpacity->isChecked());

	brush.setValue(brushprop::hard, d->ui.brushhardness->value());
	brush.setValue(brushprop::hardPressure, d->ui.pressureHardness->isChecked());

	brush.setValue(brushprop::smudge, d->ui.brushsmudging->value());
	brush.setValue(brushprop::smudgePressure, d->ui.pressureSmudging->isChecked());

	brush.setValue(brushprop::resmudge, d->ui.colorpickup->value());
	brush.setValue(brushprop::spacing, d->ui.brushspacing->value());
	brush.setValue(brushprop::incremental, d->ui.modeIncremental->isChecked());

	if(d->current == ERASER_SLOT)
		d->currentTool().setValue(toolprop::useEraseMode, true);
	else
		d->currentTool().setValue(toolprop::useEraseMode, d->ui.modeEraser->isChecked());

	d->updateBrush();
}

void BrushSettings::pushSettings()
{
	controller()->setActiveBrush(d->ui.preview->brush());
}

ToolProperties BrushSettings::saveToolSettings()
{
	ToolProperties cfg(toolType());
	for(int i=0;i<BRUSH_COUNT;++i) {
		cfg.setValue(QString("brush%1").arg(i), d->brushProps[i].asVariant());
		cfg.setValue(QString("tool%1").arg(i), d->toolProps[i].asVariant());

	}
	return cfg;
}

void BrushSettings::restoreToolSettings(const ToolProperties &cfg)
{
	d->current = qBound(0, cfg.value("active", 0).toInt(), BRUSH_COUNT-1);
	d->previousNonEraser = d->current;
	for(int i=0;i<BRUSH_COUNT;++i) {
		QVariantHash brush = cfg.value(QString("brush%1").arg(i)).toHash();
		QVariantHash tool = cfg.value(QString("tool%1").arg(i)).toHash();

		d->brushProps[i] = ToolProperties::fromVariant(brush);
		d->toolProps[i] = ToolProperties::fromVariant(tool);
		d->brushSlotButton(i)->setColorSwatch( d->toolProps[i].value(toolprop::color).value<QColor>());
	}
	d->toolProps[ERASER_SLOT].setValue(toolprop::useEraseMode, true);

	updateUi();
}

void BrushSettings::setActiveTool(const tools::Tool::Type tool)
{
	switch(tool) {

	case tools::Tool::LINE: d->ui.preview->setPreviewShape(BrushPreview::Line); break;
	case tools::Tool::RECTANGLE: d->ui.preview->setPreviewShape(BrushPreview::Rectangle); break;
	case tools::Tool::ELLIPSE: d->ui.preview->setPreviewShape(BrushPreview::Ellipse); break;
	default: d->ui.preview->setPreviewShape(BrushPreview::Stroke); break;
	}

	if(tool == tools::Tool::ERASER) {
		selectEraserSlot(true);
		for(int i=0;i<BRUSH_COUNT-1;++i)
			d->brushSlotButton(i)->setEnabled(false);
	} else {
		for(int i=0;i<BRUSH_COUNT-1;++i)
			d->brushSlotButton(i)->setEnabled(true);

		selectEraserSlot(false);
	}
}

void BrushSettings::setForeground(const QColor& color)
{
	if(color != d->currentColor()) {
		d->currentTool().setValue(toolprop::color, color);
		d->brushSlotButton(d->current)->setColorSwatch(color);
		d->ui.preview->setColor(color);
	}
}

void BrushSettings::quickAdjust1(float adjustment)
{
	int adj = qRound(adjustment);
	if(adj!=0)
		d->ui.brushsize->setValue(d->ui.brushsize->value() + adj);
}

int BrushSettings::getSize() const
{
	return d->ui.brushsize->value();
}

bool BrushSettings::getSubpixelMode() const
{
	return d->currentBrush().intValue(brushprop::brushmode) > 1;
}

bool BrushSettings::isSquare() const
{
	return d->currentBrush().intValue(brushprop::brushmode) == 1;
}

//// BRUSH PRESET PALETTE MODEL ////

static constexpr int BRUSH_ICON_SIZE = 42;

struct BrushPresetModel::Private {
	QList<ToolProperties> presets;
	mutable QList<QPixmap> iconcache;

	QPixmap getIcon(int idx) const {
		Q_ASSERT(idx >=0 && idx < presets.size());
		Q_ASSERT(presets.size() == iconcache.size());

		if(iconcache.at(idx).isNull()) {

			const brushes::ClassicBrush brush = brushFromProps(presets[idx], ToolProperties());
			const paintcore::BrushStamp stamp = brushes::makeGimpStyleBrushStamp(QPointF(), brush.size1(), brush.hardness1(), brush.opacity1());
			const int maskdia = stamp.mask.diameter();
			QImage icon(BRUSH_ICON_SIZE, BRUSH_ICON_SIZE, QImage::Format_ARGB32_Premultiplied);

			const QRgb color = (presets[idx].intValue(brushprop::brushmode)==2) ? 0x001d99f3 : 0;

			if(maskdia > BRUSH_ICON_SIZE) {
				// Clip to fit
				const int clip = (maskdia - BRUSH_ICON_SIZE);
				const uchar *m = stamp.mask.data() + (clip/2*maskdia) + clip/2;
				for(int y=0;y<BRUSH_ICON_SIZE;++y) {
					quint32 *scanline = reinterpret_cast<quint32*>(icon.scanLine(y));
					for(int x=0;x<BRUSH_ICON_SIZE;++x,++m) {
						*(scanline++) = qPremultiply((*m << 24) | color);
					}
					m += clip;
				}

			} else {
				// Center in the icon
				icon.fill(Qt::transparent);
				const uchar *m = stamp.mask.data();
				for(int y=0;y<maskdia;++y) {
					quint32 *scanline = reinterpret_cast<quint32*>(icon.scanLine(y+(BRUSH_ICON_SIZE-maskdia)/2)) + (BRUSH_ICON_SIZE-maskdia)/2;
					for(int x=0;x<maskdia;++x,++m) {
						*(scanline++) = qPremultiply((*m << 24) | color);
					}
				}
			}

			iconcache[idx] = QPixmap::fromImage(icon);
		}
		return iconcache.at(idx);
	}
};

BrushPresetModel::BrushPresetModel(QObject *parent)
	: QAbstractListModel(parent), d(new Private)
{
	loadBrushes();
	if(d->presets.isEmpty())
		makeDefaultBrushes();
}

BrushPresetModel::~BrushPresetModel()
{
	delete d;
}

BrushPresetModel *BrushPresetModel::getSharedInstance()
{
	static BrushPresetModel *m;
	if(!m)
		m = new BrushPresetModel;
	return m;
}

int BrushPresetModel::rowCount(const QModelIndex &parent) const
{
	if(parent.isValid())
		return 0;
	return d->presets.size();
}

QVariant BrushPresetModel::data(const QModelIndex &index, int role) const
{
	if(index.isValid() && index.row() >= 0 && index.row() < d->presets.size()) {
		switch(role) {
		case Qt::DecorationRole: return d->getIcon(index.row());
		case Qt::SizeHintRole: return QSize(BRUSH_ICON_SIZE, BRUSH_ICON_SIZE);
		case Qt::ToolTipRole: return d->presets.at(index.row()).value(brushprop::LABEL);
		case ToolPropertiesRole: return d->presets.at(index.row()).asVariant();
		}
	}
	return QVariant();
}

Qt::ItemFlags BrushPresetModel::flags(const QModelIndex &index) const
{
	if(index.isValid() && index.row() >= 0 && index.row() < d->presets.size()) {
		return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
	}
	return Qt::ItemIsSelectable | Qt::ItemIsEnabled |  Qt::ItemIsDropEnabled;
}

QMap<int,QVariant> BrushPresetModel::itemData(const QModelIndex &index) const
{
	QMap<int,QVariant> roles;
	if(index.isValid() && index.row()>=0 && index.row()<d->presets.size()) {
		roles[ToolPropertiesRole] = d->presets[index.row()].asVariant();
	}
	return roles;
}

bool BrushPresetModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if(!index.isValid() || index.row()<0 || index.row()>=d->presets.size())
		return false;
	switch(role) {
		case ToolPropertiesRole:
			d->presets[index.row()] = ToolProperties::fromVariant(value.toHash());
			d->iconcache[index.row()] = QPixmap();
			emit dataChanged(index, index);
			return true;
	}
	return false;
}

bool BrushPresetModel::setItemData(const QModelIndex &index, const QMap<int,QVariant> &roles)
{
	if(!index.isValid() || index.row()<0 || index.row()>=d->presets.size())
		return false;

	if(roles.contains(ToolPropertiesRole)) {
		d->presets[index.row()] = ToolProperties::fromVariant(roles[ToolPropertiesRole].toHash());
		d->iconcache[index.row()] = QPixmap();
		emit dataChanged(index, index);
		saveBrushes();
	}
	return true;
}

bool BrushPresetModel::insertRows(int row, int count, const QModelIndex &parent)
{
	if(parent.isValid())
		return false;
	if(row<0 || count<=0 || row > d->presets.size())
		return false;
	beginInsertRows(QModelIndex(), row, row+count-1);
	for(int i=0;i<count;++i) {
		d->presets.insert(row, ToolProperties());
		d->iconcache.insert(row, QPixmap());
	}
	endInsertRows();
	return true;
}

bool BrushPresetModel::removeRows(int row, int count, const QModelIndex &parent)
{
	if(parent.isValid())
		return false;
	if(row<0 || count<=0 || row+count > d->presets.size())
		return false;
	beginRemoveRows(QModelIndex(), row, row+count-1);
	d->presets.erase(d->presets.begin()+row, d->presets.begin()+row+count);
	d->iconcache.erase(d->iconcache.begin()+row, d->iconcache.begin()+row+count);
	endRemoveRows();
	saveBrushes();
	return true;
}

Qt::DropActions BrushPresetModel::supportedDropActions() const
{
	return Qt::MoveAction;
}

void BrushPresetModel::addBrush(const ToolProperties &brushProps)
{
	beginInsertRows(QModelIndex(), d->presets.size(), d->presets.size());
	d->presets.append(brushProps);
	d->iconcache.append(QPixmap());
	endInsertRows();
	saveBrushes();
}

void BrushPresetModel::loadBrushes()
{
	QSettings cfg;
	cfg.beginGroup("tools/brushpresets");
	int size = cfg.beginReadArray("preset");
	QList<ToolProperties> props;
	QList<QPixmap> iconcache;
	for(int i=0;i<size;++i) {
		cfg.setArrayIndex(i);
		props.append(ToolProperties::load(cfg));
		iconcache.append(QPixmap());
	}
	beginResetModel();
	d->presets = props;
	d->iconcache = iconcache;
	endResetModel();
}

void BrushPresetModel::saveBrushes() const
{
	QSettings cfg;
	cfg.beginGroup("tools/brushpresets");
	cfg.beginWriteArray("preset", d->presets.size());
	for(int i=0;i<d->presets.size();++i) {
		cfg.setArrayIndex(i);
		d->presets.at(i).save(cfg);
	}
	cfg.endArray();
}

void BrushPresetModel::makeDefaultBrushes()
{
	QList<ToolProperties> brushes;
	ToolProperties tp;

	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 0);
		tp.setValue(brushprop::size, 16);
		tp.setValue(brushprop::opacity, 100);
		tp.setValue(brushprop::spacing, 15);
		tp.setValue(brushprop::sizePressure, true);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 1);
		tp.setValue(brushprop::size, 10);
		tp.setValue(brushprop::opacity, 100);
		tp.setValue(brushprop::hard, 80);
		tp.setValue(brushprop::spacing, 15);
		tp.setValue(brushprop::sizePressure, true);
		tp.setValue(brushprop::opacityPressure, true);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 1);
		tp.setValue(brushprop::size, 30);
		tp.setValue(brushprop::opacity, 34);
		tp.setValue(brushprop::hard, 100);
		tp.setValue(brushprop::spacing, 18);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 0);
		tp.setValue(brushprop::incremental, false);
		tp.setValue(brushprop::size, 32);
		tp.setValue(brushprop::opacity, 65);
		tp.setValue(brushprop::spacing, 15);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 0);
		tp.setValue(brushprop::incremental, false);
		tp.setValue(brushprop::size, 70);
		tp.setValue(brushprop::opacity, 42);
		tp.setValue(brushprop::spacing, 15);
		tp.setValue(brushprop::opacityPressure, true);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 1);
		tp.setValue(brushprop::size, 113);
		tp.setValue(brushprop::opacity, 60);
		tp.setValue(brushprop::hard, 1);
		tp.setValue(brushprop::spacing, 19);
		tp.setValue(brushprop::opacityPressure, true);
		brushes << tp;
	}
	{
		ToolProperties tp;
		tp.setValue(brushprop::brushmode, 2);
		tp.setValue(brushprop::size, 43);
		tp.setValue(brushprop::opacity, 30);
		tp.setValue(brushprop::hard, 100);
		tp.setValue(brushprop::spacing, 25);
		tp.setValue(brushprop::smudge, 100);
		tp.setValue(brushprop::resmudge, 1);
		tp.setValue(brushprop::opacityPressure, true);
		brushes << tp;
	}

	// Make presets
	beginInsertRows(QModelIndex(), d->presets.size(), d->presets.size()+brushes.size()-1);
	d->presets << brushes;
	for(int i=0;i<brushes.size();++i)
		d->iconcache << QPixmap();
	endInsertRows();
}

}

