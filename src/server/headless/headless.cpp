/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2013-2018 Calle Laakkonen

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
#include "config.h"

#include "multiserver.h"
#include "initsys.h"
#include "sslserver.h"
#include "database.h"
#include "../shared/server/inmemoryconfig.h"
#include "configfile.h"

#ifdef HAVE_WEBADMIN
#include "webadmin/webadmin.h"
#endif

#include <QCoreApplication>
#include <QStringList>
#include <QSslSocket>
#include <QCommandLineParser>
#include <QDir>

#ifdef Q_OS_UNIX
#include "unixsignals.h"
#endif

namespace server {
namespace headless {

void printVersion()
{
	printf("drawpile-srv " DRAWPILE_VERSION "\n");
	printf("Protocol version: %d.%d\n", DRAWPILE_PROTO_MAJOR_VERSION, DRAWPILE_PROTO_MINOR_VERSION);
	printf("Qt version: %s (compiled against %s)\n", qVersion(), QT_VERSION_STR);
	printf("SSL library version: %s (%lu)\n", QSslSocket::sslLibraryVersionString().toLocal8Bit().constData(), QSslSocket::sslLibraryVersionNumber());
#ifdef HAVE_WEBADMIN
	printf("Libmicrohttpd version: %s\n", qPrintable(Webadmin::version()));
#else
	printf("Libmicrohttpd version: N/A\n");
#endif
}

bool start() {
	// Set up command line arguments
	QCommandLineParser parser;

	parser.setApplicationDescription("Standalone server for Drawpile");
	parser.addHelpOption();

	// --version, -v
	QCommandLineOption versionOption(QStringList() << "v" << "version", "Displays version information.");
	parser.addOption(versionOption);

#ifdef HAVE_SERVERGUI
	// --gui (this is just for the help text)
	QCommandLineOption guiOption(QStringList() << "gui", "Run the graphical version.");
	parser.addOption(guiOption);
#endif

	// --port, -p <port>
	QCommandLineOption portOption(QStringList() << "port" << "p", "Listening port", "port", QString::number(DRAWPILE_PROTO_DEFAULT_PORT));
	parser.addOption(portOption);

	// --listen, -l <address>
	QCommandLineOption listenOption(QStringList() << "listen" << "l", "Listening address", "address");
	parser.addOption(listenOption);

	// --local-host
	QCommandLineOption localAddr("local-host", "This server's hostname for session announcement", "hostname");
	parser.addOption(localAddr);

	// --announce-port <port>
	QCommandLineOption announcePortOption(QStringList() << "announce-port", "Port number to announce (set if forwarding from different port)", "port");
	parser.addOption(announcePortOption);

	// --ssl-cert <certificate file>
	QCommandLineOption sslCertOption("ssl-cert", "SSL certificate file", "certificate");
	parser.addOption(sslCertOption);

	// --ssl-key <key file>
	QCommandLineOption sslKeyOption("ssl-key", "SSL key file", "key");
	parser.addOption(sslKeyOption);

	// --secure, -S
	QCommandLineOption secureOption(QStringList() << "secure" << "S", "Mandatory SSL mode");
	parser.addOption(secureOption);

	// --record <path>
	QCommandLineOption recordOption("record", "Record sessions", "path");
	parser.addOption(recordOption);

#ifndef NDEBUG
	QCommandLineOption lagOption("random-lag", "Randomly sleep to simulate lag", "msecs", "0");
	parser.addOption(lagOption);
#endif

#ifdef HAVE_WEBADMIN
	// --web-admin-port <port>
	QCommandLineOption webadminPortOption("web-admin-port", "Web admin interface port", "port", "0");
	parser.addOption(webadminPortOption);

	// --web-admin-auth <user:password>
	QCommandLineOption webadminAuthOption("web-admin-auth", "Web admin username & password", "user:password");
	parser.addOption(webadminAuthOption);

	// --web-admin-access <address/subnet>
	QCommandLineOption webadminAccessOption("web-admin-access", "Set web admin access mask", "address/subnet|all");
	parser.addOption(webadminAccessOption);
#endif

	// --database, -d <filename>
	QCommandLineOption dbFileOption(QStringList() << "database" << "d", "Use configuration database", "filename");
	parser.addOption(dbFileOption);

	// --config, -c <filename>
	QCommandLineOption configFileOption(QStringList() << "config" << "c", "Use configuration file", "filename");
	parser.addOption(configFileOption);

	// --sessions, -s <path>
	QCommandLineOption sessionsOption(QStringList() << "sessions" << "s", "File backed sessions", "path");
	parser.addOption(sessionsOption);

	// --templates, -t <path>
	QCommandLineOption templatesOption(QStringList() << "templates" << "t", "Session templates", "path");
	parser.addOption(templatesOption);

	// --extauth <url>
#ifdef HAVE_LIBSODIUM
	QCommandLineOption extAuthOption(QStringList() << "extauth", "Extauth server URL", "url");
	parser.addOption(extAuthOption);
#endif

	// --report-url <url>
	QCommandLineOption reportUrlOption(QStringList() << "report-url", "Abuse report handler URL", "url");
	parser.addOption(reportUrlOption);

	// Parse
	parser.process(*QCoreApplication::instance());

	if(parser.isSet(versionOption)) {
		printVersion();
		::exit(0);
	}

	// Set server configuration file or database
	ServerConfig *serverconfig;
	if(parser.isSet(dbFileOption)) {
		if(parser.isSet(configFileOption)) {
			qCritical("Configuration file and database are mutually exclusive options");
			return false;
		}

		auto *db = new Database;
		if(!db->openFile(parser.value(dbFileOption))) {
			qCritical("Couldn't open database file %s", qPrintable(parser.value(dbFileOption)));
			delete db;
			return false;
		}
		serverconfig = db;

	} else if(parser.isSet(configFileOption)) {
		serverconfig = new ConfigFile(parser.value(configFileOption));

	} else {
		// No database or config file: just use the defaults
		serverconfig = new InMemoryConfig;
	}

	// Set internal server config
	InternalConfig icfg;
	icfg.localHostname = parser.value(localAddr);
#ifdef HAVE_LIBSODIUM
	icfg.extAuthUrl = parser.value(extAuthOption);
#endif
	icfg.reportUrl = parser.value(reportUrlOption);

	if(parser.isSet(announcePortOption)) {
		bool ok;
		icfg.announcePort = parser.value(announcePortOption).toInt(&ok);
		if(!ok || icfg.announcePort>0xffff) {
			qCritical("Invalid port %s", qPrintable(parser.value(announcePortOption)));
			return false;
		}
	}

	serverconfig->setInternalConfig(icfg);

	// Initialize the server
	server::MultiServer *server = new server::MultiServer(serverconfig);
	serverconfig->setParent(server);

	server->connect(server, SIGNAL(serverStopped()), QCoreApplication::instance(), SLOT(quit()));

	int port;
	{
		bool ok;
		port = parser.value(portOption).toInt(&ok);
		if(!ok || port<1 || port>0xffff) {
			qCritical("Invalid port %s", qPrintable(parser.value(portOption)));
			return false;
		}
	}

	QHostAddress address = QHostAddress::Any;
	{
		QString av = parser.value(listenOption);
		if(!av.isEmpty()) {
			if(!address.setAddress(av)) {
				qCritical("Invalid listening address %s", qPrintable(av));
				return false;
			}
		}
	}

	{
		QString sslCert = parser.value(sslCertOption);
		QString sslKey = parser.value(sslKeyOption);
		if(!sslCert.isEmpty() && !sslKey.isEmpty()) {
			server->setSslCertFile(sslCert, sslKey);
			server->setMustSecure(parser.isSet(secureOption));
			server::SslServer::requireForwardSecrecy();
		}
	}

	{
		QString recordingPath = parser.value(recordOption);
		if(!recordingPath.isEmpty()) {
			server->setRecordingPath(recordingPath);
		}
	}

	{
		QString sessionDirPath = parser.value(sessionsOption);
		if(!sessionDirPath.isEmpty()) {
			QDir sessionDir { sessionDirPath };
			if(!sessionDir.isReadable()) {
				qCritical("Cannot open %s", qPrintable(sessionDirPath));
				return false;
			} else {
				server->setSessionDirectory(sessionDir);
			}
		}
	}

	if(parser.isSet(templatesOption)) {
		QDir dir(parser.value(templatesOption));
		if(!dir.exists()) {
			qCritical("%s: template directory does not exist!", qPrintable(dir.absolutePath()));
			return false;
		}
		server->setTemplateDirectory(dir);
	}

#ifndef NDEBUG
	{
		uint lag = parser.value(lagOption).toUInt();
		server->setRandomLag(lag);
	}
#endif

#ifdef HAVE_WEBADMIN
	server::Webadmin *webadmin = new server::Webadmin;
	int webadminPort = parser.value(webadminPortOption).toInt();
	{
		QString auth = parser.value(webadminAuthOption);
		if(!auth.isEmpty())
			webadmin->setBasicAuth(auth);

		QString access = parser.value(webadminAccessOption);
		if(!access.isEmpty()) {
			if(!webadmin->setAccessSubnet(access)) {
				qCritical("Invalid subnet %s", qPrintable(access));
				return false;
			}
		}
#ifdef Q_OS_UNIX
	server->connect(UnixSignals::instance(), SIGNAL(sigUsr1()), webadmin, SLOT(restart()));
#endif
	}

#endif

	// Catch signals
#ifdef Q_OS_UNIX
	server->connect(UnixSignals::instance(), SIGNAL(sigInt()), server, SLOT(stop()));
	server->connect(UnixSignals::instance(), SIGNAL(sigTerm()), server, SLOT(stop()));
#endif

	// Start
	{
		QList<int> listenfds = initsys::getListenFds();
		if(listenfds.isEmpty()) {
			// socket activation not used
			if(!server->start(port, address))
				return false;

#ifdef HAVE_WEBADMIN
			if(webadminPort>0) {
				webadmin->setSessions(server);
				webadmin->start(webadminPort);
			}
#endif

		} else {
			// listening socket passed to us by the init system
			if(listenfds.size() > 2) {
				qCritical("Too many file descriptors received");
				return false;
			}

			server->setAutoStop(true);

			if(!server->startFd(listenfds[0]))
				return false;

			if(listenfds.size()>1) {
#ifdef HAVE_WEBADMIN
				webadmin->setSessions(server);
				webadmin->startFd(listenfds[1]);
#else
				qCritical("Web admin socket passed, but web admin support not built in!");
#endif
			}
		}
	}

	initsys::notifyReady();

	return true;
}

}
}
