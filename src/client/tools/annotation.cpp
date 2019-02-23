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

#include "canvas/canvasmodel.h"
#include "canvas/aclfilter.h"
#include "net/client.h"

#include "toolcontroller.h"
#include "annotation.h"
#include "utils.h"

#include "../shared/net/undo.h"
#include "../shared/net/annotation.h"

#include <QPixmap>

namespace tools {

Annotation::Annotation(ToolController &owner)
	: Tool(owner, ANNOTATION, QCursor(QPixmap(":cursors/text.png"), 2, 2))
{
}

/**
 * The annotation tool has fairly complex needs. Clicking on an existing
 * annotation selects it, otherwise a new annotation is started.
 */
void Annotation::begin(const paintcore::Point& point, bool right, float zoom)
{
	Q_UNUSED(right);

	const paintcore::Annotation *selection = owner.model()->layerStack()->annotations()->annotationAtPos(point.toPoint(), zoom);
	m_p1 = point;
	m_p2 = point;
	m_isNew = selection==nullptr;

	if(selection) {
		m_selectedId = selection->id;
		if(selection->protect && !owner.model()->aclFilter()->isLocalUserOperator() && selection->userId() != owner.client()->myId())
			m_handle = paintcore::Annotation::OUTSIDE;
		else
			m_handle = owner.model()->layerStack()->annotations()->annotationHandleAt(m_selectedId, point.toPoint(), zoom);
		owner.setActiveAnnotation(m_selectedId);

	} else {
		// No annotation, start creating a new one

		if(!owner.model()->aclFilter()->canUseFeature(canvas::Feature::CreateAnnotation)) {
			m_handle = paintcore::Annotation::OUTSIDE;
			return;
		}

		// I don't want to create a new preview item just for annotations,
		// so we create the preview annotation directly in the model. Since this
		// doesn't affect the other annotations, it shouldn't case any problems.
		// Also, we use a special ID for the preview object that is outside the protocol range.
		m_selectedId = PREVIEW_ID;

		owner.model()->layerStack()->annotations()->addAnnotation(m_selectedId, QRect(m_p1.toPoint(), m_p1.toPoint() + QPoint(5,5)));
		m_handle = paintcore::Annotation::RS_BOTTOMRIGHT;
	}
}

/**
 * If we have a selected annotation, move or resize it. Otherwise extend
 * the preview rectangle for the new annotation.
 */
void Annotation::motion(const paintcore::Point& point, bool constrain, bool center)
{
	Q_UNUSED(constrain);
	Q_UNUSED(center);

	if(m_handle == paintcore::Annotation::OUTSIDE)
		return;

	QPointF p = point - m_p2;
	m_handle = owner.model()->layerStack()->annotations()->annotationAdjustGeometry(m_selectedId, m_handle, p.toPoint());
	m_p2 = point;
}

/**
 * If we have a selected annotation, adjust its shape.
 * Otherwise, create a new annotation.
 */
void Annotation::end()
{
	if(!m_selectedId)
		return;

	QList<protocol::MessagePtr> msgs;
	const uint8_t contextId = owner.client()->myId();

	if(!m_isNew) {
		if(m_p1.toPoint() != m_p2.toPoint()) {
			const paintcore::Annotation *a = owner.model()->layerStack()->annotations()->getById(m_selectedId);
			if(a) {
				msgs << protocol::MessagePtr(new protocol::AnnotationReshape(contextId, m_selectedId, a->rect.x(), a->rect.y(), a->rect.width(), a->rect.height()));
			}

		} else {
			// if geometry was not changed, user merely clicked on the annotation
			// rather than dragging it. In that case, focus the text box to prepare
			// for content editing.

			// TODO reimplement
			//owner.toolSettings().getAnnotationSettings()->setFocusAt(_selected->getAnnotation()->cursorAt(_start.toPoint()));
		}

	} else if(m_handle != paintcore::Annotation::OUTSIDE) {

		QRect rect = QRect(m_p1.toPoint(), m_p2.toPoint()).normalized();

		if(rect.width() < 10 && rect.height() < 10) {
			// User created a tiny annotation, probably by clicking rather than dragging.
			// Create a nice and big annotation box rather than a minimum size one.
			rect.setSize(QSize(160, 60));
		}

		// Delete our preview annotation first
		owner.model()->layerStack()->annotations()->deleteAnnotation(PREVIEW_ID);

		uint16_t newId = owner.model()->getAvailableAnnotationId();

		if(newId==0) {
			qWarning("We ran out of annotation IDs!");
			return;
		}

		msgs << protocol::MessagePtr(new protocol::AnnotationCreate(contextId, newId, rect.x(), rect.y(), rect.width(), rect.height()));
	}

	if(!msgs.isEmpty()) {
		msgs.prepend(protocol::MessagePtr(new protocol::UndoPoint(contextId)));
		owner.client()->sendMessages(msgs);
	}
}

}

