/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2014-2018 Calle Laakkonen

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

#ifndef NETFILES_H
#define NETFILES_H

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QString;
class QUrl;
class QImage;
class QFile;
class QObject;

namespace widgets {
	class NetStatus;
}

namespace networkaccess {

/**
 * @brief Load a potentially large file from the network
 *
 * The file is first downloaded into a temporary file.
 *
 * @param url
 * @param expectType
 * @param context object which must exist for callback to be called
 * @param callback
 */
void getFile(const QUrl &url, const QString &expectType, widgets::NetStatus *netstatus, const QObject *context, std::function<void(QFile &file, const QString &errorMsg)> callback);

/**
 * @brief A convenience wrapepr aaround get() that expects an image in response
 *
 * If an error occurs, the callback is called with a null image and an error message.
 *
 * @param url the URL to fetch
 * @param netstatus the status widget whose progress meter to update
 * @param context object which must exist for callback to be called
 * @param callback the callback to call with the returned image or error message
 */
void getImage(const QUrl &url, widgets::NetStatus *netstatus, const QObject *context, std::function<void(const QImage &image, const QString &errorMsg)> callback);

}

#endif // NETWORKACCESS_H
