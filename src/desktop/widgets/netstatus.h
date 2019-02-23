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
#ifndef NETSTATUS_H
#define NETSTATUS_H

#include "net/server.h"

#include <QWidget>
#include <QUrl>
#include <QPointer>
#include <QSslCertificate>

class QLabel;
class QTimer;
class QProgressBar;

namespace dialogs {
	class NetStats;
}

namespace widgets {

class PopupMessage;

/**
 * @brief Network connection status widget
 * This widget displays the current status of the connection with the server
 * and the address of the host.
 */
class NetStatus : public QWidget
{
Q_OBJECT
public:
	NetStatus(QWidget *parent);

	void setSecurityLevel(net::Server::Security level, const QSslCertificate &certificate);

public slots:
	void connectingToHost(const QString& address, int port);
	void loggedIn(const QUrl &sessionUrl);
	void hostDisconnecting();
	void hostDisconnected();

	void setRoomcode(const QString &roomcode);

	void bytesReceived(int count);
	void bytesSent(int count);

	void lagMeasured(qint64 lag);

	//! Show the message in the balloon popup if alert is true
	void alertMessage(const QString &msg, bool alert);

	//! Update the download progress bar with message catchup progress (0-100)
	void setCatchupProgress(int progress);

	//! Update the download progress bar with file download progress (0-total)
	void setDownloadProgress(qint64 received, qint64 total);

	//! Download over, hide the progress bar
	void hideDownloadProgress();

	void join(int id, const QString& user);
	void leave(int id, const QString& user);

	//! This user was kicked off the session
	void kicked(const QString& user);

	void copyAddress();
	void copyUrl();

signals:
	//! A status message
	void statusMessage(const QString& message);

private slots:
	void discoverAddress();
	void externalIpDiscovered(const QString &ip);
	void showCertificate();
	void showNetStats();

private:
	void showCGNAlert();
	void message(const QString &msg);
	void updateLabel();

	enum { NotConnected, Connecting, LoggedIn, Disconnecting } m_state;

	QString fullAddress() const;

	QPointer<dialogs::NetStats> _netstats;
	QProgressBar *m_download;

	QLabel *m_label, *m_security;
	PopupMessage *m_popup;
	QString m_address;
	QString m_roomcode;
	int m_port;
	QUrl m_sessionUrl;

	bool m_hideServer;

	QAction *_copyaction;
	QAction *_urlaction;
	QAction *_discoverIp;

	quint64 _sentbytes, _recvbytes, _lag;

	QSslCertificate m_certificate;
};

}

#endif

