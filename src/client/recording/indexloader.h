/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2014-2016 Calle Laakkonen

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

#ifndef INDEXLOADER_H
#define INDEXLOADER_H

#include "index.h"

class KArchive;
class QImage;

namespace canvas {
	class StateSavepoint;
	class StateTracker;
}

namespace recording {

class IndexLoader
{
public:
	IndexLoader(const QString &recording, const QString &index);
	IndexLoader(const IndexLoader&) = delete;
	IndexLoader &operator=(const IndexLoader&) = delete;
	~IndexLoader();

	bool open();

	Index &index() { return m_index; }

	int thumbnailsAvailable() const { return m_thumbnailcount; }

	canvas::StateSavepoint loadSavepoint(int idx);
	QImage loadThumbnail(int idx);

private:
	QString m_recordingfile;
	KArchive *m_file;
	Index m_index;
	int m_thumbnailcount;
};

}

#endif // INDEXLOADER_H
