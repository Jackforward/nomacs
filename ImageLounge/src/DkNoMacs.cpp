/*******************************************************************************************************
 DkNoMacs.cpp
 Created on:	21.04.2011
 
 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances
 
 Copyright (C) 2011-2013 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2013 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2013 Florian Kleber <florian@nomacs.org>

 This file is part of nomacs.

 nomacs is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 nomacs is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *******************************************************************************************************/

#include "DkNoMacs.h"

// my stuff
#include "DkNetwork.h"
#include "DkViewPort.h"
#include "DkWidgets.h"
#include "DkDialog.h"
#include "DkSaveDialog.h"
#include "DkSettings.h"
#include "DkMenu.h"
#include "DkToolbars.h"
#include "DkManipulationWidgets.h"
#include "DkSettingsWidgets.h"
#include "DkMessageBox.h"
#include "DkMetaDataWidgets.h"
#include "DkThumbsWidgets.h"
#include "DkBatch.h"
#include "DkCentralWidget.h"
#include "DkMetaData.h"
#include "DkImageContainer.h"
#include "DkQuickAccess.h"
#include "DkError.h"
#include "DkUtils.h"
#include "DkControlWidget.h"
#include "DkImageLoader.h"
#include "DkTimer.h"

#ifdef  WITH_PLUGINS
#include "DkPluginInterface.h"
#include "DkPluginManager.h"
#endif //  WITH_PLUGINS

#pragma warning(push, 0)	// no warnings from includes - begin
#ifdef WITH_UPNP
#include "DkUpnp.h"
#endif // WITH_UPNP

#include <QBoxLayout>
#include <QShortcut>
#include <QResizeEvent>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QStatusBar>
#include <QPanGesture>
#include <QSplashScreen>
#include <QErrorMessage>
#include <QDesktopServices>
#include <QClipboard>
#include <QEvent>
#include <QSettings>
#include <QFileInfo>
#include <QTimer>
#include <QProcess>
#include <QStringBuilder>
#include <QDesktopWidget>
#include <QProgressDialog>
#include <QDrag>
#include <QVector2D>
#include <qmath.h>
#include <QMimeData>
#include <QNetworkProxyFactory>
#include <QInputDialog>
#include <QApplication>
#pragma warning(pop)		// no warnings from includes - end

#if defined(WIN32) && !defined(SOCK_STREAM)
#include <winsock2.h>	// needed since libraw 0.16
#endif

namespace nmc {

DkNomacsOSXEventFilter::DkNomacsOSXEventFilter(QObject *parent) : QObject(parent) {
}

/*! Handle QFileOpenEvent for mac here */
bool DkNomacsOSXEventFilter::eventFilter(QObject *obj, QEvent *event) {
	if (event->type() == QEvent::FileOpen) {
		emit loadFile(static_cast<QFileOpenEvent*>(event)->file());
		return true;
	}
	return QObject::eventFilter(obj, event);
}

DkNoMacs::DkNoMacs(QWidget *parent, Qt::WindowFlags flags)
	: QMainWindow(parent, flags) {

	QMainWindow::setWindowTitle("nomacs | Image Lounge");
	setObjectName("DkNoMacs");

	registerFileVersion();

	mSaveSettings = true;

	// load settings
	//DkSettings::load();
	
	mOpenDialog = 0;
	mSaveDialog = 0;
	mThumbSaver = 0;
	mResizeDialog = 0;
	mOpacityDialog = 0;
	mUpdater = 0;
	mTranslationUpdater = 0;
	mImgManipulationDialog = 0;
	mExportTiffDialog = 0;
	mUpdateDialog = 0;
	mProgressDialog = 0;
	mProgressDialogTranslations = 0;
	mForceDialog = 0;
	mTrainDialog = 0;
	mExplorer = 0;
	mMetaDataDock = 0;
	mAppManager = 0;
	mSettingsDialog = 0;
	mPrintPreviewDialog = 0;
	mThumbsDock = 0;
	mQuickAccess = 0;
#ifdef WITH_QUAZIP
	mArchiveExtractionDialog = 0;
#endif 

	mActivePlugin = QString();

	// start localhost client/server
	//localClientManager = new DkLocalClientManager(windowTitle());
	//localClientManger->start();

	mOldGeometry = geometry();
	mOverlaid = false;

	mMenu = new DkMenuBar(this, -1);

	resize(850, 504);
	setMinimumSize(20, 20);

	double an = std::pow(3987, 12);
	double bn = std::pow(4365, 12);

	qDebug() << "3987 ^ 12 + 4365 ^ 12 = " << std::pow(an + bn, 1/12.0) << "^ 12";
	qDebug() << "Sorry Fermat, but the Simpsons are right.";

}

DkNoMacs::~DkNoMacs() {
	release();
}

void DkNoMacs::release() {
}

void DkNoMacs::init() {

// assign icon -> in windows the 32px version
#ifdef WIN32
	QString iconPath = ":/nomacs/img/nomacs32.png";
#else
	QString iconPath = ":/nomacs/img/nomacs.png";
#endif

	loadStyleSheet();

	QIcon nmcIcon = QIcon(iconPath);
	setObjectName("DkNoMacs");
	
	if (!nmcIcon.isNull())
		setWindowIcon(nmcIcon);

	mAppManager = new DkAppManager(this);
	connect(mAppManager, SIGNAL(openFileSignal(QAction*)), this, SLOT(openFileWith(QAction*)));

	// shortcuts and actions
	createIcons();
	createActions();
	createShortcuts();
	createMenu();
	createContextMenu();
	createToolbar();
	createStatusbar();
	enableNoImageActions(false);

	// add actions since they are ignored otherwise if the menu is hidden
	centralWidget()->addActions(mFileActions.toList());
	centralWidget()->addActions(mSortActions.toList());
	centralWidget()->addActions(mEditActions.toList());
	centralWidget()->addActions(mToolsActions.toList());
	centralWidget()->addActions(mPanelActions.toList());
	centralWidget()->addActions(mViewActions.toList());
	centralWidget()->addActions(mSyncActions.toList());
	centralWidget()->addActions(mPluginsActions.toList());
	centralWidget()->addActions(mHelpActions.toList());
	
	// automatically add status tip as tool tip
	for (int idx = 0; idx < mFileActions.size(); idx++)
		mFileActions[idx]->setToolTip(mFileActions[idx]->statusTip());
	// automatically add status tip as tool tip
	for (int idx = 0; idx < mSortActions.size(); idx++)
		mSortActions[idx]->setToolTip(mSortActions[idx]->statusTip());
	for (int idx = 0; idx < mEditActions.size(); idx++)
		mEditActions[idx]->setToolTip(mEditActions[idx]->statusTip());
	for (int idx = 0; idx < mToolsActions.size(); idx++)
		mToolsActions[idx]->setToolTip(mToolsActions[idx]->statusTip());
	for (int idx = 0; idx < mPanelActions.size(); idx++)
		mPanelActions[idx]->setToolTip(mPanelActions[idx]->statusTip());
	for (int idx = 0; idx < mViewActions.size(); idx++)
		mViewActions[idx]->setToolTip(mViewActions[idx]->statusTip());
	for (int idx = 0; idx < mSyncActions.size(); idx++)
		mSyncActions[idx]->setToolTip(mSyncActions[idx]->statusTip());
	for (int idx = 0; idx < mPluginsActions.size(); idx++)
		mPluginsActions[idx]->setToolTip(mPluginsActions[idx]->statusTip());
	for (int idx = 0; idx < mHelpActions.size(); idx++)
		mHelpActions[idx]->setToolTip(mHelpActions[idx]->statusTip());


	// TODO - just for android register me as a gesture recognizer
	grabGesture(Qt::PanGesture);
	grabGesture(Qt::PinchGesture);
	grabGesture(Qt::SwipeGesture);

	// load the window at the same position as last time
	readSettings();
	installEventFilter(this);

	showMenuBar(DkSettings::app.showMenuBar);
	showToolbar(DkSettings::app.showToolBar);
	showStatusBar(DkSettings::app.showStatusBar);

	// connects that are needed in all viewers
	connect(viewport(), SIGNAL(showStatusBar(bool, bool)), this, SLOT(showStatusBar(bool, bool)));
	connect(viewport(), SIGNAL(statusInfoSignal(const QString&, int)), this, SLOT(showStatusMessage(const QString&, int)));
	connect(viewport()->getController()->getCropWidget(), SIGNAL(statusInfoSignal(const QString&)), this, SLOT(showStatusMessage(const QString&)));
	connect(viewport(), SIGNAL(enableNoImageSignal(bool)), this, SLOT(enableNoImageActions(bool)));

	// connections to the image loader
	//connect(this, SIGNAL(saveTempFileSignal(QImage)), mViewport()->getImageLoader(), SLOT(saveTempFile(QImage)));
	connect(getTabWidget(), SIGNAL(imageUpdatedSignal(QSharedPointer<DkImageContainerT>)), this, SLOT(setWindowTitle(QSharedPointer<DkImageContainerT>)));
	connect(getTabWidget(), SIGNAL(imageHasGPSSignal(bool)), mViewActions[menu_view_gps_map], SLOT(setEnabled(bool)));

	connect(viewport()->getController()->getCropWidget(), SIGNAL(showToolbar(QToolBar*, bool)), this, SLOT(showToolbar(QToolBar*, bool)));
	connect(viewport(), SIGNAL(movieLoadedSignal(bool)), this, SLOT(enableMovieActions(bool)));
	connect(viewport()->getController()->getFilePreview(), SIGNAL(showThumbsDockSignal(bool)), this, SLOT(showThumbsDock(bool)));
	connect(centralWidget(), SIGNAL(statusInfoSignal(const QString&, int)), this, SLOT(showStatusMessage(const QString&, int)));

	getTabWidget()->getThumbScrollWidget()->registerAction(mPanelActions[menu_panel_thumbview]);
	getTabWidget()->getRecentFilesWidget()->registerAction(mFileActions[menu_file_show_recent]);
	viewport()->getController()->getFilePreview()->registerAction(mPanelActions[menu_panel_preview]);
	viewport()->getController()->getMetaDataWidget()->registerAction(mPanelActions[menu_panel_exif]);
	viewport()->getController()->getPlayer()->registerAction(mPanelActions[menu_panel_player]);
	viewport()->getController()->getCropWidget()->registerAction(mEditActions[menu_edit_crop]);
	viewport()->getController()->getFileInfoLabel()->registerAction(mPanelActions[menu_panel_info]);
	viewport()->getController()->getHistogram()->registerAction(mPanelActions[menu_panel_histogram]);
	viewport()->getController()->getCommentWidget()->registerAction(mPanelActions[menu_panel_comment]);

	viewport()->getController()->getScroller()->registerAction(mPanelActions[menu_panel_scroller]);

	enableMovieActions(false);

// clean up nomacs
#ifdef WIN32
	if (!nmc::DkSettings::global.setupPath.isEmpty() && QApplication::applicationVersion() == nmc::DkSettings::global.setupVersion) {

		// ask for exists - otherwise we always try to delete it if the user deleted it
		if (!QFileInfo(nmc::DkSettings::global.setupPath).exists() || QFile::remove(nmc::DkSettings::global.setupPath)) {
			nmc::DkSettings::global.setupPath = "";
			nmc::DkSettings::global.setupVersion = "";
			DkSettings::save();
		}
	}
#endif // Q_WS_WIN

	//QTimer::singleShot(0, this, SLOT(onWindowLoaded()));
}

#ifdef WIN32	// windows specific versioning
//#include <windows.h>
//#undef min
//#undef max
//#include <stdio.h>
//#include <string>

void DkNoMacs::registerFileVersion() {
	
	// this function is based on code from:
	// http://stackoverflow.com/questions/316626/how-do-i-read-from-a-version-resource-in-visual-c

	QString version(NOMACS_VERSION);	// default version (we do not know the build)

	try {
		// get the filename of the executable containing the version resource
		TCHAR szFilename[MAX_PATH + 1] = {0};
		if (GetModuleFileName(NULL, szFilename, MAX_PATH) == 0) {
			DkFileException("Sorry, I can't read the module fileInfo name\n", __LINE__, __FILE__);
		}

		// allocate a block of memory for the version info
		DWORD dummy;
		DWORD dwSize = GetFileVersionInfoSize(szFilename, &dummy);
		if (dwSize == 0) {
			throw DkFileException("The version info size is zero\n", __LINE__, __FILE__);
		}
		std::vector<BYTE> bytes(dwSize);

		if (bytes.empty())
			throw DkFileException("The version info is empty\n", __LINE__, __FILE__);

		// load the version info
		if (!bytes.empty() && !GetFileVersionInfo(szFilename, NULL, dwSize, &bytes[0])) {
			throw DkFileException("Sorry, I can't read the version info\n", __LINE__, __FILE__);
		}

		// get the name and version strings
		UINT                uiVerLen = 0;
		VS_FIXEDFILEINFO*   pFixedInfo = 0;     // pointer to fixed file info structure

		if (!bytes.empty() && !VerQueryValue(&bytes[0], TEXT("\\"), (void**)&pFixedInfo, (UINT *)&uiVerLen)) {
			throw DkFileException("Sorry, I can't get the version values...\n", __LINE__, __FILE__);
		}

		// pFixedInfo contains a lot more information...
		version = QString::number(HIWORD(pFixedInfo->dwFileVersionMS)) % "."
			% QString::number(LOWORD(pFixedInfo->dwFileVersionMS)) % "."
			% QString::number(HIWORD(pFixedInfo->dwFileVersionLS)) % "."
			% QString::number(LOWORD(pFixedInfo->dwFileVersionLS));

	} catch (DkFileException dfe) {
		qDebug() << QString::fromStdString(dfe.Msg());
	}

	QApplication::setApplicationVersion(version);


}
#else
	void DkNoMacs::registerFileVersion() {
		QString version(NOMACS_VERSION);	// default version (we do not know the build)
		QApplication::setApplicationVersion(version);
	}
#endif

void DkNoMacs::createToolbar() {

	mToolbar = new DkMainToolBar(tr("Edit"), this);
	mToolbar->setObjectName("EditToolBar");

	if (DkSettings::display.smallIcons)
		mToolbar->setIconSize(QSize(16, 16));
	else
		mToolbar->setIconSize(QSize(32, 32));
	
	qDebug() << mToolbar->styleSheet();

	if (DkSettings::display.toolbarGradient) {
		mToolbar->setObjectName("toolBarWithGradient");
	}

	mToolbar->addAction(mFileActions[menu_file_prev]);
	mToolbar->addAction(mFileActions[menu_file_next]);
	mToolbar->addSeparator();

	mToolbar->addAction(mFileActions[menu_file_open]);
	mToolbar->addAction(mFileActions[menu_file_open_dir]);
	mToolbar->addAction(mFileActions[menu_file_save]);
	mToolbar->addAction(mToolsActions[menu_tools_filter]);
	mToolbar->addSeparator();

	// edit
	mToolbar->addAction(mEditActions[menu_edit_copy]);
	mToolbar->addAction(mEditActions[menu_edit_paste]);
	mToolbar->addSeparator();

	// edit
	mToolbar->addAction(mEditActions[menu_edit_rotate_ccw]);
	mToolbar->addAction(mEditActions[menu_edit_rotate_cw]);
	mToolbar->addSeparator();

	mToolbar->addAction(mEditActions[menu_edit_crop]);
	mToolbar->addAction(mEditActions[menu_edit_transform]);
	//toolbar->addAction(editActions[menu_edit_image_manipulation]);
	mToolbar->addSeparator();

	// view
	mToolbar->addAction(mViewActions[menu_view_fullscreen]);
	mToolbar->addAction(mViewActions[menu_view_reset]);
	mToolbar->addAction(mViewActions[menu_view_100]);
	mToolbar->addSeparator();

	mToolbar->addAction(mViewActions[menu_view_gps_map]);

	mMovieToolbar = addToolBar(tr("Movie Toolbar"));
	mMovieToolbar->setObjectName("movieToolbar");
	//movieToolbar->addSeparator();
	mMovieToolbar->addAction(mViewActions[menu_view_movie_prev]);
	mMovieToolbar->addAction(mViewActions[menu_view_movie_pause]);
	mMovieToolbar->addAction(mViewActions[menu_view_movie_next]);

	if (DkSettings::display.toolbarGradient)
		mMovieToolbar->setObjectName("toolBarWithGradient");

	if (DkSettings::display.smallIcons)
		mMovieToolbar->setIconSize(QSize(16, 16));
	else
		mMovieToolbar->setIconSize(QSize(32, 32));

	mToolbar->allActionsAdded();

	addToolBar(mToolbar);
}


void DkNoMacs::createStatusbar() {

	mStatusbarLabels.resize(status_end);

	mStatusbarLabels[status_pixel_info] = new QLabel();
	mStatusbarLabels[status_pixel_info]->hide();
	mStatusbarLabels[status_pixel_info]->setToolTip(tr("CTRL activates the crosshair cursor"));

	mStatusbar = new QStatusBar(this);
	mStatusbar->setObjectName("DkStatusBar");
	QColor col = QColor(200, 200, 230, 100);

	if (DkSettings::display.toolbarGradient)
		mStatusbar->setObjectName("statusBarWithGradient");	

	mStatusbar->addWidget(mStatusbarLabels[status_pixel_info]);
	mStatusbar->hide();

	for (int idx = 1; idx < mStatusbarLabels.size(); idx++) {
		mStatusbarLabels[idx] = new QLabel(this);
		mStatusbarLabels[idx]->setObjectName("statusBarLabel");
		mStatusbarLabels[idx]->hide();
		mStatusbar->addPermanentWidget(mStatusbarLabels[idx]);
	}

	//statusbar->addPermanentWidget()
	this->setStatusBar(mStatusbar);
}

void DkNoMacs::loadStyleSheet() {
	
	// TODO: if we first load from disk, people can style nomacs themselves
	QFileInfo cssInfo(QCoreApplication::applicationDirPath(), "nomacs.css");

	if (!cssInfo.exists())
		cssInfo = QFileInfo(":/nomacs/stylesheet.css");

	QFile file(cssInfo.absoluteFilePath());

	if (file.open(QFile::ReadOnly)) {

		QString cssString = file.readAll();

		QColor hc = DkSettings::display.highlightColor;
		hc.setAlpha(150);

		// replace color placeholders
		cssString.replace("HIGHLIGHT_COLOR", DkUtils::colorToString(DkSettings::display.highlightColor));
		cssString.replace("HIGHLIGHT_LIGHT", DkUtils::colorToString(hc));
		cssString.replace("HUD_BACKGROUND_COLOR", DkUtils::colorToString(DkSettings::display.bgColorWidget));
		cssString.replace("HUD_FONT_COLOR", DkUtils::colorToString(QColor(255,255,255)));
		cssString.replace("BACKGROUND_COLOR", DkUtils::colorToString(DkSettings::display.bgColor));
		cssString.replace("WINDOW_COLOR", DkUtils::colorToString(QPalette().color(QPalette::Window)));

		qApp->setStyleSheet(cssString);
		file.close();

		qDebug() << "CSS loaded from: " << cssInfo.absoluteFilePath();
		//qDebug() << "style: \n" << cssString;
	}
}

void DkNoMacs::createIcons() {
	
	// this is unbelievable dirty - but for now the quickest way to turn themes off if someone uses customized icons...
	if (DkSettings::display.defaultIconColor) {
		#define ICON(theme, backup) QIcon::fromTheme((theme), QIcon((backup)))
	}
	else {
		#undef ICON
		#define ICON(theme, backup) QIcon(backup), QIcon(backup)
	}

	mFileIcons.resize(icon_file_end);
	mFileIcons[icon_file_dir] = ICON("document-open-folder", ":/nomacs/img/dir.png");
	mFileIcons[icon_file_open] = ICON("document-open", ":/nomacs/img/open.png");
	mFileIcons[icon_file_save] = ICON("document-save", ":/nomacs/img/save.png");
	mFileIcons[icon_file_print] = ICON("document-print", ":/nomacs/img/printer.png");
	mFileIcons[icon_file_open_large] = ICON("document-open-large", ":/nomacs/img/open-large.png");
	mFileIcons[icon_file_dir_large] = ICON("document-open-folder-large", ":/nomacs/img/dir-large.png");
	mFileIcons[icon_file_prev] = ICON("go-previous", ":/nomacs/img/previous.png");
	mFileIcons[icon_file_next] = ICON("go-next", ":/nomacs/img/next.png");
	mFileIcons[icon_file_filter] = QIcon();
	mFileIcons[icon_file_filter].addPixmap(QPixmap(":/nomacs/img/filter.png"), QIcon::Normal, QIcon::On);
	mFileIcons[icon_file_filter].addPixmap(QPixmap(":/nomacs/img/nofilter.png"), QIcon::Normal, QIcon::Off);
	
	mEditIcons.resize(icon_edit_end);
	mEditIcons[icon_edit_rotate_cw] = ICON("object-rotate-right", ":/nomacs/img/rotate-cw.png");
	mEditIcons[icon_edit_rotate_ccw] = ICON("object-rotate-left", ":/nomacs/img/rotate-cc.png");
	mEditIcons[icon_edit_crop] = ICON("object-edit-crop", ":/nomacs/img/crop.png");
	mEditIcons[icon_edit_resize] = ICON("object-edit-resize", ":/nomacs/img/resize.png");
	mEditIcons[icon_edit_copy] = ICON("object-edit-copy", ":/nomacs/img/copy.png");
	mEditIcons[icon_edit_paste] = ICON("object-edit-paste", ":/nomacs/img/paste.png");
	mEditIcons[icon_edit_delete] = ICON("object-edit-delete", ":/nomacs/img/trash.png");

	mViewIcons.resize(icon_view_end);
	mViewIcons[icon_view_fullscreen] = ICON("view-fullscreen", ":/nomacs/img/fullscreen.png");
	mViewIcons[icon_view_reset] = ICON("zoom-draw", ":/nomacs/img/zoomReset.png");
	mViewIcons[icon_view_100] = ICON("zoom-original", ":/nomacs/img/zoom100.png");
	mViewIcons[icon_view_gps] = ICON("", ":/nomacs/img/gps-globe.png");
	mViewIcons[icon_view_movie_play] = QIcon();
	mViewIcons[icon_view_movie_play].addPixmap(QPixmap(":/nomacs/img/movie-play.png"), QIcon::Normal, QIcon::On);
	mViewIcons[icon_view_movie_play].addPixmap(QPixmap(":/nomacs/img/movie-pause.png"), QIcon::Normal, QIcon::Off);
	mViewIcons[icon_view_movie_prev] = ICON("", ":/nomacs/img/movie-prev.png");
	mViewIcons[icon_view_movie_next] = ICON("", ":/nomacs/img/movie-next.png");

	mToolsIcons.resize(icon_tools_end);
	mToolsIcons[icon_tools_manipulation] = ICON("", ":/nomacs/img/manipulation.png");

	if (!DkSettings::display.defaultIconColor || DkSettings::app.privateMode)
		colorizeIcons(DkSettings::display.iconColor);
}

void DkNoMacs::createMenu() {

	setMenuBar(mMenu);
	mFileMenu = mMenu->addMenu(tr("&File"));
	mFileMenu->addAction(mFileActions[menu_file_open]);
	mFileMenu->addAction(mFileActions[menu_file_open_dir]);
	
	mOpenWithMenu = new QMenu(tr("Open &With"), mFileMenu);
	createOpenWithMenu(mOpenWithMenu);
	mFileMenu->addMenu(mOpenWithMenu);
	mFileMenu->addAction(mFileActions[menu_file_quick_launch]);

	mFileMenu->addSeparator();
	mFileMenu->addAction(mFileActions[menu_file_save]);
	mFileMenu->addAction(mFileActions[menu_file_save_as]);
	mFileMenu->addAction(mFileActions[menu_file_save_web]);
	mFileMenu->addAction(mFileActions[menu_file_rename]);
	mFileMenu->addSeparator();

	mFileMenu->addAction(mFileActions[menu_file_show_recent]);

	mFileMenu->addSeparator();
	mFileMenu->addAction(mFileActions[menu_file_print]);
	mFileMenu->addSeparator();
	
	mSortMenu = new QMenu(tr("S&ort"), mFileMenu);
	mSortMenu->addAction(mSortActions[menu_sort_filename]);
	mSortMenu->addAction(mSortActions[menu_sort_date_created]);
	mSortMenu->addAction(mSortActions[menu_sort_date_modified]);
	mSortMenu->addAction(mSortActions[menu_sort_random]);
	mSortMenu->addSeparator();
	mSortMenu->addAction(mSortActions[menu_sort_ascending]);
	mSortMenu->addAction(mSortActions[menu_sort_descending]);

	mFileMenu->addMenu(mSortMenu);
	mFileMenu->addAction(mFileActions[menu_file_recursive]);

	mFileMenu->addAction(mFileActions[menu_file_goto]);
	mFileMenu->addAction(mFileActions[menu_file_find]);
	mFileMenu->addAction(mFileActions[menu_file_reload]);
	mFileMenu->addAction(mFileActions[menu_file_prev]);
	mFileMenu->addAction(mFileActions[menu_file_next]);
	mFileMenu->addSeparator();
	//fileMenu->addAction(fileActions[menu_file_share_fb]);
	//fileMenu->addSeparator();
	mFileMenu->addAction(mFileActions[menu_file_train_format]);
	mFileMenu->addSeparator();
	mFileMenu->addAction(mFileActions[menu_file_new_instance]);
	mFileMenu->addAction(mFileActions[menu_file_private_instance]);
	mFileMenu->addAction(mFileActions[menu_file_exit]);

	mEditMenu = mMenu->addMenu(tr("&Edit"));
	mEditMenu->addAction(mEditActions[menu_edit_copy]);
	mEditMenu->addAction(mEditActions[menu_edit_copy_buffer]);
	mEditMenu->addAction(mEditActions[menu_edit_paste]);
	mEditMenu->addAction(mEditActions[menu_edit_delete]);
	mEditMenu->addSeparator();
	mEditMenu->addAction(mEditActions[menu_edit_rotate_ccw]);
	mEditMenu->addAction(mEditActions[menu_edit_rotate_cw]);
	mEditMenu->addAction(mEditActions[menu_edit_rotate_180]);
	mEditMenu->addSeparator();

	mEditMenu->addAction(mEditActions[menu_edit_transform]);
	mEditMenu->addAction(mEditActions[menu_edit_crop]);
	mEditMenu->addAction(mEditActions[menu_edit_flip_h]);
	mEditMenu->addAction(mEditActions[menu_edit_flip_v]);
	mEditMenu->addSeparator();
	mEditMenu->addAction(mEditActions[menu_edit_auto_adjust]);
	mEditMenu->addAction(mEditActions[menu_edit_norm]);
	mEditMenu->addAction(mEditActions[menu_edit_invert]);
	mEditMenu->addAction(mEditActions[menu_edit_gray_convert]);
#ifdef WITH_OPENCV
	mEditMenu->addAction(mEditActions[menu_edit_unsharp]);
	mEditMenu->addAction(mEditActions[menu_edit_tiny_planet]);
#endif
	mEditMenu->addSeparator();
#ifdef WIN32
	mEditMenu->addAction(mEditActions[menu_edit_wallpaper]);
	mEditMenu->addSeparator();
#endif
	mEditMenu->addAction(mEditActions[menu_edit_shortcuts]);
	mEditMenu->addAction(mEditActions[menu_edit_preferences]);

	mViewMenu = mMenu->addMenu(tr("&View"));
	
	mViewMenu->addAction(mViewActions[menu_view_frameless]);	
	mViewMenu->addAction(mViewActions[menu_view_fullscreen]);
	mViewMenu->addSeparator();

	mViewMenu->addAction(mViewActions[menu_view_new_tab]);
	mViewMenu->addAction(mViewActions[menu_view_close_tab]);
	mViewMenu->addAction(mViewActions[menu_view_previous_tab]);
	mViewMenu->addAction(mViewActions[menu_view_next_tab]);
	mViewMenu->addSeparator();

	mViewMenu->addAction(mViewActions[menu_view_reset]);
	mViewMenu->addAction(mViewActions[menu_view_100]);
	mViewMenu->addAction(mViewActions[menu_view_fit_frame]);
	mViewMenu->addAction(mViewActions[menu_view_zoom_in]);
	mViewMenu->addAction(mViewActions[menu_view_zoom_out]);
	mViewMenu->addSeparator();

	mViewMenu->addAction(mViewActions[menu_view_tp_pattern]);
	mViewMenu->addAction(mViewActions[menu_view_anti_aliasing]);
	mViewMenu->addSeparator();

	mViewMenu->addAction(mViewActions[menu_view_opacity_change]);
	mViewMenu->addAction(mViewActions[menu_view_opacity_up]);
	mViewMenu->addAction(mViewActions[menu_view_opacity_down]);
	mViewMenu->addAction(mViewActions[menu_view_opacity_an]);
#ifdef WIN32
	mViewMenu->addAction(mViewActions[menu_view_lock_window]);
#endif
	mViewMenu->addSeparator();

	mViewMenu->addAction(mViewActions[menu_view_movie_pause]);
	mViewMenu->addAction(mViewActions[menu_view_movie_prev]);
	mViewMenu->addAction(mViewActions[menu_view_movie_next]);

	mViewMenu->addSeparator();
	mViewMenu->addAction(mViewActions[menu_view_gps_map]);

	mPanelMenu = mMenu->addMenu(tr("&Panels"));
	mPanelToolsMenu = mPanelMenu->addMenu(tr("Tool&bars"));
	mPanelToolsMenu->addAction(mPanelActions[menu_panel_menu]);
	mPanelToolsMenu->addAction(mPanelActions[menu_panel_toolbar]);
	mPanelToolsMenu->addAction(mPanelActions[menu_panel_statusbar]);
	mPanelToolsMenu->addAction(mPanelActions[menu_panel_transfertoolbar]);
	mPanelMenu->addAction(mPanelActions[menu_panel_explorer]);
	mPanelMenu->addAction(mPanelActions[menu_panel_metadata_dock]);
	mPanelMenu->addAction(mPanelActions[menu_panel_preview]);
	mPanelMenu->addAction(mPanelActions[menu_panel_thumbview]);
	mPanelMenu->addAction(mPanelActions[menu_panel_scroller]);
	mPanelMenu->addAction(mPanelActions[menu_panel_exif]);
	
	mPanelMenu->addSeparator();
	
	mPanelMenu->addAction(mPanelActions[menu_panel_overview]);
	mPanelMenu->addAction(mPanelActions[menu_panel_player]);
	mPanelMenu->addAction(mPanelActions[menu_panel_info]);
	mPanelMenu->addAction(mPanelActions[menu_panel_histogram]);
	mPanelMenu->addAction(mPanelActions[menu_panel_comment]);

	mToolsMenu = mMenu->addMenu(tr("&Tools"));
	mToolsMenu->addAction(mToolsActions[menu_tools_thumbs]);
	mToolsMenu->addAction(mToolsActions[menu_tools_filter]);
#ifdef WITH_OPENCV
	mToolsMenu->addAction(mToolsActions[menu_tools_manipulation]);
#endif
#ifdef WITH_LIBTIFF
	mToolsMenu->addAction(mToolsActions[menu_tools_export_tiff]);
#endif
#ifdef WITH_QUAZIP
	mToolsMenu->addAction(mToolsActions[menu_tools_extract_archive]);
#endif
#ifdef WITH_OPENCV
	mToolsMenu->addAction(mToolsActions[menu_tools_mosaic]);
#endif
	mToolsMenu->addAction(mToolsActions[menu_tools_batch]);

	// no sync menu in frameless view
	if (DkSettings::app.appMode != DkSettings::mode_frameless)
		mSyncMenu = mMenu->addMenu(tr("&Sync"));
	else 
		mSyncMenu = 0;

#ifdef WITH_PLUGINS
	// plugin menu
	mPluginsMenu = mMenu->addMenu(tr("Pl&ugins"));
	connect(mPluginsMenu, SIGNAL(aboutToShow()), this, SLOT(createPluginsMenu()));
#endif // WITH_PLUGINS

	mHelpMenu = mMenu->addMenu(tr("&?"));
#ifndef Q_WS_X11
	mHelpMenu->addAction(mHelpActions[menu_help_update]);
#endif // !Q_WS_X11
	mHelpMenu->addAction(mHelpActions[menu_help_update_translation]);
	mHelpMenu->addSeparator();
	mHelpMenu->addAction(mHelpActions[menu_help_bug]);
	mHelpMenu->addAction(mHelpActions[menu_help_feature]);
	mHelpMenu->addSeparator();
	mHelpMenu->addAction(mHelpActions[menu_help_documentation]);
	mHelpMenu->addAction(mHelpActions[menu_help_about]);

}

void DkNoMacs::createOpenWithMenu(QMenu*) {

	QList<QAction* > oldActions = mOpenWithMenu->findChildren<QAction* >();

	// remove old actions
	for (int idx = 0; idx < oldActions.size(); idx++)
		viewport()->removeAction(oldActions.at(idx));

	QVector<QAction* > appActions = mAppManager->getActions();

	for (int idx = 0; idx < appActions.size(); idx++)
		qDebug() << "adding action: " << appActions[idx]->text() << " " << appActions[idx]->toolTip();

	assignCustomShortcuts(appActions);
	mOpenWithMenu->addActions(appActions.toList());
	
	if (!appActions.empty())
		mOpenWithMenu->addSeparator();
	mOpenWithMenu->addAction(mFileActions[menu_file_app_manager]);
	centralWidget()->addActions(appActions.toList());
}

void DkNoMacs::createContextMenu() {

	mContextMenu = new QMenu(this);

	mContextMenu->addAction(mPanelActions[menu_panel_explorer]);
	mContextMenu->addAction(mPanelActions[menu_panel_metadata_dock]);
	mContextMenu->addAction(mPanelActions[menu_panel_preview]);
	mContextMenu->addAction(mPanelActions[menu_panel_thumbview]);
	mContextMenu->addAction(mPanelActions[menu_panel_scroller]);
	mContextMenu->addAction(mPanelActions[menu_panel_exif]);
	mContextMenu->addAction(mPanelActions[menu_panel_overview]);
	mContextMenu->addAction(mPanelActions[menu_panel_player]);
	mContextMenu->addAction(mPanelActions[menu_panel_info]);
	mContextMenu->addAction(mPanelActions[menu_panel_histogram]);
	mContextMenu->addAction(mPanelActions[menu_panel_comment]);
	mContextMenu->addSeparator();
	
	mContextMenu->addAction(mEditActions[menu_edit_copy_buffer]);
	mContextMenu->addAction(mEditActions[menu_edit_copy]);
	mContextMenu->addAction(mEditActions[menu_edit_copy_color]);
	mContextMenu->addAction(mEditActions[menu_edit_paste]);
	mContextMenu->addSeparator();
	
	mContextMenu->addAction(mViewActions[menu_view_frameless]);
	mContextMenu->addSeparator();

	mContextMenu->addMenu(mSortMenu);

	QMenu* viewContextMenu = mContextMenu->addMenu(tr("&View"));
	viewContextMenu->addAction(mViewActions[menu_view_fullscreen]);
	viewContextMenu->addAction(mViewActions[menu_view_reset]);
	viewContextMenu->addAction(mViewActions[menu_view_100]);
	viewContextMenu->addAction(mViewActions[menu_view_fit_frame]);

	QMenu* editContextMenu = mContextMenu->addMenu(tr("&Edit"));
	editContextMenu->addAction(mEditActions[menu_edit_rotate_cw]);
	editContextMenu->addAction(mEditActions[menu_edit_rotate_ccw]);
	editContextMenu->addAction(mEditActions[menu_edit_rotate_180]);
	editContextMenu->addSeparator();
	editContextMenu->addAction(mEditActions[menu_edit_transform]);
	editContextMenu->addAction(mEditActions[menu_edit_crop]);
	editContextMenu->addAction(mEditActions[menu_edit_delete]);

	if (mSyncMenu)
		mContextMenu->addMenu(mSyncMenu);

	mContextMenu->addSeparator();
	mContextMenu->addAction(mEditActions[menu_edit_preferences]);

}

void DkNoMacs::createActions() {

	DkViewPort* vp = viewport();

	mFileActions.resize(menu_file_end);
	
	mFileActions[menu_file_open] = new QAction(mFileIcons[icon_file_open], tr("&Open"), this);
	mFileActions[menu_file_open]->setShortcuts(QKeySequence::Open);
	mFileActions[menu_file_open]->setStatusTip(tr("Open an image"));
	connect(mFileActions[menu_file_open], SIGNAL(triggered()), this, SLOT(openFile()));

	mFileActions[menu_file_open_dir] = new QAction(mFileIcons[icon_file_dir], tr("Open &Directory"), this);
	mFileActions[menu_file_open_dir]->setShortcut(QKeySequence(shortcut_open_dir));
	mFileActions[menu_file_open_dir]->setStatusTip(tr("Open a directory and load its first image"));
	connect(mFileActions[menu_file_open_dir], SIGNAL(triggered()), this, SLOT(openDir()));

	mFileActions[menu_file_quick_launch] = new QAction(tr("&Quick Launch"), this);
	mFileActions[menu_edit_rotate_cw]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mFileActions[menu_file_quick_launch]->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
	connect(mFileActions[menu_file_quick_launch], SIGNAL(triggered()), this, SLOT(openQuickLaunch()));


	mFileActions[menu_file_app_manager] = new QAction(tr("&Manage Applications"), this);
	mFileActions[menu_file_app_manager]->setStatusTip(tr("Manage Applications which are Automatically Opened"));
	mFileActions[menu_file_app_manager]->setShortcut(QKeySequence(shortcut_app_manager));
	connect(mFileActions[menu_file_app_manager], SIGNAL(triggered()), this, SLOT(openAppManager()));
		
	mFileActions[menu_file_rename] = new QAction(tr("Re&name"), this);
	mFileActions[menu_file_rename]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mFileActions[menu_file_rename]->setShortcut(QKeySequence(shortcut_rename));
	mFileActions[menu_file_rename]->setStatusTip(tr("Rename an image"));
	connect(mFileActions[menu_file_rename], SIGNAL(triggered()), this, SLOT(renameFile()));

	mFileActions[menu_file_goto] = new QAction(tr("&Go To"), this);
	mFileActions[menu_file_goto]->setShortcut(QKeySequence(shortcut_goto));
	mFileActions[menu_file_goto]->setStatusTip(tr("Go To an image"));
	connect(mFileActions[menu_file_goto], SIGNAL(triggered()), this, SLOT(goTo()));

	mFileActions[menu_file_save] = new QAction(mFileIcons[icon_file_save], tr("&Save"), this);
	mFileActions[menu_file_save]->setShortcuts(QKeySequence::Save);
	mFileActions[menu_file_save]->setStatusTip(tr("Save an image"));
	connect(mFileActions[menu_file_save], SIGNAL(triggered()), this, SLOT(saveFile()));

	mFileActions[menu_file_save_as] = new QAction(tr("&Save As"), this);
	mFileActions[menu_file_save_as]->setShortcut(QKeySequence(shortcut_save_as));
	mFileActions[menu_file_save_as]->setStatusTip(tr("Save an image as"));
	connect(mFileActions[menu_file_save_as], SIGNAL(triggered()), this, SLOT(saveFileAs()));

	mFileActions[menu_file_save_web] = new QAction(tr("&Save for Web"), this);
	mFileActions[menu_file_save_web]->setStatusTip(tr("Save an Image for Web Applications"));
	connect(mFileActions[menu_file_save_web], SIGNAL(triggered()), this, SLOT(saveFileWeb()));

	mFileActions[menu_file_print] = new QAction(mFileIcons[icon_file_print], tr("&Print"), this);
	mFileActions[menu_file_print]->setShortcuts(QKeySequence::Print);
	mFileActions[menu_file_print]->setStatusTip(tr("Print an image"));
	connect(mFileActions[menu_file_print], SIGNAL(triggered()), this, SLOT(printDialog()));

	mFileActions[menu_file_show_recent] = new QAction(tr("&Recent Files and Folders"), this);
	mFileActions[menu_file_show_recent]->setShortcut(QKeySequence(shortcut_recent_files));
	mFileActions[menu_file_show_recent]->setCheckable(true);
	mFileActions[menu_file_show_recent]->setChecked(false);
	mFileActions[menu_file_show_recent]->setStatusTip(tr("Show Recent Files and Folders"));
	connect(mFileActions[menu_file_show_recent], SIGNAL(triggered(bool)), centralWidget(), SLOT(showRecentFiles(bool)));

	mFileActions[menu_file_reload] = new QAction(tr("&Reload File"), this);
	mFileActions[menu_file_reload]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mFileActions[menu_file_reload]->setShortcuts(QKeySequence::Refresh);
	mFileActions[menu_file_reload]->setStatusTip(tr("Reload File"));
	connect(mFileActions[menu_file_reload], SIGNAL(triggered()), vp, SLOT(reloadFile()));

	mFileActions[menu_file_next] = new QAction(mFileIcons[icon_file_next], tr("Ne&xt File"), viewport());
	mFileActions[menu_file_next]->setShortcutContext(Qt::WidgetShortcut);
	mFileActions[menu_file_next]->setShortcut(QKeySequence(shortcut_next_file));
	mFileActions[menu_file_next]->setStatusTip(tr("Load next image"));
	connect(mFileActions[menu_file_next], SIGNAL(triggered()), vp, SLOT(loadNextFileFast()));

	mFileActions[menu_file_prev] = new QAction(mFileIcons[icon_file_prev], tr("Pre&vious File"), this);
	mFileActions[menu_file_prev]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mFileActions[menu_file_prev]->setShortcut(QKeySequence(shortcut_prev_file));
	mFileActions[menu_file_prev]->setStatusTip(tr("Load previous fileInfo"));
	connect(mFileActions[menu_file_prev], SIGNAL(triggered()), vp, SLOT(loadPrevFileFast()));

	mFileActions[menu_file_train_format] = new QAction(tr("Add Image Format"), this);
	mFileActions[menu_file_train_format]->setStatusTip(tr("Add a new image format to nomacs"));
	connect(mFileActions[menu_file_train_format], SIGNAL(triggered()), this, SLOT(trainFormat()));

	mFileActions[menu_file_new_instance] = new QAction(tr("St&art New Instance"), this);
	mFileActions[menu_file_new_instance]->setShortcut(QKeySequence(shortcut_new_instance));
	mFileActions[menu_file_new_instance]->setStatusTip(tr("Open fileInfo in new instance"));
	connect(mFileActions[menu_file_new_instance], SIGNAL(triggered()), this, SLOT(newInstance()));

	mFileActions[menu_file_private_instance] = new QAction(tr("St&art Private Instance"), this);
	mFileActions[menu_file_private_instance]->setShortcut(QKeySequence(shortcut_private_instance));
	mFileActions[menu_file_private_instance]->setStatusTip(tr("Open private instance"));
	connect(mFileActions[menu_file_private_instance], SIGNAL(triggered()), this, SLOT(newInstance()));

	mFileActions[menu_file_find] = new QAction(tr("&Find && Filter"), this);
	mFileActions[menu_file_find]->setShortcut(QKeySequence::Find);
	mFileActions[menu_file_find]->setStatusTip(tr("Find an image"));
	connect(mFileActions[menu_file_find], SIGNAL(triggered()), this, SLOT(find()));

	mFileActions[menu_file_recursive] = new QAction(tr("Scan Folder Re&cursive"), this);
	mFileActions[menu_file_recursive]->setStatusTip(tr("Step through Folder and Sub Folders"));
	mFileActions[menu_file_recursive]->setCheckable(true);
	mFileActions[menu_file_recursive]->setChecked(DkSettings::global.scanSubFolders);
	connect(mFileActions[menu_file_recursive], SIGNAL(triggered(bool)), this, SLOT(setRecursiveScan(bool)));

	//fileActions[menu_file_share_fb] = new QAction(tr("Share on &Facebook"), this);
	////fileActions[menu_file_share_fb]->setShortcuts(QKeySequence::Close);
	//fileActions[menu_file_share_fb]->setStatusTip(tr("Shares the image on facebook"));
	//connect(fileActions[menu_file_share_fb], SIGNAL(triggered()), this, SLOT(shareFacebook()));

	mFileActions[menu_file_exit] = new QAction(tr("&Exit"), this);
	//fileActions[menu_file_exit]->setShortcuts(QKeySequence::Close);
	mFileActions[menu_file_exit]->setStatusTip(tr("Exit"));
	connect(mFileActions[menu_file_exit], SIGNAL(triggered()), this, SLOT(close()));

	mSortActions.resize(menu_sort_end);

	mSortActions[menu_sort_filename] = new QAction(tr("by &Filename"), this);
	mSortActions[menu_sort_filename]->setObjectName("menu_sort_filename");
	mSortActions[menu_sort_filename]->setStatusTip(tr("Sort by Filename"));
	mSortActions[menu_sort_filename]->setCheckable(true);
	mSortActions[menu_sort_filename]->setChecked(DkSettings::global.sortMode == DkSettings::sort_filename);
	connect(mSortActions[menu_sort_filename], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mSortActions[menu_sort_date_created] = new QAction(tr("by Date &Created"), this);
	mSortActions[menu_sort_date_created]->setObjectName("menu_sort_date_created");
	mSortActions[menu_sort_date_created]->setStatusTip(tr("Sort by Date Created"));
	mSortActions[menu_sort_date_created]->setCheckable(true);
	mSortActions[menu_sort_date_created]->setChecked(DkSettings::global.sortMode == DkSettings::sort_date_created);
	connect(mSortActions[menu_sort_date_created], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mSortActions[menu_sort_date_modified] = new QAction(tr("by Date Modified"), this);
	mSortActions[menu_sort_date_modified]->setObjectName("menu_sort_date_modified");
	mSortActions[menu_sort_date_modified]->setStatusTip(tr("Sort by Date Last Modified"));
	mSortActions[menu_sort_date_modified]->setCheckable(true);
	mSortActions[menu_sort_date_modified]->setChecked(DkSettings::global.sortMode == DkSettings::sort_date_modified);
	connect(mSortActions[menu_sort_date_modified], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mSortActions[menu_sort_random] = new QAction(tr("Random"), this);
	mSortActions[menu_sort_random]->setObjectName("menu_sort_random");
	mSortActions[menu_sort_random]->setStatusTip(tr("Sort in Random Order"));
	mSortActions[menu_sort_random]->setCheckable(true);
	mSortActions[menu_sort_random]->setChecked(DkSettings::global.sortMode == DkSettings::sort_random);
	connect(mSortActions[menu_sort_random], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mSortActions[menu_sort_ascending] = new QAction(tr("&Ascending"), this);
	mSortActions[menu_sort_ascending]->setObjectName("menu_sort_ascending");
	mSortActions[menu_sort_ascending]->setStatusTip(tr("Sort in Ascending Order"));
	mSortActions[menu_sort_ascending]->setCheckable(true);
	mSortActions[menu_sort_ascending]->setChecked(DkSettings::global.sortDir == Qt::AscendingOrder);
	connect(mSortActions[menu_sort_ascending], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mSortActions[menu_sort_descending] = new QAction(tr("&Descending"), this);
	mSortActions[menu_sort_descending]->setObjectName("menu_sort_descending");
	mSortActions[menu_sort_descending]->setStatusTip(tr("Sort in Descending Order"));
	mSortActions[menu_sort_descending]->setCheckable(true);
	mSortActions[menu_sort_descending]->setChecked(DkSettings::global.sortDir == Qt::DescendingOrder);
	connect(mSortActions[menu_sort_descending], SIGNAL(triggered(bool)), this, SLOT(changeSorting(bool)));

	mEditActions.resize(menu_edit_end);

	mEditActions[menu_edit_rotate_cw] = new QAction(mEditIcons[icon_edit_rotate_cw], tr("9&0%1 Clockwise").arg(dk_degree_str), this);
	mEditActions[menu_edit_rotate_cw]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_rotate_cw]->setShortcut(QKeySequence(shortcut_rotate_cw));
	mEditActions[menu_edit_rotate_cw]->setStatusTip(tr("rotate the image 90%1 clockwise").arg(dk_degree_str));
	connect(mEditActions[menu_edit_rotate_cw], SIGNAL(triggered()), vp, SLOT(rotateCW()));

	mEditActions[menu_edit_rotate_ccw] = new QAction(mEditIcons[icon_edit_rotate_ccw], tr("&90%1 Counter Clockwise").arg(dk_degree_str), this);
	mEditActions[menu_edit_rotate_ccw]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_rotate_ccw]->setShortcut(QKeySequence(shortcut_rotate_ccw));
	mEditActions[menu_edit_rotate_ccw]->setStatusTip(tr("rotate the image 90%1 counter clockwise").arg(dk_degree_str));
	connect(mEditActions[menu_edit_rotate_ccw], SIGNAL(triggered()), vp, SLOT(rotateCCW()));

	mEditActions[menu_edit_rotate_180] = new QAction(tr("1&80%1").arg(dk_degree_str), this);
	mEditActions[menu_edit_rotate_180]->setStatusTip(tr("rotate the image by 180%1").arg(dk_degree_str));
	connect(mEditActions[menu_edit_rotate_180], SIGNAL(triggered()), vp, SLOT(rotate180()));

	mEditActions[menu_edit_copy] = new QAction(mEditIcons[icon_edit_copy], tr("&Copy"), this);
	mEditActions[menu_edit_copy]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_copy]->setShortcut(QKeySequence::Copy);
	mEditActions[menu_edit_copy]->setStatusTip(tr("copy image"));
	connect(mEditActions[menu_edit_copy], SIGNAL(triggered()), vp, SLOT(copyImage()));

	mEditActions[menu_edit_copy_buffer] = new QAction(tr("Copy &Buffer"), this);
	mEditActions[menu_edit_copy_buffer]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_copy_buffer]->setShortcut(shortcut_copy_buffer);
	mEditActions[menu_edit_copy_buffer]->setStatusTip(tr("copy image"));
	connect(mEditActions[menu_edit_copy_buffer], SIGNAL(triggered()), vp, SLOT(copyImageBuffer()));

	mEditActions[menu_edit_copy_color] = new QAction(tr("Copy Co&lor"), this);
	mEditActions[menu_edit_copy_color]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_copy_color]->setShortcut(shortcut_copy_color);
	mEditActions[menu_edit_copy_color]->setStatusTip(tr("copy pixel color value as HEX"));
	connect(mEditActions[menu_edit_copy_color], SIGNAL(triggered()), vp, SLOT(copyPixelColorValue()));

	QList<QKeySequence> pastScs;
	pastScs.append(QKeySequence::Paste);
	pastScs.append(shortcut_paste);
	mEditActions[menu_edit_paste] = new QAction(mEditIcons[icon_edit_paste], tr("&Paste"), this);
	mEditActions[menu_edit_paste]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_paste]->setShortcuts(pastScs);
	mEditActions[menu_edit_paste]->setStatusTip(tr("paste image"));
	connect(mEditActions[menu_edit_paste], SIGNAL(triggered()), centralWidget(), SLOT(pasteImage()));

	mEditActions[menu_edit_transform] = new QAction(mEditIcons[icon_edit_resize], tr("R&esize Image"), this);
	mEditActions[menu_edit_transform]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_transform]->setShortcut(shortcut_transform);
	mEditActions[menu_edit_transform]->setStatusTip(tr("resize the current image"));
	connect(mEditActions[menu_edit_transform], SIGNAL(triggered()), this, SLOT(resizeImage()));

	mEditActions[menu_edit_crop] = new QAction(mEditIcons[icon_edit_crop], tr("Cr&op Image"), this);
	mEditActions[menu_edit_crop]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_crop]->setShortcut(shortcut_crop);
	mEditActions[menu_edit_crop]->setStatusTip(tr("cut the current image"));
	mEditActions[menu_edit_crop]->setCheckable(true);
	mEditActions[menu_edit_crop]->setChecked(false);
	connect(mEditActions[menu_edit_crop], SIGNAL(triggered(bool)), vp->getController(), SLOT(showCrop(bool)));

	mEditActions[menu_edit_flip_h] = new QAction(tr("Flip &Horizontal"), this);
	//editActions[menu_edit_flip_h]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	//editActions[menu_edit_flip_h]->setShortcut();
	mEditActions[menu_edit_flip_h]->setStatusTip(tr("Flip Image Horizontally"));
	connect(mEditActions[menu_edit_flip_h], SIGNAL(triggered()), this, SLOT(flipImageHorizontal()));

	mEditActions[menu_edit_flip_v] = new QAction(tr("Flip &Vertical"), this);
	//editActions[menu_edit_flip_v]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	//editActions[menu_edit_flip_v]->setShortcut();
	mEditActions[menu_edit_flip_v]->setStatusTip(tr("Flip Image Vertically"));
	connect(mEditActions[menu_edit_flip_v], SIGNAL(triggered()), this, SLOT(flipImageVertical()));

	mEditActions[menu_edit_norm] = new QAction(tr("Nor&malize Image"), this);
	mEditActions[menu_edit_norm]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_norm]->setShortcut(shortcut_norm_image);
	mEditActions[menu_edit_norm]->setStatusTip(tr("Normalize the Image"));
	connect(mEditActions[menu_edit_norm], SIGNAL(triggered()), this, SLOT(normalizeImage()));

	mEditActions[menu_edit_auto_adjust] = new QAction(tr("&Auto Adjust"), this);
	mEditActions[menu_edit_auto_adjust]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_auto_adjust]->setShortcut(shortcut_auto_adjust);
	mEditActions[menu_edit_auto_adjust]->setStatusTip(tr("Auto Adjust Image Contrast and Color Balance"));
	connect(mEditActions[menu_edit_auto_adjust], SIGNAL(triggered()), this, SLOT(autoAdjustImage()));

	mEditActions[menu_edit_invert] = new QAction(tr("&Invert Image"), this);
//	editActions[menu_edit_invert]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
//	editActions[menu_edit_invert]->setShortcut();
	mEditActions[menu_edit_invert]->setStatusTip(tr("Invert the Image"));		    
	connect(mEditActions[menu_edit_invert], SIGNAL(triggered()), this, SLOT(invertImage()));

	mEditActions[menu_edit_gray_convert] = new QAction(tr("&Convert to Grayscale"), this);	
	mEditActions[menu_edit_gray_convert]->setStatusTip(tr("Convert to Grayscale"));		   
	connect(mEditActions[menu_edit_gray_convert], SIGNAL(triggered()), this, SLOT(convert2gray()));

	mEditActions[menu_edit_unsharp] = new QAction(tr("&Unsharp Mask"), this);
	mEditActions[menu_edit_unsharp]->setStatusTip(tr("Stretches the Local Contrast of an Image"));
	connect(mEditActions[menu_edit_unsharp], SIGNAL(triggered()), this, SLOT(unsharpMask()));

	mEditActions[menu_edit_tiny_planet] = new QAction(tr("&Tiny Planet"), this);
	mEditActions[menu_edit_tiny_planet]->setStatusTip(tr("Computes a tiny planet image"));
	connect(mEditActions[menu_edit_tiny_planet], SIGNAL(triggered()), this, SLOT(tinyPlanet()));

	mEditActions[menu_edit_delete] = new QAction(mEditIcons[icon_edit_delete], tr("&Delete"), this);
	mEditActions[menu_edit_delete]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mEditActions[menu_edit_delete]->setShortcut(QKeySequence::Delete);
	mEditActions[menu_edit_delete]->setStatusTip(tr("delete current fileInfo"));
	connect(mEditActions[menu_edit_delete], SIGNAL(triggered()), this, SLOT(deleteFile()));

	mEditActions[menu_edit_wallpaper] = new QAction(tr("&Wallpaper"), this);
	mEditActions[menu_edit_wallpaper]->setStatusTip(tr("set the current image as wallpaper"));
	connect(mEditActions[menu_edit_wallpaper], SIGNAL(triggered()), this, SLOT(setWallpaper()));

	mEditActions[menu_edit_shortcuts] = new QAction(tr("&Keyboard Shortcuts"), this);
	mEditActions[menu_edit_shortcuts]->setShortcut(QKeySequence(shortcut_shortcuts));
	mEditActions[menu_edit_shortcuts]->setStatusTip(tr("lets you customize your keyboard shortcuts"));
	connect(mEditActions[menu_edit_shortcuts], SIGNAL(triggered()), this, SLOT(openKeyboardShortcuts()));

	mEditActions[menu_edit_preferences] = new QAction(tr("&Settings"), this);
	mEditActions[menu_edit_preferences]->setShortcut(QKeySequence(shortcut_settings));
	mEditActions[menu_edit_preferences]->setStatusTip(tr("settings"));
	connect(mEditActions[menu_edit_preferences], SIGNAL(triggered()), this, SLOT(openSettings()));

	// view menu
	mPanelActions.resize(menu_panel_end);
	mPanelActions[menu_panel_menu] = new QAction(tr("&Menu"), this);
	mPanelActions[menu_panel_menu]->setStatusTip(tr("Hides the Menu and Shows it Again on ALT"));
	mPanelActions[menu_panel_menu]->setCheckable(true);
	connect(mPanelActions[menu_panel_menu], SIGNAL(toggled(bool)), this, SLOT(showMenuBar(bool)));

	mPanelActions[menu_panel_toolbar] = new QAction(tr("Tool&bar"), this);
	mPanelActions[menu_panel_toolbar]->setShortcut(QKeySequence(shortcut_show_toolbar));
	mPanelActions[menu_panel_toolbar]->setStatusTip(tr("Show Toolbar"));
	mPanelActions[menu_panel_toolbar]->setCheckable(true);
	connect(mPanelActions[menu_panel_toolbar], SIGNAL(toggled(bool)), this, SLOT(showToolbar(bool)));

	mPanelActions[menu_panel_statusbar] = new QAction(tr("&Statusbar"), this);
	mPanelActions[menu_panel_statusbar]->setShortcut(QKeySequence(shortcut_show_statusbar));
	mPanelActions[menu_panel_statusbar]->setStatusTip(tr("Show Statusbar"));
	mPanelActions[menu_panel_statusbar]->setCheckable(true);
	connect(mPanelActions[menu_panel_statusbar], SIGNAL(toggled(bool)), this, SLOT(showStatusBar(bool)));

	// Added by fabian - for transferfunction:
	mPanelActions[menu_panel_transfertoolbar] = new QAction(tr("&Pseudocolor Function"), this);
	mPanelActions[menu_panel_transfertoolbar]->setShortcut(QKeySequence(shortcut_show_transfer));
	mPanelActions[menu_panel_transfertoolbar]->setStatusTip(tr("Show Pseudocolor Function"));
	mPanelActions[menu_panel_transfertoolbar]->setCheckable(true);
	mPanelActions[menu_panel_transfertoolbar]->setChecked(false);
	connect(mPanelActions[menu_panel_transfertoolbar], SIGNAL(toggled(bool)), this, SLOT(setContrast(bool)));

	mPanelActions[menu_panel_overview] = new QAction(tr("O&verview"), this);
	mPanelActions[menu_panel_overview]->setShortcut(QKeySequence(shortcut_show_overview));
	mPanelActions[menu_panel_overview]->setStatusTip(tr("Shows the Zoom Overview"));
	mPanelActions[menu_panel_overview]->setCheckable(true);
	mPanelActions[menu_panel_overview]->setChecked(DkSettings::app.showOverview.testBit(DkSettings::app.currentAppMode));
	connect(mPanelActions[menu_panel_overview], SIGNAL(toggled(bool)), vp->getController(), SLOT(showOverview(bool)));

	mPanelActions[menu_panel_player] = new QAction(tr("Pla&yer"), this);
	mPanelActions[menu_panel_player]->setShortcut(QKeySequence(shortcut_show_player));
	mPanelActions[menu_panel_player]->setStatusTip(tr("Shows the Slide Show Player"));
	mPanelActions[menu_panel_player]->setCheckable(true);
	connect(mPanelActions[menu_panel_player], SIGNAL(toggled(bool)), vp->getController(), SLOT(showPlayer(bool)));

	mPanelActions[menu_panel_explorer] = new QAction(tr("File &Explorer"), this);
	mPanelActions[menu_panel_explorer]->setShortcut(QKeySequence(shortcut_show_explorer));
	mPanelActions[menu_panel_explorer]->setStatusTip(tr("Show File Explorer"));
	mPanelActions[menu_panel_explorer]->setCheckable(true);
	connect(mPanelActions[menu_panel_explorer], SIGNAL(toggled(bool)), this, SLOT(showExplorer(bool)));

	mPanelActions[menu_panel_metadata_dock] = new QAction(tr("Metadata &Info"), this);
	mPanelActions[menu_panel_metadata_dock]->setShortcut(QKeySequence(shortcut_show_metadata_dock));
	mPanelActions[menu_panel_metadata_dock]->setStatusTip(tr("Show Metadata Info"));
	mPanelActions[menu_panel_metadata_dock]->setCheckable(true);
	connect(mPanelActions[menu_panel_metadata_dock], SIGNAL(toggled(bool)), this, SLOT(showMetaDataDock(bool)));

	mPanelActions[menu_panel_preview] = new QAction(tr("&Thumbnails"), this);
	mPanelActions[menu_panel_preview]->setShortcut(QKeySequence(shortcut_open_preview));
	mPanelActions[menu_panel_preview]->setStatusTip(tr("Show Thumbnails"));
	mPanelActions[menu_panel_preview]->setCheckable(true);
	connect(mPanelActions[menu_panel_preview], SIGNAL(toggled(bool)), vp->getController(), SLOT(showPreview(bool)));
	connect(mPanelActions[menu_panel_preview], SIGNAL(toggled(bool)), this, SLOT(showThumbsDock(bool)));

	mPanelActions[menu_panel_thumbview] = new QAction(tr("&Thumbnail Preview"), this);
	mPanelActions[menu_panel_thumbview]->setShortcut(QKeySequence(shortcut_open_thumbview));
	mPanelActions[menu_panel_thumbview]->setStatusTip(tr("Show Thumbnails Preview"));
	mPanelActions[menu_panel_thumbview]->setCheckable(true);
	connect(mPanelActions[menu_panel_thumbview], SIGNAL(toggled(bool)), getTabWidget(), SLOT(showThumbView(bool)));

	mPanelActions[menu_panel_scroller] = new QAction(tr("&Folder Scrollbar"), this);
	mPanelActions[menu_panel_scroller]->setShortcut(QKeySequence(shortcut_show_scroller));
	mPanelActions[menu_panel_scroller]->setStatusTip(tr("Show Folder Scrollbar"));
	mPanelActions[menu_panel_scroller]->setCheckable(true);
	connect(mPanelActions[menu_panel_scroller], SIGNAL(toggled(bool)), vp->getController(), SLOT(showScroller(bool)));

	mPanelActions[menu_panel_exif] = new QAction(tr("&Metadata"), this);
	mPanelActions[menu_panel_exif]->setShortcut(QKeySequence(shortcut_show_exif));
	mPanelActions[menu_panel_exif]->setStatusTip(tr("Shows the Metadata Panel"));
	mPanelActions[menu_panel_exif]->setCheckable(true);
	connect(mPanelActions[menu_panel_exif], SIGNAL(toggled(bool)), vp->getController(), SLOT(showMetaData(bool)));

	mPanelActions[menu_panel_info] = new QAction(tr("File &Info"), this);
	mPanelActions[menu_panel_info]->setShortcut(QKeySequence(shortcut_show_info));
	mPanelActions[menu_panel_info]->setStatusTip(tr("Shows the Info Panel"));
	mPanelActions[menu_panel_info]->setCheckable(true);
	connect(mPanelActions[menu_panel_info], SIGNAL(toggled(bool)), vp->getController(), SLOT(showFileInfo(bool)));

	mPanelActions[menu_panel_histogram] = new QAction(tr("&Histogram"), this);
	mPanelActions[menu_panel_histogram]->setShortcut(QKeySequence(shortcut_show_histogram));
	mPanelActions[menu_panel_histogram]->setStatusTip(tr("Shows the Histogram Panel"));
	mPanelActions[menu_panel_histogram]->setCheckable(true);
	connect(mPanelActions[menu_panel_histogram], SIGNAL(toggled(bool)), vp->getController(), SLOT(showHistogram(bool)));

	mPanelActions[menu_panel_comment] = new QAction(tr("Image &Notes"), this);
	mPanelActions[menu_panel_comment]->setShortcut(QKeySequence(shortcut_show_comment));
	mPanelActions[menu_panel_comment]->setStatusTip(tr("Shows Image Notes"));
	mPanelActions[menu_panel_comment]->setCheckable(true);
	connect(mPanelActions[menu_panel_comment], SIGNAL(toggled(bool)), vp->getController(), SLOT(showCommentWidget(bool)));

	mViewActions.resize(menu_view_end);
	mViewActions[menu_view_fit_frame] = new QAction(tr("&Fit Window"), this);
	mViewActions[menu_view_fit_frame]->setShortcut(QKeySequence(shortcut_fit_frame));
	mViewActions[menu_view_fit_frame]->setStatusTip(tr("Fit window to the image"));
	connect(mViewActions[menu_view_fit_frame], SIGNAL(triggered()), this, SLOT(fitFrame()));

	QList<QKeySequence> scs;
	scs.append(shortcut_full_screen_ff);
	scs.append(shortcut_full_screen_ad);
	mViewActions[menu_view_fullscreen] = new QAction(mViewIcons[icon_view_fullscreen], tr("Fu&ll Screen"), this);
	mViewActions[menu_view_fullscreen]->setShortcuts(scs);
	mViewActions[menu_view_fullscreen]->setStatusTip(tr("Full Screen"));
	connect(mViewActions[menu_view_fullscreen], SIGNAL(triggered()), this, SLOT(toggleFullScreen()));

	mViewActions[menu_view_reset] = new QAction(mViewIcons[icon_view_reset], tr("&Zoom to Fit"), this);
	mViewActions[menu_view_reset]->setShortcut(QKeySequence(shortcut_reset_view));
	mViewActions[menu_view_reset]->setStatusTip(tr("Shows the initial view (no zooming)"));
	connect(mViewActions[menu_view_reset], SIGNAL(triggered()), vp, SLOT(zoomToFit()));

	mViewActions[menu_view_100] = new QAction(mViewIcons[icon_view_100], tr("Show &100%"), this);
	mViewActions[menu_view_100]->setShortcut(QKeySequence(shortcut_zoom_full));
	mViewActions[menu_view_100]->setStatusTip(tr("Shows the image at 100%"));
	connect(mViewActions[menu_view_100], SIGNAL(triggered()), vp, SLOT(fullView()));

	mViewActions[menu_view_zoom_in] = new QAction(tr("Zoom &In"), this);
	mViewActions[menu_view_zoom_in]->setShortcut(QKeySequence::ZoomIn);
	mViewActions[menu_view_zoom_in]->setStatusTip(tr("zoom in"));
	connect(mViewActions[menu_view_zoom_in], SIGNAL(triggered()), vp, SLOT(zoomIn()));

	mViewActions[menu_view_zoom_out] = new QAction(tr("&Zoom Out"), this);
	mViewActions[menu_view_zoom_out]->setShortcut(QKeySequence::ZoomOut);
	mViewActions[menu_view_zoom_out]->setStatusTip(tr("zoom out"));
	connect(mViewActions[menu_view_zoom_out], SIGNAL(triggered()), vp, SLOT(zoomOut()));

	mViewActions[menu_view_anti_aliasing] = new QAction(tr("&Anti Aliasing"), this);
	mViewActions[menu_view_anti_aliasing]->setShortcut(QKeySequence(shortcut_anti_aliasing));
	mViewActions[menu_view_anti_aliasing]->setStatusTip(tr("if checked images are smoother"));
	mViewActions[menu_view_anti_aliasing]->setCheckable(true);
	mViewActions[menu_view_anti_aliasing]->setChecked(DkSettings::display.antiAliasing);
	connect(mViewActions[menu_view_anti_aliasing], SIGNAL(toggled(bool)), vp->getImageStorage(), SLOT(antiAliasingChanged(bool)));

	mViewActions[menu_view_tp_pattern] = new QAction(tr("&Transparency Pattern"), this);
	mViewActions[menu_view_tp_pattern]->setShortcut(QKeySequence(shortcut_tp_pattern));
	mViewActions[menu_view_tp_pattern]->setStatusTip(tr("if checked, a pattern will be displayed for transparent objects"));
	mViewActions[menu_view_tp_pattern]->setCheckable(true);
	mViewActions[menu_view_tp_pattern]->setChecked(DkSettings::display.tpPattern);
	connect(mViewActions[menu_view_tp_pattern], SIGNAL(toggled(bool)), vp, SLOT(togglePattern(bool)));

	mViewActions[menu_view_frameless] = new QAction(tr("&Frameless"), this);
	mViewActions[menu_view_frameless]->setShortcut(QKeySequence(shortcut_frameless));
	mViewActions[menu_view_frameless]->setStatusTip(tr("shows a frameless window"));
	mViewActions[menu_view_frameless]->setCheckable(true);
	mViewActions[menu_view_frameless]->setChecked(false);
	connect(mViewActions[menu_view_frameless], SIGNAL(toggled(bool)), this, SLOT(setFrameless(bool)));

	mViewActions[menu_view_new_tab] = new QAction(tr("New &Tab"), this);
	mViewActions[menu_view_new_tab]->setShortcut(QKeySequence(shortcut_new_tab));
	mViewActions[menu_view_new_tab]->setStatusTip(tr("Open a new tab"));
	connect(mViewActions[menu_view_new_tab], SIGNAL(triggered()), centralWidget(), SLOT(addTab()));

	mViewActions[menu_view_close_tab] = new QAction(tr("&Close Tab"), this);
	mViewActions[menu_view_close_tab]->setShortcut(QKeySequence(shortcut_close_tab));
	mViewActions[menu_view_close_tab]->setStatusTip(tr("Close current tab"));
	connect(mViewActions[menu_view_close_tab], SIGNAL(triggered()), centralWidget(), SLOT(removeTab()));

	mViewActions[menu_view_previous_tab] = new QAction(tr("&Previous Tab"), this);
	mViewActions[menu_view_previous_tab]->setShortcut(QKeySequence(shortcut_previous_tab));
	mViewActions[menu_view_previous_tab]->setStatusTip(tr("Switch to previous tab"));
	connect(mViewActions[menu_view_previous_tab], SIGNAL(triggered()), centralWidget(), SLOT(previousTab()));

	mViewActions[menu_view_next_tab] = new QAction(tr("&Next Tab"), this);
	mViewActions[menu_view_next_tab]->setShortcut(QKeySequence(shortcut_next_tab));
	mViewActions[menu_view_next_tab]->setStatusTip(tr("Switch to next tab"));
	connect(mViewActions[menu_view_next_tab], SIGNAL(triggered()), centralWidget(), SLOT(nextTab()));

	mViewActions[menu_view_opacity_change] = new QAction(tr("&Change Opacity"), this);
	mViewActions[menu_view_opacity_change]->setShortcut(QKeySequence(shortcut_opacity_change));
	mViewActions[menu_view_opacity_change]->setStatusTip(tr("change the window opacity"));
	connect(mViewActions[menu_view_opacity_change], SIGNAL(triggered()), this, SLOT(showOpacityDialog()));

	mViewActions[menu_view_opacity_up] = new QAction(tr("Opacity &Up"), this);
	mViewActions[menu_view_opacity_up]->setShortcut(QKeySequence(shortcut_opacity_up));
	mViewActions[menu_view_opacity_up]->setStatusTip(tr("changes the window opacity"));
	connect(mViewActions[menu_view_opacity_up], SIGNAL(triggered()), this, SLOT(opacityUp()));

	mViewActions[menu_view_opacity_down] = new QAction(tr("Opacity &Down"), this);
	mViewActions[menu_view_opacity_down]->setShortcut(QKeySequence(shortcut_opacity_down));
	mViewActions[menu_view_opacity_down]->setStatusTip(tr("changes the window opacity"));
	connect(mViewActions[menu_view_opacity_down], SIGNAL(triggered()), this, SLOT(opacityDown()));

	mViewActions[menu_view_opacity_an] = new QAction(tr("To&ggle Opacity"), this);
	mViewActions[menu_view_opacity_an]->setShortcut(QKeySequence(shortcut_an_opacity));
	mViewActions[menu_view_opacity_an]->setStatusTip(tr("toggle the window opacity"));
	connect(mViewActions[menu_view_opacity_an], SIGNAL(triggered()), this, SLOT(animateChangeOpacity()));

	mViewActions[menu_view_lock_window] = new QAction(tr("Lock &Window"), this);
	mViewActions[menu_view_lock_window]->setShortcut(QKeySequence(shortcut_lock_window));
	mViewActions[menu_view_lock_window]->setStatusTip(tr("lock the window"));
	mViewActions[menu_view_lock_window]->setCheckable(true);
	mViewActions[menu_view_lock_window]->setChecked(false);
	connect(mViewActions[menu_view_lock_window], SIGNAL(triggered(bool)), this, SLOT(lockWindow(bool)));

	mViewActions[menu_view_movie_pause] = new QAction(mViewIcons[icon_view_movie_play], tr("&Pause Movie"), this);
	mViewActions[menu_view_movie_pause]->setStatusTip(tr("pause the current movie"));
	mViewActions[menu_view_movie_pause]->setCheckable(true);
	mViewActions[menu_view_movie_pause]->setChecked(false);
	connect(mViewActions[menu_view_movie_pause], SIGNAL(triggered(bool)), vp, SLOT(pauseMovie(bool)));

	mViewActions[menu_view_movie_prev] = new QAction(mViewIcons[icon_view_movie_prev], tr("P&revious Frame"), this);
	mViewActions[menu_view_movie_prev]->setStatusTip(tr("show previous frame"));
	connect(mViewActions[menu_view_movie_prev], SIGNAL(triggered()), vp, SLOT(previousMovieFrame()));

	mViewActions[menu_view_movie_next] = new QAction(mViewIcons[icon_view_movie_next], tr("&Next Frame"), this);
	mViewActions[menu_view_movie_next]->setStatusTip(tr("show next frame"));
	connect(mViewActions[menu_view_movie_next], SIGNAL(triggered()), vp, SLOT(nextMovieFrame()));

	mViewActions[menu_view_gps_map] = new QAction(mViewIcons[icon_view_gps], tr("Show G&PS Coordinates"), this);
	mViewActions[menu_view_gps_map]->setStatusTip(tr("shows the GPS coordinates"));
	mViewActions[menu_view_gps_map]->setEnabled(false);
	connect(mViewActions[menu_view_gps_map], SIGNAL(triggered()), this, SLOT(showGpsCoordinates()));
	
	// batch actions
	mToolsActions.resize(menu_tools_end);

	mToolsActions[menu_tools_thumbs] = new QAction(tr("Compute &Thumbnails"), this);
	mToolsActions[menu_tools_thumbs]->setStatusTip(tr("compute all thumbnails of the current folder"));
	mToolsActions[menu_tools_thumbs]->setEnabled(false);
	connect(mToolsActions[menu_tools_thumbs], SIGNAL(triggered()), this, SLOT(computeThumbsBatch()));

	mToolsActions[menu_tools_filter] = new QAction(mFileIcons[icon_file_filter], tr("&Filter"), this);
	mToolsActions[menu_tools_filter]->setStatusTip(tr("Find an image"));
	mToolsActions[menu_tools_filter]->setCheckable(true);
	mToolsActions[menu_tools_filter]->setChecked(false);
	connect(mToolsActions[menu_tools_filter], SIGNAL(triggered(bool)), this, SLOT(find(bool)));

	mToolsActions[menu_tools_manipulation] = new QAction(mToolsIcons[icon_tools_manipulation], tr("Image &Manipulation"), this);
	mToolsActions[menu_tools_manipulation]->setShortcut(shortcut_manipulation);
	mToolsActions[menu_tools_manipulation]->setStatusTip(tr("modify the current image"));
	connect(mToolsActions[menu_tools_manipulation], SIGNAL(triggered()), this, SLOT(openImgManipulationDialog()));

	mToolsActions[menu_tools_export_tiff] = new QAction(tr("Export Multipage &TIFF"), this);
	mToolsActions[menu_tools_export_tiff]->setStatusTip(tr("Export TIFF pages to multiple tiff files"));
	connect(mToolsActions[menu_tools_export_tiff], SIGNAL(triggered()), this, SLOT(exportTiff()));

	mToolsActions[menu_tools_extract_archive] = new QAction(tr("Extract From Archive"), this);
	mToolsActions[menu_tools_extract_archive]->setStatusTip(tr("Extract images from an archive (%1)").arg(DkSettings::app.containerRawFilters));		
	mToolsActions[menu_tools_extract_archive]->setShortcut(QKeySequence(shortcut_extract));
	connect(mToolsActions[menu_tools_extract_archive], SIGNAL(triggered()), this, SLOT(extractImagesFromArchive()));

	mToolsActions[menu_tools_mosaic] = new QAction(tr("&Mosaic Image"), this);
	mToolsActions[menu_tools_mosaic]->setStatusTip(tr("Create a Mosaic Image"));
	connect(mToolsActions[menu_tools_mosaic], SIGNAL(triggered()), this, SLOT(computeMosaic()));

	mToolsActions[menu_tools_batch] = new QAction(tr("Batch Processing"), this);
	mToolsActions[menu_tools_batch]->setStatusTip(tr("Apply actions to multiple images"));
	connect(mToolsActions[menu_tools_batch], SIGNAL(triggered()), getTabWidget(), SLOT(startBatchProcessing()));
	
	// plugins menu
	mPluginsActions.resize(menu_plugins_end);
	mPluginsActions[menu_plugin_manager] = new QAction(tr("&Plugin Manager"), this);
	mPluginsActions[menu_plugin_manager]->setStatusTip(tr("manage installed plugins and download new ones"));
	connect(mPluginsActions[menu_plugin_manager], SIGNAL(triggered()), this, SLOT(openPluginManager()));
	
	// help menu
	mHelpActions.resize(menu_help_end);
	mHelpActions[menu_help_about] = new QAction(tr("&About Nomacs"), this);
	mHelpActions[menu_help_about]->setShortcut(QKeySequence(shortcut_show_help));
	mHelpActions[menu_help_about]->setStatusTip(tr("about"));
	connect(mHelpActions[menu_help_about], SIGNAL(triggered()), this, SLOT(aboutDialog()));

	mHelpActions[menu_help_documentation] = new QAction(tr("&Documentation"), this);
	mHelpActions[menu_help_documentation]->setStatusTip(tr("Online Documentation"));
	connect(mHelpActions[menu_help_documentation], SIGNAL(triggered()), this, SLOT(openDocumentation()));

	mHelpActions[menu_help_bug] = new QAction(tr("&Report a Bug"), this);
	mHelpActions[menu_help_bug]->setStatusTip(tr("Report a Bug"));
	connect(mHelpActions[menu_help_bug], SIGNAL(triggered()), this, SLOT(bugReport()));

	mHelpActions[menu_help_feature] = new QAction(tr("&Feature Request"), this);
	mHelpActions[menu_help_feature]->setStatusTip(tr("Feature Request"));
	connect(mHelpActions[menu_help_feature], SIGNAL(triggered()), this, SLOT(featureRequest()));

	mHelpActions[menu_help_update] = new QAction(tr("&Check for Updates"), this);
	mHelpActions[menu_help_update]->setStatusTip(tr("check for updates"));
	connect(mHelpActions[menu_help_update], SIGNAL(triggered()), this, SLOT(checkForUpdate()));

	mHelpActions[menu_help_update_translation] = new QAction(tr("&Update Translation"), this);
	mHelpActions[menu_help_update_translation]->setStatusTip(tr("Checks for a new version of the translations of the current language"));
	connect(mHelpActions[menu_help_update_translation], SIGNAL(triggered()), this, SLOT(updateTranslations()));

	assignCustomShortcuts(mFileActions);
	assignCustomShortcuts(mSortActions);
	assignCustomShortcuts(mEditActions);
	assignCustomShortcuts(mViewActions);
	assignCustomShortcuts(mPanelActions);
	assignCustomShortcuts(mToolsActions);
	assignCustomShortcuts(mHelpActions);
	assignCustomPluginShortcuts();

	// add sort actions to the thumbscene
	getTabWidget()->getThumbScrollWidget()->addContextMenuActions(mSortActions, tr("&Sort"));
}

void DkNoMacs::assignCustomShortcuts(QVector<QAction*> actions) {

	QSettings& settings = Settings::instance().getSettings();
	settings.beginGroup("CustomShortcuts");

	for (int idx = 0; idx < actions.size(); idx++) {
		QString val = settings.value(actions[idx]->text(), "no-shortcut").toString();

		if (val != "no-shortcut")
			actions[idx]->setShortcut(val);

		// assign widget shortcuts to all of them
		actions[idx]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	}

	settings.endGroup();
}

void DkNoMacs::assignCustomPluginShortcuts() {
#ifdef WITH_PLUGINS

	QSettings& settings = Settings::instance().getSettings();
	settings.beginGroup("CustomPluginShortcuts");
	QStringList psKeys = settings.allKeys();
	settings.endGroup();

	if (psKeys.size() > 0) {

		settings.beginGroup("CustomShortcuts");

		mPluginsDummyActions = QVector<QAction *>();

		for (int i = 0; i< psKeys.size(); i++) {

			QAction* action = new QAction(psKeys.at(i), this);
			QString val = settings.value(psKeys.at(i), "no-shortcut").toString();
			if (val != "no-shortcut")
				action->setShortcut(val);
			connect(action, SIGNAL(triggered()), this, SLOT(runPluginFromShortcut()));
			// assign widget shortcuts to all of them
			action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
			mPluginsDummyActions.append(action);
			qDebug() << "new plugin action: " << psKeys.at(i);
		}

		settings.endGroup();
		centralWidget()->addActions(mPluginsDummyActions.toList());
	}

#endif // WITH_PLUGINS
}

void DkNoMacs::colorizeIcons(const QColor& col) {

	// now colorize all icons
	for (int idx = 0; idx < mFileIcons.size(); idx++) {

		// never colorize these large icons
		if (idx == icon_file_open_large || idx == icon_file_dir_large)
			continue;

		mFileIcons[idx].addPixmap(DkImage::colorizePixmap(mFileIcons[idx].pixmap(100, QIcon::Normal, QIcon::On), col), QIcon::Normal, QIcon::On);
		mFileIcons[idx].addPixmap(DkImage::colorizePixmap(mFileIcons[idx].pixmap(100, QIcon::Normal, QIcon::Off), col), QIcon::Normal, QIcon::Off);
	}

	// now colorize all icons
	for (int idx = 0; idx < mEditIcons.size(); idx++)
		mEditIcons[idx].addPixmap(DkImage::colorizePixmap(mEditIcons[idx].pixmap(100), col));

	for (int idx = 0; idx < mViewIcons.size(); idx++)
		mViewIcons[idx].addPixmap(DkImage::colorizePixmap(mViewIcons[idx].pixmap(100), col));

	for (int idx = 0; idx < mToolsIcons.size(); idx++)
		mToolsIcons[idx].addPixmap(DkImage::colorizePixmap(mToolsIcons[idx].pixmap(100), col));
}

void DkNoMacs::createShortcuts() {

	DkViewPort* vp = viewport();

	mShortcuts.resize(sc_end);

	mShortcuts[sc_test_img] = new QShortcut(shortcut_test_img, this);
	QObject::connect(mShortcuts[sc_test_img], SIGNAL(activated()), vp, SLOT(loadLena()));

	mShortcuts[sc_test_rec] = new QShortcut(shortcut_test_rec, this);
	QObject::connect(mShortcuts[sc_test_rec], SIGNAL(activated()), this, SLOT(loadRecursion()));

	mShortcuts[sc_test_pong] = new QShortcut(shortcut_pong, this);
	QObject::connect(mShortcuts[sc_test_pong], SIGNAL(activated()), this, SLOT(startPong()));

	for (int idx = 0; idx < mShortcuts.size(); idx++) {

		// assign widget shortcuts to all of them
		mShortcuts[idx]->setContext(Qt::WidgetWithChildrenShortcut);
	}
}

void DkNoMacs::enableNoImageActions(bool enable) {

	mFileActions[menu_file_save]->setEnabled(enable);
	mFileActions[menu_file_save_as]->setEnabled(enable);
	mFileActions[menu_file_save_web]->setEnabled(enable);
	mFileActions[menu_file_rename]->setEnabled(enable);
	mFileActions[menu_file_print]->setEnabled(enable);
	mFileActions[menu_file_reload]->setEnabled(enable);
	mFileActions[menu_file_prev]->setEnabled(enable);
	mFileActions[menu_file_next]->setEnabled(enable);
	mFileActions[menu_file_goto]->setEnabled(enable);
	mFileActions[menu_file_find]->setEnabled(enable);

	mEditActions[menu_edit_rotate_cw]->setEnabled(enable);
	mEditActions[menu_edit_rotate_ccw]->setEnabled(enable);
	mEditActions[menu_edit_rotate_180]->setEnabled(enable);
	mEditActions[menu_edit_delete]->setEnabled(enable);
	mEditActions[menu_edit_transform]->setEnabled(enable);
	mEditActions[menu_edit_crop]->setEnabled(enable);
	mEditActions[menu_edit_copy]->setEnabled(enable);
	mEditActions[menu_edit_copy_buffer]->setEnabled(enable);
	mEditActions[menu_edit_copy_color]->setEnabled(enable);
	mEditActions[menu_edit_wallpaper]->setEnabled(enable);
	mEditActions[menu_edit_flip_h]->setEnabled(enable);
	mEditActions[menu_edit_flip_v]->setEnabled(enable);
	mEditActions[menu_edit_norm]->setEnabled(enable);
	mEditActions[menu_edit_auto_adjust]->setEnabled(enable);
#ifdef WITH_OPENCV
	mEditActions[menu_edit_unsharp]->setEnabled(enable);
#else
	editActions[menu_edit_unsharp]->setEnabled(false);
#endif
#ifdef WITH_OPENCV
	mEditActions[menu_edit_tiny_planet]->setEnabled(enable);
#else
	editActions[menu_edit_tiny_planet]->setEnabled(false);
#endif

	mEditActions[menu_edit_invert]->setEnabled(enable);
	mEditActions[menu_edit_gray_convert]->setEnabled(enable);	

	mToolsActions[menu_tools_thumbs]->setEnabled(enable);
	
	mPanelActions[menu_panel_info]->setEnabled(enable);
#ifdef WITH_OPENCV
	mPanelActions[menu_panel_histogram]->setEnabled(enable);
#else
	panelActions[menu_panel_histogram]->setEnabled(false);
#endif
	mPanelActions[menu_panel_scroller]->setEnabled(enable);
	mPanelActions[menu_panel_comment]->setEnabled(enable);
	mPanelActions[menu_panel_preview]->setEnabled(enable);
	mPanelActions[menu_panel_exif]->setEnabled(enable);
	mPanelActions[menu_panel_overview]->setEnabled(enable);
	mPanelActions[menu_panel_player]->setEnabled(enable);
	
	mViewActions[menu_view_fullscreen]->setEnabled(enable);
	mViewActions[menu_view_reset]->setEnabled(enable);
	mViewActions[menu_view_100]->setEnabled(enable);
	mViewActions[menu_view_fit_frame]->setEnabled(enable);
	mViewActions[menu_view_zoom_in]->setEnabled(enable);
	mViewActions[menu_view_zoom_out]->setEnabled(enable);

#ifdef WITH_OPENCV
	mToolsActions[menu_tools_manipulation]->setEnabled(enable);
#else
	toolsActions[menu_tools_manipulation]->setEnabled(false);
#endif

	QList<QAction* > actions = mOpenWithMenu->actions();
	for (int idx = 0; idx < actions.size()-1; idx++)
		actions.at(idx)->setEnabled(enable);

}

void DkNoMacs::enableMovieActions(bool enable) {

	DkSettings::app.showMovieToolBar=enable;
	mViewActions[menu_view_movie_pause]->setEnabled(enable);
	mViewActions[menu_view_movie_prev]->setEnabled(enable);
	mViewActions[menu_view_movie_next]->setEnabled(enable);

	mViewActions[menu_view_movie_pause]->setChecked(false);
	
	if (enable)
		addToolBar(mMovieToolbar);
	else
		removeToolBar(mMovieToolbar);
	
	if (mToolbar->isVisible())
		mMovieToolbar->setVisible(enable);
}

void DkNoMacs::clearFileHistory() {
	DkSettings::global.recentFiles.clear();
}

void DkNoMacs::clearFolderHistory() {
	DkSettings::global.recentFolders.clear();
}


DkViewPort* DkNoMacs::viewport() const {

	DkCentralWidget* cw = dynamic_cast<DkCentralWidget*>(centralWidget());

	if (!cw)
		return 0;

	return cw->getViewPort();
}

DkCentralWidget* DkNoMacs::getTabWidget() const {

	DkCentralWidget* cw = dynamic_cast<DkCentralWidget*>(centralWidget());
	return cw;
}

void DkNoMacs::updateAll() {

	QWidgetList w = QApplication::topLevelWidgets();
	for (int idx = 0; idx < w.size(); idx++) {
		if (w[idx]->objectName().contains(QString("DkNoMacs")))
			w[idx]->update();
	}
}

//QWidget* DkNoMacs::getDialogParent() {
//
//	QWidgetList wList = QApplication::topLevelWidgets();
//	for (int idx = 0; idx < wList.size(); idx++) {
//		if (wList[idx]->objectName().contains(QString("DkNoMacs")))
//			return wList[idx];
//	}
//
//	return 0;
//}

// Qt how-to
void DkNoMacs::closeEvent(QCloseEvent *event) {

	DkCentralWidget* cw = static_cast<DkCentralWidget*>(centralWidget());

	if (cw && cw->getTabs().size() > 1) {
		
		DkMessageBox* msg = new DkMessageBox(QMessageBox::Question, tr("Quit nomacs"), 
			tr("Do you want nomacs to save your tabs?"), 
			(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel), this);
		msg->setButtonText(QMessageBox::Yes, tr("&Save and Quit"));
		msg->setButtonText(QMessageBox::No, tr("&Quit"));
		msg->setObjectName("saveTabsDialog");

		int answer = msg->exec();
	
		if (answer == QMessageBox::Cancel || answer == QMessageBox::NoButton) {	// User canceled - do not close
			event->ignore();
			return;
		}

		cw->saveSettings(answer == QMessageBox::Yes);
	}
	else
		cw->saveSettings(false);

	if (viewport()) {
		if (!viewport()->unloadImage(true)) {
			// do not close if the user hit cancel in the save changes dialog
			event->ignore();
			return;
		}
	}

	emit closeSignal();
	qDebug() << "saving window settings...";
	setVisible(false);
	//showNormal();

	if (mSaveSettings) {
		QSettings& settings = Settings::instance().getSettings();
		settings.setValue("geometryNomacs", geometry());
		settings.setValue("geometry", saveGeometry());
		settings.setValue("windowState", saveState());
		
		if (mExplorer)
			settings.setValue(mExplorer->objectName(), QMainWindow::dockWidgetArea(mExplorer));
		if (mMetaDataDock)
			settings.setValue(mMetaDataDock->objectName(), QMainWindow::dockWidgetArea(mMetaDataDock));
		if (mThumbsDock)
			settings.setValue(mThumbsDock->objectName(), QMainWindow::dockWidgetArea(mThumbsDock));

		DkSettings::save();
	}

	QMainWindow::closeEvent(event);
}

void DkNoMacs::resizeEvent(QResizeEvent *event) {

	QMainWindow::resizeEvent(event);
	
	if (!mOverlaid)
		mOldGeometry = geometry();
	else if (windowOpacity() < 1.0f) {
		animateChangeOpacity();
		mOverlaid = false;
	}

}

void DkNoMacs::moveEvent(QMoveEvent *event) {

	QMainWindow::moveEvent(event);

	if (!mOverlaid)
		mOldGeometry = geometry();
	else if (windowOpacity() < 1.0f) {
		animateChangeOpacity();
		mOverlaid = false;
	}
}

void DkNoMacs::mouseDoubleClickEvent(QMouseEvent* event) {

	if (event->button() != Qt::LeftButton || viewport() && viewport()->getImage().isNull())
		return;

	if (isFullScreen())
		exitFullScreen();
	else
		enterFullScreen();

	//QMainWindow::mouseDoubleClickEvent(event);
}


void DkNoMacs::mousePressEvent(QMouseEvent* event) {

	mMousePos = event->pos();

	QMainWindow::mousePressEvent(event);
}

void DkNoMacs::mouseReleaseEvent(QMouseEvent *event) {

	QMainWindow::mouseReleaseEvent(event);
}

void DkNoMacs::contextMenuEvent(QContextMenuEvent *event) {

	QMainWindow::contextMenuEvent(event);

	if (!event->isAccepted())
		mContextMenu->exec(event->globalPos());
}

void DkNoMacs::mouseMoveEvent(QMouseEvent *event) {

	QMainWindow::mouseMoveEvent(event);
}

bool DkNoMacs::gestureEvent(QGestureEvent *event) {
	
	DkViewPort* vp = viewport();

	if (QGesture *swipe = event->gesture(Qt::SwipeGesture)) {
		QSwipeGesture* swipeG = static_cast<QSwipeGesture *>(swipe);

		qDebug() << "swipe detected\n";
		if (vp) {
			
			if (swipeG->horizontalDirection() == QSwipeGesture::Left)
				vp->loadNextFileFast();
			else if (swipeG->horizontalDirection() == QSwipeGesture::Right)
				vp->loadPrevFileFast();

			// TODO: recognize some other gestures please
		}

	}
	else if (QGesture *pan = event->gesture(Qt::PanGesture)) {
		
		QPanGesture* panG = static_cast<QPanGesture *>(pan);

		qDebug() << "you're speedy: " << panG->acceleration();

		QPointF delta = panG->delta();

		if (panG->acceleration() > 10 && delta.x() && fabs(delta.y()/delta.x()) < 0.2) {
			
			if (delta.x() < 0)
				vp->loadNextFileFast();
			else
				vp->loadPrevFileFast();
		}

		if (vp)
			vp->moveView(panG->delta());
	}
	else if (QGesture *pinch = event->gesture(Qt::PinchGesture)) {

		QPinchGesture* pinchG = static_cast<QPinchGesture *>(pinch);

		//if (pinchG->changeFlags() == QPinchGesture::ChangeFlag.ScaleFactorChanged) {
		qDebug() << "scale Factor: " << pinchG->scaleFactor();
		if (pinchG->scaleFactor() != 0 && vp) {
			vp->zoom((float)pinchG->scaleFactor());
		}
		else if (pinchG->rotationAngle() != 0 && vp) {

			float angle = (float)pinchG->rotationAngle();
			qDebug() << "angle: " << angle;
			//vp->rotate(angle);
		}
	}

	qDebug() << "gesture event (NoMacs)";

	//	pinchTriggered(static_cast<QPinchGesture *>(pinch));
	return true;
}

void DkNoMacs::flipImageHorizontal() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();
	img = img.mirrored(true, false);

	if (img.isNull())
		vp->getController()->setInfo(tr("Sorry, I cannot Flip the Image..."));
	else
		vp->setEditedImage(img);
}

void DkNoMacs::flipImageVertical() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();
	img = img.mirrored(false, true);

	if (img.isNull())
		vp->getController()->setInfo(tr("Sorry, I cannot Flip the Image..."));
	else
		vp->setEditedImage(img);

}

void DkNoMacs::invertImage() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();
	img.invertPixels();

	if (img.isNull())
		vp->getController()->setInfo(tr("Sorry, I cannot Invert the Image..."));
	else
		vp->setEditedImage(img);

}

void DkNoMacs::convert2gray() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();

	QVector<QRgb> table(256);
	for(int i=0;i<256;++i)
		table[i]=qRgb(i,i,i);

	img = img.convertToFormat(QImage::Format_Indexed8,table);

	if (img.isNull())
		vp->getController()->setInfo(tr("Sorry, I cannot convert the Image..."));
	else
		vp->setEditedImage(img);
}

void DkNoMacs::normalizeImage() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();
	
	bool normalized = DkImage::normImage(img);

	if (!normalized || img.isNull())
		vp->getController()->setInfo(tr("The Image is Already Normalized..."));
	else
		vp->setEditedImage(img);
}

void DkNoMacs::autoAdjustImage() {

	DkViewPort* vp = viewport();

	if (!vp)
		return;

	QImage img = vp->getImage();

	bool normalized = DkImage::autoAdjustImage(img);

	if (!normalized || img.isNull())
		vp->getController()->setInfo(tr("Sorry, I cannot Auto Adjust"));
	else
		vp->setEditedImage(img);
}

void DkNoMacs::unsharpMask() {
#ifdef WITH_OPENCV
	DkUnsharpDialog* unsharpDialog = new DkUnsharpDialog(this);
	unsharpDialog->setImage(viewport()->getImage());
	int answer = unsharpDialog->exec();
	if (answer == QDialog::Accepted) {
		QImage editedImage = unsharpDialog->getImage();
		viewport()->setEditedImage(editedImage);
	}

	unsharpDialog->deleteLater();
#endif
}

void DkNoMacs::tinyPlanet() {

#ifdef WITH_OPENCV
	
	DkTinyPlanetDialog* tinyPlanetDialog = new DkTinyPlanetDialog(this);
	tinyPlanetDialog->setImage(viewport()->getImage());
	
	int answer = tinyPlanetDialog->exec();

	if (answer == QDialog::Accepted) {
		QImage editedImage = tinyPlanetDialog->getImage();
		viewport()->setEditedImage(editedImage);
	}

	tinyPlanetDialog->deleteLater();
#endif
}

void DkNoMacs::readSettings() {
	
	qDebug() << "reading settings...";
	QSettings& settings = Settings::instance().getSettings();

#ifdef Q_WS_WIN
	// fixes #392 - starting maximized on 2nd screen - tested on win8 only
	QRect r = settings.value("geometryNomacs", QRect()).toRect();

	if (r.width() && r.height())	// do not set the geometry if nomacs is loaded the first time
		setGeometry(r);
#endif

	restoreGeometry(settings.value("geometry").toByteArray());
	restoreState(settings.value("windowState").toByteArray());
}

void DkNoMacs::restart() {
	
	if (!viewport()) 
		return;

	QString exe = QApplication::applicationFilePath();
	QStringList args;

	if (getTabWidget()->getCurrentImage())
		args.append(getTabWidget()->getCurrentImage()->filePath());

	bool started = mProcess.startDetached(exe, args);

	// close me if the new instance started
	if (started)
		close();
}

void DkNoMacs::toggleFullScreen() {

	if (isFullScreen())
		exitFullScreen();
	else
		enterFullScreen();
}

void DkNoMacs::enterFullScreen() {
	
	DkSettings::app.currentAppMode += qFloor(DkSettings::mode_end*0.5f);
	if (DkSettings::app.currentAppMode < 0) {
		qDebug() << "illegal state: " << DkSettings::app.currentAppMode;
		DkSettings::app.currentAppMode = DkSettings::mode_default;
	}
	
	menuBar()->hide();
	mToolbar->hide();
	mMovieToolbar->hide();
	mStatusbar->hide();
	getTabWidget()->showTabs(false);

	showExplorer(DkDockWidget::testDisplaySettings(DkSettings::app.showExplorer), false);
	showMetaDataDock(DkDockWidget::testDisplaySettings(DkSettings::app.showMetaDataDock), false);

	DkSettings::app.maximizedMode = isMaximized();
	setWindowState(Qt::WindowFullScreen);
	
	if (viewport())
		viewport()->setFullScreen(true);

	update();
}

void DkNoMacs::exitFullScreen() {

	if (isFullScreen()) {
		DkSettings::app.currentAppMode -= qFloor(DkSettings::mode_end*0.5f);
		if (DkSettings::app.currentAppMode < 0) {
			qDebug() << "illegal state: " << DkSettings::app.currentAppMode;
			DkSettings::app.currentAppMode = DkSettings::mode_default;
		}

		if (DkSettings::app.showMenuBar) mMenu->show();
		if (DkSettings::app.showToolBar) mToolbar->show();
		if (DkSettings::app.showStatusBar) mStatusbar->show();
		if (DkSettings::app.showMovieToolBar) mMovieToolbar->show();
		showExplorer(DkDockWidget::testDisplaySettings(DkSettings::app.showExplorer), false);
		showMetaDataDock(DkDockWidget::testDisplaySettings(DkSettings::app.showMetaDataDock), false);

		if(DkSettings::app.maximizedMode) 
			setWindowState(Qt::WindowMaximized);
		else 
			setWindowState(Qt::WindowNoState);
		
		if (getTabWidget())
			getTabWidget()->showTabs(true);

		update();	// if no resize is triggered, the mViewport won't change its color
	}

	if (viewport())
		viewport()->setFullScreen(false);
}

void DkNoMacs::setFrameless(bool) {

	if (!viewport()) 
		return;

	QString exe = QApplication::applicationFilePath();
	QStringList args;

	if (getTabWidget()->getCurrentImage())
		args.append(getTabWidget()->getCurrentImage()->filePath());
	
	if (objectName() != "DkNoMacsFrameless") {
		DkSettings::app.appMode = DkSettings::mode_frameless;
        //args.append("-graphicssystem");
        //args.append("native");
    } else {
		DkSettings::app.appMode = DkSettings::mode_default;
    }
	
	DkSettings::save();
	
	bool started = mProcess.startDetached(exe, args);

	// close me if the new instance started
	if (started)
		close();

	qDebug() << "frameless arguments: " << args;
}

void DkNoMacs::startPong() const {

	QString exe = QApplication::applicationFilePath();
	QStringList args;

	args.append("-pong");

	bool started = mProcess.startDetached(exe, args);
	qDebug() << "pong started: " << started;
}

void DkNoMacs::fitFrame() {

	QRectF viewRect = viewport()->getImageViewRect();
	QRectF vpRect = viewport()->geometry();
	QRectF nmRect = frameGeometry();
	QSize frDiff = frameGeometry().size()-geometry().size();

	// compute new size
	QPointF c = nmRect.center();
	nmRect.setSize(nmRect.size() + viewRect.size() - vpRect.size());
	nmRect.moveCenter(c);
	
	// still fits on screen?
	QDesktopWidget* dw = QApplication::desktop();
	QRect screenRect = dw->availableGeometry(this);
	QRect newGeometry = screenRect.intersected(nmRect.toRect());
	
	// correct frame
	newGeometry.setSize(newGeometry.size()-frDiff);
	newGeometry.moveTopLeft(newGeometry.topLeft() - frameGeometry().topLeft()+geometry().topLeft());

	setGeometry(newGeometry);

	// reset mViewport if we did not clip -> compensates round-off errors
	if (screenRect.contains(nmRect.toRect()))
		viewport()->resetView();

}

void DkNoMacs::setRecursiveScan(bool recursive) {

	DkSettings::global.scanSubFolders = recursive;

	QSharedPointer<DkImageLoader> loader = getTabWidget()->getCurrentImageLoader();
	
	if (!loader)
		return;

	if (recursive)
		viewport()->getController()->setInfo(tr("Recursive Folder Scan is Now Enabled"));
	else
		viewport()->getController()->setInfo(tr("Recursive Folder Scan is Now Disabled"));

	loader->updateSubFolders(loader->getDirPath());
}

void DkNoMacs::showOpacityDialog() {

	if (!mOpacityDialog) {
		mOpacityDialog = new DkOpacityDialog(this);
		mOpacityDialog->setWindowTitle(tr("Change Opacity"));
	}
	
	if (mOpacityDialog->exec())
		setWindowOpacity(mOpacityDialog->value()/100.0f);
}

void DkNoMacs::opacityDown() {

	changeOpacity(-0.3f);
}

void DkNoMacs::opacityUp() {
	
	changeOpacity(0.3f);
}

void DkNoMacs::changeOpacity(float change) {

	float newO = (float)windowOpacity() + change;
	if (newO > 1) newO = 1.0f;
	if (newO < 0.1) newO = 0.1f;
	setWindowOpacity(newO);
}

void DkNoMacs::animateOpacityDown() {

	float newO = (float)windowOpacity() - 0.03f;

	if (newO < 0.3f) {
		setWindowOpacity(0.3f);
		return;
	}

	setWindowOpacity(newO);
	QTimer::singleShot(20, this, SLOT(animateOpacityDown()));
}

void DkNoMacs::animateOpacityUp() {

	float newO = (float)windowOpacity() + 0.03f;

	if (newO > 1.0f) {
		setWindowOpacity(1.0f);
		return;
	}

	setWindowOpacity(newO);
	QTimer::singleShot(20, this, SLOT(animateOpacityUp()));
}

// >DIR: diem - why can't we put it in mViewport?
void DkNoMacs::animateChangeOpacity() {

	float newO = (float)windowOpacity();

	if (newO >= 1.0f)
		animateOpacityDown();
	else
		animateOpacityUp();
}

void DkNoMacs::lockWindow(bool lock) {

	
#ifdef WIN32
	
	qDebug() << "locking: " << lock;

	if (lock) {
		//setAttribute(Qt::WA_TransparentForMouseEvents);
		HWND hwnd = (HWND) winId(); // get handle of the widget
		LONG styles = GetWindowLong(hwnd, GWL_EXSTYLE);
		SetWindowLong(hwnd, GWL_EXSTYLE, styles | WS_EX_TRANSPARENT); 
		SetWindowPos((HWND)this->winId(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		viewport()->getController()->setInfo(tr("Window Locked\nTo unlock: gain focus (ALT+Tab),\nthen press CTRL+SHIFT+ALT+B"), 5000);
	}
	else if (lock && windowOpacity() == 1.0f) {
		viewport()->getController()->setInfo(tr("You should first reduce opacity\n before working through the window."));
		mViewActions[menu_view_lock_window]->setChecked(false);
	}
	else {
		qDebug() << "deactivating...";
		HWND hwnd = (HWND) winId(); // get handle of the widget
		LONG styles = GetWindowLong(hwnd, GWL_EXSTYLE);
		SetWindowLong(hwnd, GWL_EXSTYLE, styles & ~WS_EX_TRANSPARENT); 

		SetWindowPos((HWND)this->winId(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
#else
	// TODO: find corresponding command for linux etc

	//setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
	//show();
#endif
}

void DkNoMacs::newClientConnected(bool connected, bool) {
	mOverlaid = false;
	// add methods if clients are connected

	mSyncActions[menu_sync]->setEnabled(connected);
	mSyncActions[menu_sync_pos]->setEnabled(connected);
	mSyncActions[menu_sync_arrange]->setEnabled(connected);

}

void DkNoMacs::tcpSetWindowRect(QRect newRect, bool opacity, bool overlaid) {

	this->mOverlaid = overlaid;

	DkUtils::printDebug(DK_MODULE, "arranging...\n");

	// we are currently overlaid...
	if (!overlaid) {

		setGeometry(mOldGeometry);
		if (opacity)
			animateOpacityUp();
		mOldGeometry = geometry();
	}
	else {

#ifdef WIN32
		showMinimized();
		setWindowState(Qt::WindowActive);
#else
		Qt::WindowFlags flags = windowFlags();
		setWindowFlags(Qt::WindowStaysOnTopHint);	// we need this to 'generally' (for all OSs) bring the window to front
		setWindowFlags(flags);	// reset flags
		showNormal();
#endif

		mOldGeometry = geometry();
		
		this->move(newRect.topLeft());
		this->resize(newRect.size() - (frameGeometry().size() - geometry().size()));

		//setGeometry(newRect);
		if (opacity)
			animateOpacityDown();
		
		//this->setActiveWindow();
	}
};

void DkNoMacs::tcpSendWindowRect() {

	mOverlaid = !mOverlaid;

	qDebug() << "overlaying";
	// change my geometry
	tcpSetWindowRect(this->frameGeometry(), !mOverlaid, mOverlaid);

	emit sendPositionSignal(frameGeometry(), mOverlaid);

};

void DkNoMacs::tcpSendArrange() {
	
	mOverlaid = !mOverlaid;
	emit sendArrangeSignal(mOverlaid);
}

void DkNoMacs::showExplorer(bool show, bool saveSettings) {

	if (!mExplorer) {

		// get last location
		mExplorer = new DkExplorer(tr("File Explorer"));
		mExplorer->registerAction(mPanelActions[menu_panel_explorer]);
		mExplorer->setDisplaySettings(&DkSettings::app.showExplorer);
		addDockWidget(mExplorer->getDockLocationSettings(Qt::LeftDockWidgetArea), mExplorer);

		connect(mExplorer, SIGNAL(openFile(const QString&)), getTabWidget(), SLOT(loadFile(const QString&)));
		connect(mExplorer, SIGNAL(openDir(const QString&)), getTabWidget()->getThumbScrollWidget(), SLOT(setDir(const QString&)));
		connect(getTabWidget(), SIGNAL(imageUpdatedSignal(QSharedPointer<DkImageContainerT>)), mExplorer, SLOT(setCurrentImage(QSharedPointer<DkImageContainerT>)));
	}

	mExplorer->setVisible(show, saveSettings);

	if (getTabWidget()->getCurrentImage() && QFileInfo(getTabWidget()->getCurrentFilePath()).exists()) {
		mExplorer->setCurrentPath(getTabWidget()->getCurrentFilePath());
	}
	else {
		QStringList folders = DkSettings::global.recentFiles;

		if (folders.size() > 0)
			mExplorer->setCurrentPath(folders[0]);
	}

}

void DkNoMacs::showMetaDataDock(bool show, bool saveSettings) {

	if (!mMetaDataDock) {

		mMetaDataDock = new DkMetaDataDock(tr("Meta Data Info"), this);
		mMetaDataDock->registerAction(mPanelActions[menu_panel_metadata_dock]);
		mMetaDataDock->setDisplaySettings(&DkSettings::app.showMetaDataDock);
		addDockWidget(mMetaDataDock->getDockLocationSettings(Qt::RightDockWidgetArea), mMetaDataDock);

		connect(getTabWidget(), SIGNAL(imageUpdatedSignal(QSharedPointer<DkImageContainerT>)), mMetaDataDock, SLOT(setImage(QSharedPointer<DkImageContainerT>)));
	}

	mMetaDataDock->setVisible(show, saveSettings);

	if (getTabWidget()->getCurrentImage())
		mMetaDataDock->setImage(getTabWidget()->getCurrentImage());
}

void DkNoMacs::showThumbsDock(bool show) {

	
	// nothing todo here
	if (mThumbsDock && mThumbsDock->isVisible() && show)
		return;
	
	int winPos = viewport()->getController()->getFilePreview()->getWindowPosition();

	if (winPos != DkFilePreview::cm_pos_dock_hor && winPos != DkFilePreview::cm_pos_dock_ver) {
		if (mThumbsDock) {

			//DkSettings::display.thumbDockSize = qMin(thumbsDock->width(), thumbsDock->height());
			QSettings& settings = Settings::instance().getSettings();
			settings.setValue("thumbsDockLocation", QMainWindow::dockWidgetArea(mThumbsDock));

			mThumbsDock->hide();
			mThumbsDock->setWidget(0);
			mThumbsDock->deleteLater();
			mThumbsDock = 0;
		}
		return;
	}

	if (!mThumbsDock) {
		mThumbsDock = new DkDockWidget(tr("Thumbnails"), this);
		mThumbsDock->registerAction(mPanelActions[menu_panel_preview]);
		mThumbsDock->setDisplaySettings(&DkSettings::app.showFilePreview);
		mThumbsDock->setWidget(viewport()->getController()->getFilePreview());
		addDockWidget(mThumbsDock->getDockLocationSettings(Qt::TopDockWidgetArea), mThumbsDock);
		thumbsDockAreaChanged();

		QLabel* thumbsTitle = new QLabel(mThumbsDock);
		thumbsTitle->setObjectName("thumbsTitle");
		thumbsTitle->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		thumbsTitle->setPixmap(QPixmap(":/nomacs/img/widget-separator.png").scaled(QSize(16, 4)));
		thumbsTitle->setFixedHeight(16);
		mThumbsDock->setTitleBarWidget(thumbsTitle);

		connect(mThumbsDock, SIGNAL(dockLocationChanged(Qt::DockWidgetArea)), this, SLOT(thumbsDockAreaChanged()));
	}

	if (show != mThumbsDock->isVisible())
		mThumbsDock->setVisible(show);
}

void DkNoMacs::thumbsDockAreaChanged() {

	Qt::DockWidgetArea area = dockWidgetArea(mThumbsDock);

	int thumbsOrientation = DkFilePreview::cm_pos_dock_hor;

	if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
		thumbsOrientation = DkFilePreview::cm_pos_dock_ver;

	viewport()->getController()->getFilePreview()->setWindowPosition(thumbsOrientation);

}

void DkNoMacs::openDir() {

	// load system default open dialog
	QString dirName = QFileDialog::getExistingDirectory(this, tr("Open an Image Directory"),
		getTabWidget()->getCurrentDir());

	if (dirName.isEmpty())
		return;

	qDebug() << "loading directory: " << dirName;
	
	getTabWidget()->loadFile(dirName);
}

void DkNoMacs::openFile() {

	if (!viewport())
		return;

	QStringList openFilters = DkSettings::app.openFilters;
	openFilters.pop_front();
	openFilters.prepend(tr("All Files (*.*)"));

	// load system default open dialog
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open Image"),
		getTabWidget()->getCurrentDir(), 
		openFilters.join(";;"));

	if (fileName.isEmpty())
		return;

	qDebug() << "os filename: " << fileName;
	getTabWidget()->loadFile(fileName);
}

void DkNoMacs::openQuickLaunch() {

	// create new model
	if (!mQuickAccess) {
		mQuickAccess = new DkQuickAccess(this);
		
		// add all actions
		mQuickAccess->addActions(mFileActions);
		mQuickAccess->addActions(mOpenWithMenu->actions().toVector());
		mQuickAccess->addActions(mSortActions);
		mQuickAccess->addActions(mEditActions);
		mQuickAccess->addActions(mViewActions);
		mQuickAccess->addActions(mPanelActions);
		mQuickAccess->addActions(mToolsActions);
		mQuickAccess->addActions(mSyncActions);
#ifdef WITH_PLUGINS
		createPluginsMenu();
		mQuickAccess->addActions(mPluginsActions);
#endif // WITH_PLUGINS
		mQuickAccess->addActions(mHelpActions);

		connect(mToolbar->getCompleter(), SIGNAL(activated(const QModelIndex&)), mQuickAccess, SLOT(fireAction(const QModelIndex&)));
		connect(mQuickAccess, SIGNAL(loadFileSignal(const QString&)), getTabWidget(), SLOT(loadFile(const QString&)));
		//connect(toolbar, SIGNAL(quickAccessFinishedSignal(const QModelIndex&)), quickAccess, SLOT(fireAction(const QModelIndex&)));
	}
	
	mQuickAccess->addDirs(DkSettings::global.recentFolders);
	mQuickAccess->addFiles(DkSettings::global.recentFiles);

	mToolbar->setQuickAccessModel(mQuickAccess->getModel());
}

void DkNoMacs::loadFile(const QString& filePath) {

	if (!viewport())
		return;

	getTabWidget()->loadFileToTab(filePath);
}

void DkNoMacs::renameFile() {

	QFileInfo file = getTabWidget()->getCurrentFilePath();

	if (!file.absoluteDir().exists()) {
		viewport()->getController()->setInfo(tr("Sorry, the directory: %1  does not exist\n").arg(file.absolutePath()));
		return;
	}
	if (file.exists() && !file.isWritable()) {
		viewport()->getController()->setInfo(tr("Sorry, I can't write to the fileInfo: %1").arg(file.fileName()));
		return;
	}

	bool ok;
	QString filename = QInputDialog::getText(this, file.baseName(), tr("Rename:"), QLineEdit::Normal, file.baseName(), &ok);

	if (ok && !filename.isEmpty() && filename != file.baseName()) {
		
		if (!file.suffix().isEmpty())
			filename.append("." + file.suffix());
		
		qDebug() << "renaming: " << file.fileName() << " -> " << filename;
		QFileInfo renamedFile = QFileInfo(file.absoluteDir(), filename);

		// overwrite file?
		if (renamedFile.exists()) {

			QMessageBox infoDialog(this);
			infoDialog.setWindowTitle(tr("Question"));
			infoDialog.setText(tr("The fileInfo: %1  already exists.\n Do you want to replace it?").arg(filename));
			infoDialog.setIcon(QMessageBox::Question);
			infoDialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			infoDialog.setDefaultButton(QMessageBox::No);
			infoDialog.show();
			int choice = infoDialog.exec();

			if (choice == QMessageBox::Yes) {

				QFile oldFile(renamedFile.absoluteFilePath());
				bool removed = oldFile.remove();

				// tell user that deleting went wrong, and stop the renaming
				if (!removed) {
					viewport()->getController()->setInfo(tr("Sorry, I can't delete: %1").arg(file.fileName()));
					return;
				}
			}
			else
				return;		// cancel renaming
		}

		viewport()->unloadImage();

		QFile newFile(file.absoluteFilePath());
		bool renamed = newFile.rename(renamedFile.absoluteFilePath());
		
		// tell user that deleting went wrong, and stop the renaming
		if (!renamed)
			viewport()->getController()->setInfo(tr("Sorry, I can't rename: %1").arg(file.fileName()));
		else
			getTabWidget()->loadFile(renamedFile.absoluteFilePath());
		
	}

}

void DkNoMacs::find(bool filterAction) {

	if(!getCurrRunningPlugin().isEmpty()) 
		applyPluginChanges(true, false);

	if (!viewport() || !getTabWidget()->getCurrentImageLoader())
		return;

	if (filterAction) {

		int db = (QObject::sender() == mToolsActions[menu_tools_filter]) ? DkSearchDialog::filter_button : DkSearchDialog::find_button;
		
		qDebug() << "default button: " << db;
		DkSearchDialog* searchDialog = new DkSearchDialog(this);
		searchDialog->setDefaultButton(db);

		searchDialog->setFiles(getTabWidget()->getCurrentImageLoader()->getFileNames());
		searchDialog->setPath(getTabWidget()->getCurrentImageLoader()->getDirPath());

		connect(searchDialog, SIGNAL(filterSignal(const QStringList&)), getTabWidget()->getCurrentImageLoader().data(), SLOT(setFolderFilters(const QStringList&)));
		connect(searchDialog, SIGNAL(loadFileSignal(const QString&)), getTabWidget(), SLOT(loadFile(const QString&)));
		int answer = searchDialog->exec();

		mToolsActions[menu_tools_filter]->setChecked(answer == DkSearchDialog::filter_button);		
	}
	else {
		// remove the filter 
		getTabWidget()->getCurrentImageLoader()->setFolderFilters(QStringList());
	}

}

void DkNoMacs::changeSorting(bool change) {

	if (change) {
	
		QString senderName = QObject::sender()->objectName();

		if (senderName == "menu_sort_filename")
			DkSettings::global.sortMode = DkSettings::sort_filename;
		else if (senderName == "menu_sort_date_created")
			DkSettings::global.sortMode = DkSettings::sort_date_created;
		else if (senderName == "menu_sort_date_modified")
			DkSettings::global.sortMode = DkSettings::sort_date_modified;
		else if (senderName == "menu_sort_random")
			DkSettings::global.sortMode = DkSettings::sort_random;
		else if (senderName == "menu_sort_ascending")
			DkSettings::global.sortDir = DkSettings::sort_ascending;
		else if (senderName == "menu_sort_descending")
			DkSettings::global.sortDir = DkSettings::sort_descending;

		if (getTabWidget()->getCurrentImageLoader()) 
			getTabWidget()->getCurrentImageLoader()->sort();
	}

	for (int idx = 0; idx < mSortActions.size(); idx++) {

		if (idx < menu_sort_ascending)
			mSortActions[idx]->setChecked(idx == DkSettings::global.sortMode);
		else if (idx >= menu_sort_ascending)
			mSortActions[idx]->setChecked(idx-menu_sort_ascending == DkSettings::global.sortDir);
	}
}

void DkNoMacs::goTo() {

	if(!getCurrRunningPlugin().isEmpty()) 
		applyPluginChanges(true, false);

	if (!viewport() || !getTabWidget()->getCurrentImageLoader())
		return;

	QSharedPointer<DkImageLoader> loader = getTabWidget()->getCurrentImageLoader();
	
	bool ok = false;
	int fileIdx = QInputDialog::getInt(this, tr("Go To Image"), tr("Image Index:"), 0, 0, loader->numFiles()-1, 1, &ok);

	if (ok)
		loader->loadFileAt(fileIdx);

}

void DkNoMacs::trainFormat() {

	if (!viewport())
		return;

	if (!mTrainDialog)
		mTrainDialog = new DkTrainDialog(this);

	mTrainDialog->setCurrentFile(getTabWidget()->getCurrentFilePath());
	bool okPressed = mTrainDialog->exec() != 0;

	if (okPressed && getTabWidget()->getCurrentImageLoader()) {
		getTabWidget()->getCurrentImageLoader()->load(mTrainDialog->getAcceptedFile());
		restart();	// quick & dirty, but currently he messes up the filteredFileList if the same folder was already loaded
	}


}

void DkNoMacs::extractImagesFromArchive() {
#ifdef WITH_QUAZIP
	if (!viewport())
		return;

	if (!mArchiveExtractionDialog)
		mArchiveExtractionDialog = new DkArchiveExtractionDialog(this);

	if (getTabWidget()->getCurrentImage()) {
		if (getTabWidget()->getCurrentImage()->isFromZip())
			mArchiveExtractionDialog->setCurrentFile(getTabWidget()->getCurrentImage()->getZipData()->getZipFilePath(), true);
		else 
			mArchiveExtractionDialog->setCurrentFile(getTabWidget()->getCurrentFilePath(), false);
	}
	else 
		mArchiveExtractionDialog->setCurrentFile(getTabWidget()->getCurrentFilePath(), false);

	mArchiveExtractionDialog->exec();
#endif
}


void DkNoMacs::saveFile() {

	saveFileAs(true);
}

void DkNoMacs::saveFileAs(bool silent) {
	
	qDebug() << "saving...";

	if(!mActivePlugin.isEmpty()) 
		applyPluginChanges(true, true);

	if (getTabWidget()->getCurrentImageLoader())
		getTabWidget()->getCurrentImageLoader()->saveUserFileAs(getTabWidget()->getViewPort()->getImage(), silent);
}

void DkNoMacs::saveFileWeb() {

	if (getTabWidget()->getCurrentImageLoader())
		getTabWidget()->getCurrentImageLoader()->saveFileWeb(getTabWidget()->getViewPort()->getImage());
}

void DkNoMacs::resizeImage() {


	if(!getCurrRunningPlugin().isEmpty()) 
		applyPluginChanges(true, false);

	if (!viewport() || viewport()->getImage().isNull())
		return;

	if (!mResizeDialog)
		mResizeDialog = new DkResizeDialog(this);

	QSharedPointer<DkImageContainerT> imgC = getTabWidget()->getCurrentImage();
	QSharedPointer<DkMetaDataT> metaData;

	if (imgC) {
		metaData = imgC->getMetaData();
		QVector2D res = metaData->getResolution();
		mResizeDialog->setExifDpi((float)res.x());
	}

	qDebug() << "resize image: " << viewport()->getImage().size();


	mResizeDialog->setImage(viewport()->getImage());

	if (!mResizeDialog->exec())
		return;

	if (mResizeDialog->resample()) {

		QImage rImg = mResizeDialog->getResizedImage();

		if (!rImg.isNull()) {

			// this reloads the image -> that's not what we want!
			if (metaData)
				metaData->setResolution(QVector2D(mResizeDialog->getExifDpi(), mResizeDialog->getExifDpi()));

			imgC->setImage(rImg);
			viewport()->setEditedImage(imgC);
		}
	}
	else if (metaData) {
		// ok, user just wants to change the resolution
		metaData->setResolution(QVector2D(mResizeDialog->getExifDpi(), mResizeDialog->getExifDpi()));
		qDebug() << "setting resolution to: " << mResizeDialog->getExifDpi();
		//mViewport()->setEditedImage(mViewport()->getImage());
	}
}

void DkNoMacs::deleteFile() {

	if(!getCurrRunningPlugin().isEmpty()) 
		applyPluginChanges(false, false);

	if (!viewport() || viewport()->getImage().isNull() || !getTabWidget()->getCurrentImageLoader())
		return;

	QFileInfo fileInfo = getTabWidget()->getCurrentFilePath();

	if (QMessageBox::question(this, tr("Info"), tr("Do you want to permanently delete %1").arg(fileInfo.fileName())) == QMessageBox::Yes) {
		viewport()->stopMovie();	// movies keep file handles so stop it before we can delete files
		
		if (!getTabWidget()->getCurrentImageLoader()->deleteFile())
			viewport()->loadMovie();	// load the movie again, if we could not delete it
	}
}

void DkNoMacs::openAppManager() {

	DkAppManagerDialog* appManagerDialog = new DkAppManagerDialog(mAppManager, this, windowFlags());
	connect(appManagerDialog, SIGNAL(openWithSignal(QAction*)), this, SLOT(openFileWith(QAction*)));
	appManagerDialog->exec();

	appManagerDialog->deleteLater();

	mOpenWithMenu->clear();
	createOpenWithMenu(mOpenWithMenu);
}

void DkNoMacs::exportTiff() {

#ifdef WITH_LIBTIFF
	if (!mExportTiffDialog)
		mExportTiffDialog = new DkExportTiffDialog(this);

	mExportTiffDialog->setFile(getTabWidget()->getCurrentFilePath());
	mExportTiffDialog->exec();
#endif
}

void DkNoMacs::computeMosaic() {
#ifdef WITH_OPENCV

	if(!getCurrRunningPlugin().isEmpty()) applyPluginChanges(true, false);

	//if (!mosaicDialog)
	DkMosaicDialog* mosaicDialog = new DkMosaicDialog(this, Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

	mosaicDialog->setFile(getTabWidget()->getCurrentFilePath());

	int response = mosaicDialog->exec();

	if (response == QDialog::Accepted && !mosaicDialog->getImage().isNull()) {
		QImage editedImage = mosaicDialog->getImage();
		viewport()->setEditedImage(editedImage);
		saveFileAs();
	}

	mosaicDialog->deleteLater();
#endif
}

void DkNoMacs::openImgManipulationDialog() {

	if(!getCurrRunningPlugin().isEmpty()) applyPluginChanges(true, false);

	if (!viewport() || viewport()->getImage().isNull())
		return;

	if (!mImgManipulationDialog)
		mImgManipulationDialog = new DkImageManipulationDialog(this);
	else 
		mImgManipulationDialog->resetValues();

	QImage tmpImg = viewport()->getImage();
	mImgManipulationDialog->setImage(&tmpImg);

	bool ok = mImgManipulationDialog->exec() != 0;

	if (ok) {

#ifdef WITH_OPENCV

		QImage mImg = DkImage::mat2QImage(DkImageManipulationWidget::manipulateImage(DkImage::qImage2Mat(viewport()->getImage())));

		if (!mImg.isNull())
			viewport()->setEditedImage(mImg);

#endif
	}
}


void DkNoMacs::setWallpaper() {

	// based on code from: http://qtwiki.org/Set_windows_background_using_QT

	QImage img = viewport()->getImage();

	QImage dImg = img;

	QSharedPointer<DkImageLoader> loader = QSharedPointer<DkImageLoader>(new DkImageLoader());
	QFileInfo tmpPath = loader->saveTempFile(dImg, "wallpaper", ".jpg", true, false);
	
	// is there a more elegant way to see if saveTempFile returned an empty path
	if (tmpPath.absoluteFilePath() == QFileInfo().absoluteFilePath()) {
		QMessageBox::critical(this, tr("Error"), tr("Sorry, I could not create a wallpaper..."));
		return;
	}

#ifdef WIN32

	//Read current windows background image path
	QSettings appSettings( "HKEY_CURRENT_USER\\Control Panel\\Desktop", QSettings::NativeFormat);
	appSettings.setValue("Wallpaper", tmpPath.absoluteFilePath());

	QByteArray ba = tmpPath.absoluteFilePath().toLatin1();
	SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (void*)ba.data(), SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
#endif
	// TODO: add functionality for unix based systems
}

void DkNoMacs::printDialog() {

	if(!getCurrRunningPlugin().isEmpty()) applyPluginChanges(true, false);

	QPrinter printer;

	QVector2D res(150,150);
	QSharedPointer<DkImageContainerT> imgC = getTabWidget()->getCurrentImage();
	
	if (imgC)
		res = imgC->getMetaData()->getResolution();

	//QPrintPreviewDialog* previewDialog = new QPrintPreviewDialog();
	QImage img = viewport()->getImage();
	if (!mPrintPreviewDialog)
		mPrintPreviewDialog = new DkPrintPreviewDialog(img, (float)res.x(), 0, this);
	else
		mPrintPreviewDialog->setImage(img, (float)res.x());

	mPrintPreviewDialog->show();
	mPrintPreviewDialog->updateZoomFactor(); // otherwise the initial zoom factor is wrong

}

void DkNoMacs::computeThumbsBatch() {

	if (!viewport())
		return;

	if (!mForceDialog)
		mForceDialog = new DkForceThumbDialog(this);
	mForceDialog->setWindowTitle(tr("Save Thumbnails"));
	mForceDialog->setDir(getTabWidget()->getCurrentDir());

	if (!mForceDialog->exec())
		return;

	if (!mThumbSaver)
		mThumbSaver = new DkThumbsSaver(this);
	
	if (getTabWidget()->getCurrentImageLoader())
		mThumbSaver->processDir(getTabWidget()->getCurrentImageLoader()->getImages(), mForceDialog->forceSave());
}

void DkNoMacs::aboutDialog() {

	DkSplashScreen* spScreen = new DkSplashScreen(this, 0);
	spScreen->exec();
	spScreen->deleteLater();
}

void DkNoMacs::openDocumentation() {

	QString url = QString("http://www.nomacs.org/documentation/");

	QDesktopServices::openUrl(QUrl(url));
}

void DkNoMacs::bugReport() {

	QString url = QString("http://www.nomacs.org/redmine/projects/nomacs/")
		% QString("issues/new?issue[tracker_id]=1&issue[custom_field_values][1]=")
		% QApplication::applicationVersion();

	url += "&issue[custom_field_values][4]=";
#if defined WIN32 &&	_MSC_VER == 1600
	url += "Windows XP";
#elif defined WIN32 && _WIN64
	url += "Windows Vista/7/8 64bit";
#elif defined WIN32 && _WIN32
	url += "Windows Vista/7/8 32bit";
#elif defined Q_WS_X11 && __x86_64__	// >DIR: check if qt5 still supports these flags [19.2.2014 markus]
	url += "Linux 64bit";
#elif defined Q_WS_X11 && __i386__
	url += "Linux 32bit";
#elif defined Q_WS_MAC
	url += "Mac OS";
#else
	url += "";
#endif

	
	QDesktopServices::openUrl(QUrl(url));
}

void DkNoMacs::featureRequest() {
	
	QString url = QString("http://www.nomacs.org/redmine/projects/nomacs/")
		% QString("issues/new?issue[tracker_id]=2&issue[custom_field_values][1]=")
		% QApplication::applicationVersion();

	url += "&issue[custom_field_values][4]=";
#if defined WIN32 &&	_MSC_VER == 1600
	url += "Windows Vista/XP";
#elif defined WIN32 && _WIN64
	url += "Windows 7/8/10 64bit";
#elif defined WIN32 && _WIN32
	url += "Windows 7/8/10 32bit";
#elif defined Q_WS_X11 && __x86_64__
	url += "Linux 64bit";
#elif defined Q_WS_X11 && __i386__
	url += "Linux 32bit";
#elif defined Q_WS_MAC
	url += "Mac OS";
#else
	url += "";
#endif

	QDesktopServices::openUrl(QUrl(url));
}

void DkNoMacs::cleanSettings() {

	QSettings& settings = Settings::instance().getSettings();
	settings.clear();

	readSettings();
	resize(400, 225);
	move(100, 100);
}

void DkNoMacs::newInstance(const QString& filePath) {

	if (!viewport()) 
		return;

	QString exe = QApplication::applicationFilePath();
	QStringList args;

	QAction* a = static_cast<QAction*>(sender());

	if (a && a == mFileActions[menu_file_private_instance])
		args.append("-p");

	if (filePath.isEmpty())
		args.append(getTabWidget()->getCurrentFilePath());
	else
		args.append(filePath);

	if (objectName() == "DkNoMacsFrameless")
		args.append("1");	
	
	QProcess::startDetached(exe, args);
}

void DkNoMacs::loadRecursion() {

	if (!getTabWidget()->getCurrentImage())
		return;

	viewport()->toggleDissolve();


	//QImage img = getTabWidget()->getCurrentImage()->image();

	//while (DkImage::addToImage(img, 1)) {
	//	mViewport()->setEditedImage(img);
	//	QApplication::sendPostedEvents();
	//}

	//QImage img = QPixmap::grabWindow(this->winId()).toImage();
	//mViewport()->setImage(img);
}

// Added by fabian for transfer function:

void DkNoMacs::setContrast(bool contrast) {

	qDebug() << "contrast: " << contrast;

	if (!viewport()) 
		return;

	QString exe = QApplication::applicationFilePath();
	QStringList args;
	args.append(getTabWidget()->getCurrentFilePath());
	
	if (contrast)
		DkSettings::app.appMode = DkSettings::mode_contrast;
	else
		DkSettings::app.appMode = DkSettings::mode_default;

	bool started = mProcess.startDetached(exe, args);

	// close me if the new instance started
	if (started)
		close();

	qDebug() << "contrast arguments: " << args;
}

void DkNoMacs::showRecentFiles(bool show) {

	if (DkSettings::app.appMode != DkSettings::mode_frameless && !DkSettings::global.recentFiles.empty())
		getTabWidget()->showRecentFiles(show);

}

void DkNoMacs::onWindowLoaded() {

	QSettings& settings = Settings::instance().getSettings();
	bool firstTime = settings.value("AppSettings/firstTime.nomacs.3", true).toBool();

	if (DkDockWidget::testDisplaySettings(DkSettings::app.showExplorer))
		showExplorer(true);
	if (DkDockWidget::testDisplaySettings(DkSettings::app.showMetaDataDock))
		showMetaDataDock(true);

	if (firstTime) {

		// here are some first time requests
		DkWelcomeDialog* wecomeDialog = new DkWelcomeDialog(this);
		wecomeDialog->exec();

		settings.setValue("AppSettings/firstTime.nomacs.3", false);

		if (wecomeDialog->isLanguageChanged()) {
			restartWithTranslationUpdate();
		}
	}

	checkForUpdate(true);

	// load settings AFTER everything is initialized
	getTabWidget()->loadSettings();

}

void DkNoMacs::keyPressEvent(QKeyEvent *event) {
	
	if (event->key() == Qt::Key_Alt) {
		mPosGrabKey = QCursor::pos();
		mOtherKeyPressed = false;
	}
	else
		mOtherKeyPressed = true;

}

void DkNoMacs::keyReleaseEvent(QKeyEvent* event) {

	if (event->key() == Qt::Key_Alt && !mOtherKeyPressed && (mPosGrabKey - QCursor::pos()).manhattanLength() == 0)
		mMenu->showMenu();
	
}

// >DIR diem: eating shortcut overrides (this allows us to use navigation keys like arrows)
bool DkNoMacs::eventFilter(QObject*, QEvent* event) {

	if (event->type() == QEvent::ShortcutOverride) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

		// consume esc key if fullscreen is on
		if (keyEvent->key() == Qt::Key_Escape && isFullScreen()) {
			exitFullScreen();
			return true;
		}
		else if (keyEvent->key() == Qt::Key_Escape && DkSettings::app.closeOnEsc)
			close();
	}
	if (event->type() == QEvent::Gesture) {
		return gestureEvent(static_cast<QGestureEvent*>(event));
	}

	return false;
}

void DkNoMacs::showMenuBar(bool show) {

	DkSettings::app.showMenuBar = show;
	int tts = (DkSettings::app.showMenuBar) ? -1 : 5000;
	mPanelActions[menu_panel_menu]->setChecked(DkSettings::app.showMenuBar);
	mMenu->setTimeToShow(tts);
	mMenu->showMenu();

	if (!show)
		mMenu->hide();
}

void DkNoMacs::showToolbar(QToolBar* toolbar, bool show) {

	if (!toolbar)
		return;

	showToolbarsTemporarily(!show);

	if (show) {
		addToolBar(toolBarArea(this->mToolbar), toolbar);
	}
	else
		removeToolBar(toolbar);

	toolbar->setVisible(show);
}

void DkNoMacs::showToolbarsTemporarily(bool show) {

	if (show) {
		for (int idx = 0; idx < mHiddenToolbars.size(); idx++)
			mHiddenToolbars.at(idx)->show();
	}
	else {

		mHiddenToolbars.clear();
		QList<QToolBar *> toolbars = findChildren<QToolBar *>();

		for (int idx = 0; idx < toolbars.size(); idx++) {
			
			if (toolbars.at(idx)->isVisible()) {
				toolbars.at(idx)->hide();
				mHiddenToolbars.append(toolbars.at(idx));
			}
		}
	}
}

void DkNoMacs::showToolbar(bool show) {

	DkSettings::app.showToolBar = show;
	mPanelActions[menu_panel_toolbar]->setChecked(DkSettings::app.showToolBar);
	
	if (DkSettings::app.showToolBar)
		mToolbar->show();
	else
		mToolbar->hide();
}

void DkNoMacs::showStatusBar(bool show, bool permanent) {

	if (mStatusbar->isVisible() == show)
		return;

	if (permanent)
		DkSettings::app.showStatusBar = show;
	mPanelActions[menu_panel_statusbar]->setChecked(DkSettings::app.showStatusBar);

	mStatusbar->setVisible(show);

	viewport()->setVisibleStatusbar(show);
}

void DkNoMacs::showStatusMessage(QString msg, int which) {

	if (which < 0 || which >= mStatusbarLabels.size())
		return;

	mStatusbarLabels[which]->setVisible(!msg.isEmpty());
	mStatusbarLabels[which]->setText(msg);
}

void DkNoMacs::openFileWith(QAction* action) {

	if (!action)
		return;

	QFileInfo app(action->toolTip());

	if (!app.exists())
		viewport()->getController()->setInfo("Sorry, " % app.fileName() % " does not exist");

	QStringList args;
	
	QString filePath = getTabWidget()->getCurrentFilePath();

	if (app.fileName() == "explorer.exe")
		args << "/select," << QDir::toNativeSeparators(filePath);
	else if (app.fileName().toLower() == "outlook.exe") {
		args << "/a" << QDir::toNativeSeparators(filePath);
	}
	else
		args << QDir::toNativeSeparators(filePath);

	//bool started = process.startDetached("psOpenImages.exe", args);	// already deprecated
	bool started = mProcess.startDetached(app.absoluteFilePath(), args);

	if (started)
		qDebug() << "starting: " << app.fileName() << args;
	else if (viewport())
		viewport()->getController()->setInfo("Sorry, I could not start: " % app.absoluteFilePath());
}

void DkNoMacs::showGpsCoordinates() {

	QSharedPointer<DkMetaDataT> metaData = getTabWidget()->getCurrentImage()->getMetaData();

	if (!DkMetaDataHelper::getInstance().hasGPS(metaData)) {
		viewport()->getController()->setInfo("Sorry, I could not find the GPS coordinates...");
		return;
	}

	qDebug() << "gps: " << DkMetaDataHelper::getInstance().getGpsCoordinates(metaData);

	QDesktopServices::openUrl(QUrl(DkMetaDataHelper::getInstance().getGpsCoordinates(metaData)));  
}

QVector <QAction* > DkNoMacs::getFileActions() {

	return mFileActions;
}

QVector <QAction* > DkNoMacs::getBatchActions() {

	return mToolsActions;
}

QVector <QAction* > DkNoMacs::getPanelActions() {

	return mPanelActions;
}

QVector <QAction* > DkNoMacs::getViewActions() {

	return mViewActions;
}

QVector <QAction* > DkNoMacs::getSyncActions() {

	return mSyncActions;
}

void DkNoMacs::setWindowTitle(QSharedPointer<DkImageContainerT> imgC) {

	if (!imgC) {
		setWindowTitle(QString());
		return;
	}

	setWindowTitle(imgC->filePath(), imgC->image().size(), imgC->isEdited(), imgC->getTitleAttribute());
}

void DkNoMacs::setWindowTitle(const QString& filePath, const QSize& size, bool edited, const QString& attr) {

	// TODO: rename!

	QFileInfo fInfo = filePath;
	QString title = QFileInfo(filePath).fileName();
	title = title.remove(".lnk");
	
	if (title.isEmpty()) {
		title = "nomacs - Image Lounge";
		if (DkSettings::app.privateMode) 
			title.append(tr(" [Private Mode]"));
	}

	if (edited)
		title.append("[*]");

	title.append(" ");
	title.append(attr);	// append some attributes

	QString attributes;

	if (!size.isEmpty())
		attributes.sprintf(" - %i x %i", size.width(), size.height());
	if (size.isEmpty() && viewport())
		attributes.sprintf(" - %i x %i", viewport()->getImage().width(), viewport()->getImage().height());
	if (DkSettings::app.privateMode) 
		attributes.append(tr(" [Private Mode]"));

	QMainWindow::setWindowTitle(title.append(attributes));
	setWindowFilePath(filePath);
	emit sendTitleSignal(windowTitle());
	setWindowModified(edited);

	if ((!viewport()->getController()->getFileInfoLabel()->isVisible() || 
		!DkSettings::slideShow.display.testBit(DkSettings::display_creation_date)) && getTabWidget()->getCurrentImage()) {
		
		// create statusbar info
		QSharedPointer<DkMetaDataT> metaData = getTabWidget()->getCurrentImage()->getMetaData();
		QString dateString = metaData->getExifValue("DateTimeOriginal");
		dateString = DkUtils::convertDateString(dateString, fInfo);
		showStatusMessage(dateString, status_time_info);
	}
	else 
		showStatusMessage("", status_time_info);	// hide label

	if (fInfo.exists())
		showStatusMessage(DkUtils::readableByte((float)fInfo.size()), status_filesize_info);
	else 
		showStatusMessage("", status_filesize_info);

}

void DkNoMacs::openKeyboardShortcuts() {

	QList<QAction* > openWithActionList = mOpenWithMenu->actions();

	DkShortcutsDialog* shortcutsDialog = new DkShortcutsDialog(this);
	shortcutsDialog->addActions(mFileActions, mFileMenu->title());
	shortcutsDialog->addActions(openWithActionList.toVector(), mOpenWithMenu->title());
	shortcutsDialog->addActions(mSortActions, mSortMenu->title());
	shortcutsDialog->addActions(mEditActions, mEditMenu->title());
	shortcutsDialog->addActions(mViewActions, mViewMenu->title());
	shortcutsDialog->addActions(mPanelActions, mPanelMenu->title());
	shortcutsDialog->addActions(mToolsActions, mToolsMenu->title());
	shortcutsDialog->addActions(mSyncActions, mSyncMenu->title());
#ifdef WITH_PLUGINS
	createPluginsMenu();

	QVector<QAction*> allPluginActions = mPluginsActions;

	for (const QMenu* m : mPluginSubMenus) {
		allPluginActions << m->actions().toVector();
	}

	shortcutsDialog->addActions(allPluginActions, mPluginsMenu->title());
#endif // WITH_PLUGINS
	shortcutsDialog->addActions(mHelpActions, mHelpMenu->title());

	shortcutsDialog->exec();

}

void DkNoMacs::openSettings() {

	if (!mSettingsDialog) {
		mSettingsDialog = new DkSettingsDialog(this);
		connect(mSettingsDialog, SIGNAL(setToDefaultSignal()), this, SLOT(cleanSettings()));
		connect(mSettingsDialog, SIGNAL(settingsChanged()), viewport(), SLOT(settingsChanged()));
		connect(mSettingsDialog, SIGNAL(languageChanged()), this, SLOT(restartWithTranslationUpdate()));
		connect(mSettingsDialog, SIGNAL(settingsChangedRestart()), this, SLOT(restart()));
		connect(mSettingsDialog, SIGNAL(settingsChanged()), this, SLOT(settingsChanged()));
	}

	mSettingsDialog->exec();

	qDebug() << "hier k�nnte ihre werbung stehen...";
}

void DkNoMacs::settingsChanged() {
	
	if (!isFullScreen()) {
		showMenuBar(DkSettings::app.showMenuBar);
		showToolbar(DkSettings::app.showToolBar);
		showStatusBar(DkSettings::app.showStatusBar);
	}
}

void DkNoMacs::checkForUpdate(bool silent) {

	// updates are supported on windows only
#ifndef Q_WS_X11

	// do we really need to check for update?
	if (!silent || !DkSettings::sync.updateDialogShown && QDate::currentDate() > DkSettings::sync.lastUpdateCheck && DkSettings::sync.checkForUpdates) {

		DkTimer dt;

		if (!mUpdater) {
			mUpdater = new DkUpdater(this);
			connect(mUpdater, SIGNAL(displayUpdateDialog(QString, QString)), this, SLOT(showUpdateDialog(QString, QString)));
			connect(mUpdater, SIGNAL(showUpdaterMessage(QString, QString)), this, SLOT(showUpdaterMessage(QString, QString)));
		}
		mUpdater->silent = silent;
		mUpdater->checkForUpdates();

		qDebug() << "checking for updates takes: " << dt.getTotal();
	}
#endif // !#ifndef Q_WS_X11
}

void DkNoMacs::showUpdaterMessage(QString msg, QString title) {
	
	QMessageBox infoDialog(this);
	infoDialog.setWindowTitle(title);
	infoDialog.setIcon(QMessageBox::Information);
	infoDialog.setText(msg);
	infoDialog.show();

	infoDialog.exec();
}

void DkNoMacs::showUpdateDialog(QString msg, QString title) {
	
	if (mProgressDialog != 0 && !mProgressDialog->isHidden()) { // check if the progress bar is already open 
		showUpdaterMessage(tr("Already downloading update"), "update");
		return;
	}

	DkSettings::sync.updateDialogShown = true;

	DkSettings::save();
	
	if (!mUpdateDialog) {
		mUpdateDialog = new DkUpdateDialog(this);
		mUpdateDialog->setWindowTitle(title);
		mUpdateDialog->upperLabel->setText(msg);
		connect(mUpdateDialog, SIGNAL(startUpdate()), this, SLOT(performUpdate()));
	}

	mUpdateDialog->exec();
}

void DkNoMacs::performUpdate() {
	
	if (!mUpdater) {
		qDebug() << "WARNING updater is NULL where it should not be.";
		return;
	}

	mUpdater->performUpdate();

	if (!mProgressDialog) {
		mProgressDialog = new QProgressDialog(tr("Downloading update..."), tr("Cancel Update"), 0, 100, this);
		mProgressDialog->setWindowIcon(windowIcon());
		connect(mProgressDialog, SIGNAL(canceled()), mUpdater, SLOT(cancelUpdate()));
		connect(mUpdater, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(updateProgress(qint64, qint64)));
		connect(mUpdater, SIGNAL(downloadFinished(QString)), mProgressDialog, SLOT(close()));
		//connect(updater, SIGNAL(downloadFinished(QString)), progressDialog, SLOT(deleteLater()));
		connect(mUpdater, SIGNAL(downloadFinished(QString)), this, SLOT(startSetup(QString)));
	}
	mProgressDialog->setWindowModality(Qt::ApplicationModal);

	mProgressDialog->show();
	//progressDialog->raise();
	//progressDialog->activateWindow();
	mProgressDialog->setWindowModality(Qt::NonModal);
}

void DkNoMacs::updateProgress(qint64 received, qint64 total) {
	mProgressDialog->setMaximum((int)total);
	mProgressDialog->setValue((int)received);
}

void DkNoMacs::updateProgressTranslations(qint64 received, qint64 total) {
	qDebug() << "rec:" << received << "  total:" << total;
	mProgressDialogTranslations->setMaximum((int)total);
	mProgressDialogTranslations->setValue((int)received);
}

void DkNoMacs::startSetup(QString filePath) {
	
	qDebug() << "starting setup filePath:" << filePath;
	
	if (!QFile::exists(filePath))
		qDebug() << "fileInfo does not exist";
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath))) {
		QString msg = tr("Unable to install new version<br>") +
			tr("You can download the new version from our web page") +
			"<br><a href=\"http://www.nomacs.org/download/\">www.nomacs.org</a><br>";
		showUpdaterMessage(msg, "update");
	}
}

void DkNoMacs::updateTranslations() {
	
	if (!mTranslationUpdater) {
		mTranslationUpdater = new DkTranslationUpdater(false, this);
		connect(mTranslationUpdater, SIGNAL(showUpdaterMessage(QString, QString)), this, SLOT(showUpdaterMessage(QString, QString)));
	}

	if (!mProgressDialogTranslations) {
		mProgressDialogTranslations = new QProgressDialog(tr("Downloading new translations..."), tr("Cancel"), 0, 100, this);
		mProgressDialogTranslations->setWindowIcon(windowIcon());
		connect(mProgressDialogTranslations, SIGNAL(canceled()), mTranslationUpdater, SLOT(cancelUpdate()));
		//connect(progressDialogTranslations, SIGNAL(canceled()), translationUpdater, SLOT(cancelUpdate()));
		connect(mTranslationUpdater, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(updateProgressTranslations(qint64, qint64)));
		connect(mTranslationUpdater, SIGNAL(downloadFinished()), mProgressDialogTranslations, SLOT(close()));
	}
	mProgressDialogTranslations->setWindowModality(Qt::ApplicationModal);

	mProgressDialogTranslations->show();
	//progressDialog->raise();
	//progressDialog->activateWindow();
	mProgressDialogTranslations->setWindowModality(Qt::NonModal);

	mTranslationUpdater->checkForUpdates();
}

void DkNoMacs::restartWithTranslationUpdate() {
	
	if (!mTranslationUpdater) {
		mTranslationUpdater = new DkTranslationUpdater(false, this);
		connect(mTranslationUpdater, SIGNAL(showUpdaterMessage(QString, QString)), this, SLOT(showUpdaterMessage(QString, QString)));
	}

	mTranslationUpdater->silent = true;
	connect(mTranslationUpdater, SIGNAL(downloadFinished()), this, SLOT(restart()));
	updateTranslations();
}

//void DkNoMacs::errorDialog(const QString& msg) {
//	dialog(msg, this, tr("Error"));
//}

//void DkNoMacs::errorDialog(QString msg, QString title) {
//
//	dialog(msg, this, title);
//}

//int DkNoMacs::dialog(QString msg, QWidget* parent, QString title) {
//
//	if (!parent) {
//		QWidgetList w = QApplication::topLevelWidgets();
//
//		for (int idx = 0; idx < w.size(); idx++) {
//
//			if (w[idx]->objectName().contains(QString("DkNoMacs"))) {
//				parent = w[idx];
//				break;
//			}
//		}
//	}
//
//	QMessageBox errorDialog(parent);
//	errorDialog.setWindowTitle(title);
//	errorDialog.setIcon(QMessageBox::Critical);
//	errorDialog.setText(msg);
//	errorDialog.show();
//
//	return errorDialog.exec();
//
//}

//int DkNoMacs::infoDialog(QString msg, QWidget* parent, QString title) {
//
//	QMessageBox errorDialog(parent);
//	errorDialog.setWindowTitle(title);
//	errorDialog.setIcon(QMessageBox::Question);
//	errorDialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
//	errorDialog.setText(msg);
//	errorDialog.show();
//
//	return errorDialog.exec();
//}

/**
* Creates the plugin menu when it is not empty
* called in DkNoMacs::createPluginsMenu()
**/
void DkNoMacs::addPluginsToMenu() {
	
#ifdef WITH_PLUGINS

	QMap<QString, DkPluginInterface *> loadedPlugins = DkPluginManager::instance().getPlugins();
	QList<QString> pluginIdList = DkPluginManager::instance().getPluginIdList();

	QMap<QString, QString> runId2PluginId = QMap<QString, QString>();
	QList<QPair<QString, QString> > sortedNames = QList<QPair<QString, QString> >();
	QStringList pluginMenu = QStringList();

	for (int i = 0; i < pluginIdList.size(); i++) {

		DkPluginInterface* cPlugin = loadedPlugins.value(pluginIdList.at(i));

		if (cPlugin) {

			QStringList runID = cPlugin->runID();
			QList<QAction*> actions = cPlugin->createActions(this);

			if (!actions.empty()) {
				
				for (int iAction = 0; iAction < actions.size(); iAction++) {
					connect(actions.at(iAction), SIGNAL(triggered()), this, SLOT(runLoadedPlugin()));
					runId2PluginId.insert(actions.at(iAction)->data().toString(), pluginIdList.at(i));
				}

				QMenu* sm = new QMenu(cPlugin->pluginMenuName(),this);
				sm->setStatusTip(cPlugin->pluginStatusTip());
				sm->addActions(actions);
				runId2PluginId.insert(cPlugin->pluginMenuName(), pluginIdList.at(i));
			
				mPluginSubMenus.append(sm);

				
			}
			else {
		
				// deprecated!
				for (int j = 0; j < runID.size(); j++) {
				
					runId2PluginId.insert(runID.at(j), pluginIdList.at(i));
					sortedNames.append(qMakePair(runID.at(j), cPlugin->pluginMenuName(runID.at(j))));
				}
			}
		}
	}

	mPluginsMenu->addAction(mPluginsActions[menu_plugin_manager]);
	mPluginsMenu->addSeparator();

	QMap<QString, bool> pluginsEnabled = QMap<QString, bool>();

	QSettings& settings = Settings::instance().getSettings();
	int size = settings.beginReadArray("PluginSettings/disabledPlugins");
	for (int i = 0; i < size; ++i) {
		settings.setArrayIndex(i);
		if (pluginIdList.contains(settings.value("pluginId").toString())) pluginsEnabled.insert(settings.value("pluginId").toString(), false);
	}
	settings.endArray();

	for(int i = 0; i < sortedNames.size(); i++) {


		if (pluginsEnabled.value(runId2PluginId.value(sortedNames.at(i).first), true)) {

			QAction* pluginAction = new QAction(sortedNames.at(i).second, this);
			pluginAction->setStatusTip(loadedPlugins.value(runId2PluginId.value(sortedNames.at(i).first))->pluginStatusTip(sortedNames.at(i).first));
			pluginAction->setData(sortedNames.at(i).first);
			connect(pluginAction, SIGNAL(triggered()), this, SLOT(runLoadedPlugin()));

			centralWidget()->addAction(pluginAction);
			mPluginsMenu->addAction(pluginAction);
			pluginAction->setToolTip(pluginAction->statusTip());

			mPluginsActions.append(pluginAction);
		}		
	}

	for (int idx = 0; idx < mPluginSubMenus.size(); idx++) {

		if (pluginsEnabled.value(runId2PluginId.value(mPluginSubMenus.at(idx)->title()), true))
			mPluginsMenu->addMenu(mPluginSubMenus.at(idx));

	}

	DkPluginManager::instance().setRunId2PluginId(runId2PluginId);

	QVector<QAction*> allPluginActions = mPluginsActions;

	for (const QMenu* m : mPluginSubMenus) {
		allPluginActions << m->actions().toVector();
	}

	assignCustomShortcuts(allPluginActions);
	savePluginActions(allPluginActions);

#endif // WITH_PLUGINS
}

void DkNoMacs::savePluginActions(QVector<QAction *> actions) {
#ifdef WITH_PLUGINS

	QSettings& settings = Settings::instance().getSettings();
	settings.beginGroup("CustomPluginShortcuts");
	settings.remove("");
	for (int i = 0; i < actions.size(); i++)
		settings.setValue(actions.at(i)->text(), actions.at(i)->text());
	settings.endGroup();
#endif // WITH_PLUGINS
}


/**
* Creates the plugins menu 
**/
void DkNoMacs::createPluginsMenu() {
#ifdef WITH_PLUGINS
	qDebug() << "CREATING plugin menu";

	if (!mPluginMenuCreated) {
		QList<QString> pluginIdList = DkPluginManager::instance().getPluginIdList();

		qDebug() << "id list: " << pluginIdList;

		if (pluginIdList.isEmpty()) { // no  plugins
			mPluginsMenu->clear();
			mPluginsActions.resize(menu_plugins_end);
			mPluginsMenu->addAction(mPluginsActions[menu_plugin_manager]);
		}
		else {
			mPluginsMenu->clear();
			// delete old plugin actions	
			for (int idx = mPluginsActions.size(); idx > menu_plugins_end; idx--) {
				mPluginsActions.last()->deleteLater();
				mPluginsActions.last() = 0;
				mPluginsActions.pop_back();
			}
			mPluginsActions.resize(menu_plugins_end);
			addPluginsToMenu();
		}

		mPluginMenuCreated = true;
	}
#endif // WITH_PLUGINS
}

void DkNoMacs::openPluginManager() {
#ifdef WITH_PLUGINS

	if (!mActivePlugin.isEmpty()) {
		closePlugin(true, false);
	}

	if (!mActivePlugin.isEmpty()) {
	   	   
		QMessageBox infoDialog(this);
		infoDialog.setWindowTitle("Close plugin");
		infoDialog.setIcon(QMessageBox::Information);
		infoDialog.setText("Please close the currently opened plugin first.");
		infoDialog.show();

		infoDialog.exec();
		return;
	}

	DkPluginManagerDialog* pluginDialog = new DkPluginManagerDialog(this);
	pluginDialog->exec();
	pluginDialog->deleteLater();

	createPluginsMenu();

#endif // WITH_PLUGINS
}

void DkNoMacs::runLoadedPlugin() {
#ifdef WITH_PLUGINS

   QAction* action = qobject_cast<QAction*>(sender());

   if (!action)
	   return;

   if (!mActivePlugin.isEmpty())
	   closePlugin(true, false);

   if (!mActivePlugin.isEmpty()) {
	   
	    // the plugin is not closed in time
	   
	   	QMessageBox infoDialog(this);
		infoDialog.setWindowTitle("Close plugin");
		infoDialog.setIcon(QMessageBox::Information);
		infoDialog.setText("Please first close the currently opened plugin.");
		infoDialog.show();

		infoDialog.exec();
		return;
   }

   QString key = action->data().toString();
   DkPluginInterface* cPlugin = DkPluginManager::instance().getPlugin(key);

   // something is wrong if no plugin is loaded
   if (!cPlugin) {
	   qDebug() << "plugin is NULL";
	   return;
   }

	if(viewport()->getImage().isNull() && cPlugin->closesOnImageChange()){
		QMessageBox msgBox(this);
		msgBox.setText("No image loaded\nThe plugin can't run.");
		msgBox.setIcon(QMessageBox::Warning);
		msgBox.exec();
		return;
	}

   if (cPlugin->interfaceType() == DkPluginInterface::interface_viewport) {
	    
		DkViewPortInterface* vPlugin = dynamic_cast<DkViewPortInterface*>(cPlugin);

	   if(!vPlugin || !vPlugin->getViewPort()) 
		   return;

	   mActivePlugin = key;
	   	   
	   connect(vPlugin->getViewPort(), SIGNAL(closePlugin(bool, bool)), this, SLOT(closePlugin(bool, bool)));
	   connect(vPlugin->getViewPort(), SIGNAL(showToolbar(QToolBar*, bool)), this, SLOT(showToolbar(QToolBar*, bool)));
	   connect(vPlugin->getViewPort(), SIGNAL(loadFile(QFileInfo)), viewport(), SLOT(loadFile(QFileInfo)));
	   connect(vPlugin->getViewPort(), SIGNAL(loadImage(QImage)), viewport(), SLOT(setImage(QImage)));
	   
	   viewport()->getController()->setPluginWidget(vPlugin, false);
   }
   else if (cPlugin->interfaceType() == DkPluginInterface::interface_basic) {

		QSharedPointer<DkImageContainerT> result = DkImageContainerT::fromImageContainer(cPlugin->runPlugin(key, getTabWidget()->getCurrentImage()));
		if(result) 
			viewport()->setEditedImage(result);
   }
#endif // WITH_PLUGINS
}

void DkNoMacs::runPluginFromShortcut() {
#ifdef WITH_PLUGINS

	qDebug() << "running plugin shortcut...";

	QAction* action = qobject_cast<QAction*>(sender());
	QString actionName = action->text();

	for (int i = 0; i < mPluginsDummyActions.size(); i++)
		viewport()->removeAction(mPluginsDummyActions.at(i));

	createPluginsMenu();
	
	QVector<QAction*> allPluginActions = mPluginsActions;

	for (const QMenu* m : mPluginSubMenus) {
		allPluginActions << m->actions().toVector();
	}

	// this method fails if two plugins have the same action name!!
	for (int i = 0; i < allPluginActions.size(); i++)
		if (allPluginActions.at(i)->text().compare(actionName) == 0) {
			allPluginActions.at(i)->trigger();
			break;
		}
#endif // WITH_PLUGINS
}

void DkNoMacs::closePlugin(bool askForSaving, bool) {
#ifdef WITH_PLUGINS

	if (mActivePlugin.isEmpty())
		return;

	if (!viewport())
		return;

	DkPluginInterface* cPlugin = DkPluginManager::instance().getPlugin(mActivePlugin);


	mActivePlugin = QString();
	bool isSaveNeeded = false;

	if (!cPlugin) 
		return;
	DkViewPortInterface* vPlugin = dynamic_cast<DkViewPortInterface*>(cPlugin);

	if (!vPlugin) 
		return;

	// this is that complicated because we do not want plugins to have threaded containers - this could get weird
	QSharedPointer<DkImageContainerT> pluginImage = DkImageContainerT::fromImageContainer(vPlugin->runPlugin("", getTabWidget()->getCurrentImage()));	// empty vars - mViewport plugin doesn't need them

	if (pluginImage) {
		if (askForSaving) {

			QMessageBox msgBox(this);
			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			msgBox.setDefaultButton(QMessageBox::No);
			msgBox.setEscapeButton(QMessageBox::No);
			msgBox.setIcon(QMessageBox::Question);
			msgBox.setWindowTitle(tr("Closing plugin..."));

			msgBox.setText(tr("Do you want to apply plugin changes?"));

			if(msgBox.exec() == QMessageBox::Yes) {
				viewport()->setEditedImage(pluginImage);
				isSaveNeeded = true;
			}
			msgBox.deleteLater();
		}				
		else 
			viewport()->setEditedImage(pluginImage);
	}

	disconnect(vPlugin->getViewPort(), SIGNAL(showToolbar(QToolBar*, bool)), this, SLOT(showToolbar(QToolBar*, bool)));

	viewport()->getController()->setPluginWidget(vPlugin, true);

	//if(!alreadySaving && isSaveNeeded) saveFileAs();

	//mViewport()->setPluginImageWasApplied(true);

#endif // WITH_PLUGINS
}

void DkNoMacs::applyPluginChanges(bool askForSaving, bool alreadySaving) {

#ifdef WITH_PLUGINS
	if (mActivePlugin.isEmpty())
		return;

	DkPluginInterface* cPlugin = DkPluginManager::instance().getPlugin(mActivePlugin);

	// does the plugin want to be closed on image changes?
	if (!cPlugin->closesOnImageChange())
		return;

	closePlugin(askForSaving, alreadySaving);
#endif // WITH_PLUGINS
}

// DkNoMacsSync --------------------------------------------------------------------
DkNoMacsSync::DkNoMacsSync(QWidget *parent, Qt::WindowFlags flags) : DkNoMacs(parent, flags) {
}

DkNoMacsSync::~DkNoMacsSync() {

	if (mLocalClient) {

		// terminate local client
		mLocalClient->quit();
		mLocalClient->wait();

		delete mLocalClient;
		mLocalClient = 0;
	}

	if (mRcClient) {

		if (DkSettings::sync.syncMode == DkSettings::sync_mode_remote_control)
			mRcClient->sendNewMode(DkSettings::sync_mode_remote_control);	// TODO: if we need this threaded emit a signal here

		emit stopSynchronizeWithSignal();

		mRcClient->quit();
		mRcClient->wait();

		delete mRcClient;
		mRcClient = 0;

	}

}

void DkNoMacsSync::initLanClient() {

	DkTimer dt;
	if (mLanClient) {

		mLanClient->quit();
		mLanClient->wait();

		delete mLanClient;
	}


	// remote control server
	if (mRcClient) {
		mRcClient->quit();
		mRcClient->wait();

		delete mRcClient;
	}

	qDebug() << "client clearing takes: " << dt.getTotal();

	if (!DkSettings::sync.enableNetworkSync) {

		mLanClient = 0;
		mRcClient = 0;

		mTcpLanMenu->setEnabled(false);
		mSyncActions[menu_sync_remote_control]->setEnabled(false);
		mSyncActions[menu_sync_remote_display]->setEnabled(false);
		return;
	}

	mTcpLanMenu->clear();

	// start lan client/server
	mLanClient = new DkLanManagerThread(this);
	mLanClient->setObjectName("lanClient");
#ifdef WITH_UPNP
	if (!upnpControlPoint) {
		upnpControlPoint = QSharedPointer<DkUpnpControlPoint>(new DkUpnpControlPoint());
	}
	lanClient->upnpControlPoint = upnpControlPoint;
	if (!upnpDeviceHost) {
		upnpDeviceHost = QSharedPointer<DkUpnpDeviceHost>(new DkUpnpDeviceHost());
	}
	lanClient->upnpDeviceHost = upnpDeviceHost;
#endif // WITH_UPNP
	mLanClient->start();

	mTcpLanMenu->setClientManager(mLanClient);
	mTcpLanMenu->addTcpAction(mLanActions[menu_lan_server]);
	mTcpLanMenu->addTcpAction(mLanActions[menu_lan_image]);	// well this is a bit nasty... we only add it here to have correct enable/disable behavior...
	mTcpLanMenu->setEnabled(true);
	mTcpLanMenu->enableActions(false, false);

	mRcClient = new DkRCManagerThread(this);
	mRcClient->setObjectName("rcClient");
#ifdef WITH_UPNP
	if (!upnpControlPoint) {
		upnpControlPoint = QSharedPointer<DkUpnpControlPoint>(new DkUpnpControlPoint());
	}
	rcClient->upnpControlPoint = upnpControlPoint;
	if (!upnpDeviceHost) {
		upnpDeviceHost = QSharedPointer<DkUpnpDeviceHost>(new DkUpnpDeviceHost());
	}
	rcClient->upnpDeviceHost = upnpDeviceHost;
#endif // WITH_UPNP
	
	mRcClient->start();
	
	connect(mLanActions[menu_lan_server], SIGNAL(toggled(bool)), this, SLOT(startTCPServer(bool)));	// TODO: something that makes sense...
	connect(mLanActions[menu_lan_image], SIGNAL(triggered()), viewport(), SLOT(tcpSendImage()));

	connect(this, SIGNAL(startTCPServerSignal(bool)), mLanClient, SLOT(startServer(bool)));
	connect(this, SIGNAL(startRCServerSignal(bool)), mRcClient, SLOT(startServer(bool)), Qt::QueuedConnection);

	if (!DkSettings::sync.syncWhiteList.empty()) {
		qDebug() << "whitelist not empty .... starting server";
#ifdef WITH_UPNP
		upnpDeviceHost->startDevicehost(":/nomacs/descriptions/nomacs-device.xml");
#endif // WITH_UPNP

		// TODO: currently blocking : )
		emit startRCServerSignal(true);
		//rcClient->startServer(true);
	}
	else 
		qDebug() << "whitelist empty!!";



	qDebug() << "start server takes: " << dt.getTotal();
}

void DkNoMacsSync::createActions() {

	DkNoMacs::createActions();

	DkViewPort* vp = viewport();

	// sync menu
	mSyncActions.resize(menu_sync_end);
	mSyncActions[menu_sync] = new QAction(tr("Synchronize &View"), this);
	mSyncActions[menu_sync]->setShortcut(QKeySequence(shortcut_sync));
	mSyncActions[menu_sync]->setStatusTip(tr("synchronize the current view"));
	mSyncActions[menu_sync]->setEnabled(false);
	connect(mSyncActions[menu_sync], SIGNAL(triggered()), vp, SLOT(tcpForceSynchronize()));

	mSyncActions[menu_sync_pos] = new QAction(tr("&Window Overlay"), this);
	mSyncActions[menu_sync_pos]->setShortcut(QKeySequence(shortcut_tab));
	mSyncActions[menu_sync_pos]->setStatusTip(tr("toggle the window opacity"));
	mSyncActions[menu_sync_pos]->setEnabled(false);
	connect(mSyncActions[menu_sync_pos], SIGNAL(triggered()), this, SLOT(tcpSendWindowRect()));

	mSyncActions[menu_sync_arrange] = new QAction(tr("Arrange Instances"), this);
	mSyncActions[menu_sync_arrange]->setShortcut(QKeySequence(shortcut_arrange));
	mSyncActions[menu_sync_arrange]->setStatusTip(tr("arrange connected instances"));
	mSyncActions[menu_sync_arrange]->setEnabled(false);
	connect(mSyncActions[menu_sync_arrange], SIGNAL(triggered()), this, SLOT(tcpSendArrange()));

	mSyncActions[menu_sync_connect_all] = new QAction(tr("Connect &All"), this);
	mFileActions[menu_sync_connect_all]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	mSyncActions[menu_sync_connect_all]->setShortcut(QKeySequence(shortcut_connect_all));
	mSyncActions[menu_sync_connect_all]->setStatusTip(tr("connect all instances"));
	connect(mSyncActions[menu_sync_connect_all], SIGNAL(triggered()), this, SLOT(tcpConnectAll()));

	mSyncActions[menu_sync_all_actions] = new QAction(tr("&Sync All Actions"), this);
	mSyncActions[menu_sync_all_actions]->setStatusTip(tr("Transmit All Signals Automatically."));
	mSyncActions[menu_sync_all_actions]->setCheckable(true);
	mSyncActions[menu_sync_all_actions]->setChecked(DkSettings::sync.syncActions);
	connect(mSyncActions[menu_sync_all_actions], SIGNAL(triggered(bool)), this, SLOT(tcpAutoConnect(bool)));

	mSyncActions[menu_sync_start_upnp] = new QAction(tr("&Start Upnp"), this);
	mSyncActions[menu_sync_start_upnp]->setStatusTip(tr("Starts a Upnp Media Renderer."));
	mSyncActions[menu_sync_start_upnp]->setCheckable(true);
	connect(mSyncActions[menu_sync_start_upnp], SIGNAL(triggered(bool)), this, SLOT(startUpnpRenderer(bool)));

	mSyncActions[menu_sync_remote_control] = new QAction(tr("&Remote Control"), this);
	//syncActions[menu_sync_remote_control]->setShortcut(QKeySequence(shortcut_connect_all));
	mSyncActions[menu_sync_remote_control]->setStatusTip(tr("Automatically Receive Images From Your Remote Instance."));
	mSyncActions[menu_sync_remote_control]->setCheckable(true);
	connect(mSyncActions[menu_sync_remote_control], SIGNAL(triggered(bool)), this, SLOT(tcpRemoteControl(bool)));

	mSyncActions[menu_sync_remote_display] = new QAction(tr("Remote &Display"), this);
	mSyncActions[menu_sync_remote_display]->setStatusTip(tr("Automatically Send Images to a Remote Instance."));
	mSyncActions[menu_sync_remote_display]->setCheckable(true);
	connect(mSyncActions[menu_sync_remote_display], SIGNAL(triggered(bool)), this, SLOT(tcpRemoteDisplay(bool)));

	mLanActions.resize(menu_lan_end);

	// start server action
	mLanActions[menu_lan_server] = new QAction(tr("Start &Server"), this);
	mLanActions[menu_lan_server]->setObjectName("serverAction");
	mLanActions[menu_lan_server]->setCheckable(true);
	mLanActions[menu_lan_server]->setChecked(false);

	mLanActions[menu_lan_image] = new QAction(tr("Send &Image"), this);
	mLanActions[menu_lan_image]->setObjectName("sendImageAction");
	mLanActions[menu_lan_image]->setShortcut(QKeySequence(shortcut_send_img));
	//sendImage->setEnabled(false);		// TODO: enable/disable sendImage action as needed
	mLanActions[menu_lan_image]->setToolTip(tr("Sends the current image to all clients."));

	assignCustomShortcuts(mSyncActions);
}

void DkNoMacsSync::createMenu() {

	DkNoMacs::createMenu();

	// local host menu
	mTcpViewerMenu = new DkTcpMenu(tr("&Synchronize"), mSyncMenu, mLocalClient);
	mTcpViewerMenu->showNoClientsFound(true);
	mSyncMenu->addMenu(mTcpViewerMenu);

	// connect all action
	mTcpViewerMenu->addTcpAction(mSyncActions[menu_sync_connect_all]);

	// LAN menu
	mTcpLanMenu = new DkTcpMenu(tr("&LAN Synchronize"), mSyncMenu, mLanClient);	// TODO: replace
	mSyncMenu->addMenu(mTcpLanMenu);

	mSyncMenu->addAction(mSyncActions[menu_sync_remote_control]);
	mSyncMenu->addAction(mSyncActions[menu_sync_remote_display]);
	mSyncMenu->addAction(mLanActions[menu_lan_image]);
	mSyncMenu->addSeparator();

	mSyncMenu->addAction(mSyncActions[menu_sync]);
	mSyncMenu->addAction(mSyncActions[menu_sync_pos]);
	mSyncMenu->addAction(mSyncActions[menu_sync_arrange]);
	mSyncMenu->addAction(mSyncActions[menu_sync_all_actions]);
#ifdef WITH_UPNP
	// disable this action since it does not work using herqq
	//syncMenu->addAction(syncActions[menu_sync_start_upnp]);
#endif // WITH_UPNP

}

// mouse events
void DkNoMacsSync::mouseMoveEvent(QMouseEvent *event) {

	int dist = QPoint(event->pos()-mMousePos).manhattanLength();

	// create drag sync action
	if (event->buttons() == Qt::LeftButton && dist > QApplication::startDragDistance() &&
		event->modifiers() == (Qt::ControlModifier | Qt::AltModifier)) {

			qDebug() << "generating a drag event...";

			QByteArray connectionData;
			QDataStream dataStream(&connectionData, QIODevice::WriteOnly);
			dataStream << mLocalClient->getServerPort();
			qDebug() << "serverport: " << mLocalClient->getServerPort();

			QDrag* drag = new QDrag(this);
			QMimeData* mimeData = new QMimeData;
			mimeData->setData("network/sync-dir", connectionData);

			drag->setMimeData(mimeData);
			drag->exec(Qt::CopyAction | Qt::MoveAction);
	}
	else
		DkNoMacs::mouseMoveEvent(event);

}

void DkNoMacsSync::dragEnterEvent(QDragEnterEvent *event) {

	if (event->mimeData()->hasFormat("network/sync-dir")) {
		event->accept();
	}

	QMainWindow::dragEnterEvent(event);
}

void DkNoMacsSync::dropEvent(QDropEvent *event) {

	if (event->source() == this) {
		event->accept();
		return;
	}

	if (event->mimeData()->hasFormat("network/sync-dir")) {

		QByteArray connectionData = event->mimeData()->data("network/sync-dir");
		QDataStream dataStream(&connectionData, QIODevice::ReadOnly);
		quint16 peerId;
		dataStream >> peerId;

		emit synchronizeWithServerPortSignal(peerId);
		qDebug() << "drop server port: " << peerId;
	}
	else
		QMainWindow::dropEvent(event);

}

void DkNoMacsSync::enableNoImageActions(bool enable /* = true */) {

	DkNoMacs::enableNoImageActions(enable);

	mSyncActions[menu_sync_connect_all]->setEnabled(enable);
}

qint16 DkNoMacsSync::getServerPort() {

	return (mLocalClient) ? mLocalClient->getServerPort() : 0;
}

void DkNoMacsSync::syncWith(qint16 port) {
	emit synchronizeWithServerPortSignal(port);
}

// slots
void DkNoMacsSync::tcpConnectAll() {

	QList<DkPeer*> peers = mLocalClient->getPeerList();

	for (int idx = 0; idx < peers.size(); idx++)
		emit synchronizeWithSignal(peers.at(idx)->peerId);

}

void DkNoMacsSync::tcpChangeSyncMode(int syncMode, bool connectWithWhiteList) {

	if (syncMode == DkSettings::sync.syncMode || !mRcClient)
		return;

	// turn off everything
	if (syncMode == DkSettings::sync_mode_default)
		mRcClient->sendGoodByeToAll();

	mSyncActions[menu_sync_remote_control]->setChecked(false);
	mSyncActions[menu_sync_remote_display]->setChecked(false);

	if (syncMode == DkSettings::sync_mode_default) {
		DkSettings::sync.syncMode = syncMode;
		return;
	}

	// if we do not connect with the white list, the signal came from the rc client
	// so we can easily assume that we are connected
	bool connected = (connectWithWhiteList) ? connectWhiteList(syncMode, DkSettings::sync.syncMode == DkSettings::sync_mode_default) : true;

	if (!connected) {
		DkSettings::sync.syncMode = DkSettings::sync_mode_default;
		viewport()->getController()->setInfo(tr("Sorry, I could not find any clients."));
		return;
	}

	// turn on the new mode
	switch(syncMode) {
		case DkSettings::sync_mode_remote_control: 
			mSyncActions[menu_sync_remote_control]->setChecked(true);	
			break;
		case DkSettings::sync_mode_remote_display: 
			mSyncActions[menu_sync_remote_display]->setChecked(true);
			break;
	//default:
	}

	DkSettings::sync.syncMode = syncMode;
}


void DkNoMacsSync::tcpRemoteControl(bool start) {

	if (!mRcClient)
		return;

	tcpChangeSyncMode((start) ? DkSettings::sync_mode_remote_control : DkSettings::sync_mode_default, true);
}

void DkNoMacsSync::tcpRemoteDisplay(bool start) {

	if (!mRcClient)
		return;

	tcpChangeSyncMode((start) ? DkSettings::sync_mode_remote_display : DkSettings::sync_mode_default, true);

}

void DkNoMacsSync::tcpAutoConnect(bool connect) {

	DkSettings::sync.syncActions = connect;
}

#ifdef WITH_UPNP
void DkNoMacsSync::startUpnpRenderer(bool start) {
	if (!upnpRendererDeviceHost) {
		upnpRendererDeviceHost = QSharedPointer<DkUpnpRendererDeviceHost>(new DkUpnpRendererDeviceHost());
		connect(upnpRendererDeviceHost.data(), SIGNAL(newImage(QImage)), viewport(), SLOT(setImage(QImage)));
	}
	if(start)
		upnpRendererDeviceHost->startDevicehost(":/nomacs/descriptions/nomacs_mediarenderer_description.xml");
	else
		upnpDeviceHost->stopDevicehost();
}
#else
void DkNoMacsSync::startUpnpRenderer(bool) {}	// dummy
#endif // WITH_UPNP

bool DkNoMacsSync::connectWhiteList(int mode, bool connect) {

	if (!mRcClient)
		return false;

	bool couldConnect = false;

	QList<DkPeer*> peers = mRcClient->getPeerList();
	qDebug() << "number of peers in list:" << peers.size();

	// TODO: add gui if idx != 1
	if (connect && !peers.isEmpty()) {
		DkPeer* peer = peers[0];

		emit synchronizeRemoteControl(peer->peerId);
		
		if (mode == DkSettings::sync_mode_remote_control)
			mRcClient->sendNewMode(DkSettings::sync_mode_remote_display);	// TODO: if we need this threaded emit a signal here
		else
			mRcClient->sendNewMode(DkSettings::sync_mode_remote_control);	// TODO: if we need this threaded emit a signal here

		couldConnect = true;
	}
	else if (!connect) {

		if (mode == DkSettings::sync_mode_remote_control)
			mRcClient->sendNewMode(DkSettings::sync_mode_remote_display);	// TODO: if we need this threaded emit a signal here
		else
			mRcClient->sendNewMode(DkSettings::sync_mode_remote_control);	// TODO: if we need this threaded emit a signal here

		emit stopSynchronizeWithSignal();
		couldConnect = true;
	}

	return couldConnect;
}

void DkNoMacsSync::newClientConnected(bool connected, bool local) {

	mTcpLanMenu->enableActions(connected, local);
	
	DkNoMacs::newClientConnected(connected, local);
}

void DkNoMacsSync::startTCPServer(bool start) {
	
#ifdef WITH_UPNP
	if (!upnpDeviceHost->isStarted())
		upnpDeviceHost->startDevicehost(":/nomacs/descriptions/nomacs-device.xml");
#endif // WITH_UPNP
	emit startTCPServerSignal(start);
}

void DkNoMacsSync::settingsChanged() {
	initLanClient();

	DkNoMacs::settingsChanged();
}

void DkNoMacsSync::clientInitialized() {
	
	//TODO: things that need to be done after the clientManager has finished initialization
#ifdef WITH_UPNP
	QObject* obj = QObject::sender();
	if (obj && (obj->objectName() == "lanClient" || obj->objectName() == "rcClient")) {
		qDebug() << "sender:" << obj->objectName();
		if (!upnpControlPoint->isStarted()) {
			qDebug() << "initializing upnpControlPoint";
			upnpControlPoint->init();
		}
	} 
#endif // WITH_UPNP
	
	emit clientInitializedSignal();
}

DkNoMacsIpl::DkNoMacsIpl(QWidget *parent, Qt::WindowFlags flags) : DkNoMacsSync(parent, flags) {

		// init members
	DkViewPort* vp = new DkViewPort(this);
	vp->setAlignment(Qt::AlignHCenter);

	DkCentralWidget* cw = new DkCentralWidget(vp, this);
	setCentralWidget(cw);

	mLocalClient = new DkLocalManagerThread(this);
	mLocalClient->setObjectName("localClient");
	mLocalClient->start();

	mLanClient = 0;
	mRcClient = 0;


	init();
	setAcceptDrops(true);
	setMouseTracking (true);	//receive mouse event everytime

	DkTimer dt;
		
	// sync signals
	connect(vp, SIGNAL(newClientConnectedSignal(bool, bool)), this, SLOT(newClientConnected(bool, bool)));

	DkSettings::app.appMode = 0;
	initLanClient();
	//emit sendTitleSignal(windowTitle());
	qDebug() << "LAN client created in: " << dt.getTotal();
	// show it...
	show();
	DkSettings::app.appMode = DkSettings::mode_default;

	qDebug() << "mViewport (normal) created in " << dt.getTotal();
}

// FramelessNoMacs --------------------------------------------------------------------
DkNoMacsFrameless::DkNoMacsFrameless(QWidget *parent, Qt::WindowFlags flags)
	: DkNoMacs(parent, flags) {

		setObjectName("DkNoMacsFrameless");
		DkSettings::app.appMode = DkSettings::mode_frameless;
		
		setWindowFlags(Qt::FramelessWindowHint);
		setAttribute(Qt::WA_TranslucentBackground, true);

		// init members
		DkViewPortFrameless* vp = new DkViewPortFrameless(this);
		vp->setAlignment(Qt::AlignHCenter);

		DkCentralWidget* cw = new DkCentralWidget(vp, this);
		setCentralWidget(cw);

		init();
		
		setAcceptDrops(true);
		setMouseTracking (true);	//receive mouse event everytime

		// in frameless, you cannot control if menu is visible...
		mPanelActions[menu_panel_menu]->setEnabled(false);
		mPanelActions[menu_panel_statusbar]->setEnabled(false);
		mPanelActions[menu_panel_statusbar]->setChecked(false);
		mPanelActions[menu_panel_toolbar]->setChecked(false);

		mMenu->setTimeToShow(5000);
		mMenu->hide();
		
		vp->addStartActions(mFileActions[menu_file_open], &mFileIcons[icon_file_open_large]);
		vp->addStartActions(mFileActions[menu_file_open_dir], &mFileIcons[icon_file_dir_large]);

		mViewActions[menu_view_frameless]->blockSignals(true);
		mViewActions[menu_view_frameless]->setChecked(true);
		mViewActions[menu_view_frameless]->blockSignals(false);

		mDesktop = QApplication::desktop();
		updateScreenSize();
		show();
        
        connect(mDesktop, SIGNAL(workAreaResized(int)), this, SLOT(updateScreenSize(int)));

		setObjectName("DkNoMacsFrameless");
		showStatusBar(false);	// fix
}

DkNoMacsFrameless::~DkNoMacsFrameless() {
	release();
}

void DkNoMacsFrameless::release() {
}

void DkNoMacsFrameless::createContextMenu() {

	DkNoMacs::createContextMenu();

	mContextMenu->addSeparator();
	mContextMenu->addAction(mFileActions[menu_file_exit]);
}

void DkNoMacsFrameless::enableNoImageActions(bool enable) {

	DkNoMacs::enableNoImageActions(enable);

	// actions that should always be disabled
	mViewActions[menu_view_fit_frame]->setEnabled(false);

}

void DkNoMacsFrameless::updateScreenSize(int) {

	if (!mDesktop)
		return;

	// for now: set to fullscreen

	int sc = mDesktop->screenCount();
	QRect screenRects = mDesktop->availableGeometry();

	for (int idx = 0; idx < sc; idx++) {

		qDebug() << "screens: " << mDesktop->availableGeometry(idx);
		QRect curScreen = mDesktop->availableGeometry(idx);
		screenRects.setLeft(qMin(screenRects.left(), curScreen.left()));
		screenRects.setTop(qMin(screenRects.top(), curScreen.top()));
		screenRects.setBottom(qMax(screenRects.bottom(), curScreen.bottom()));
		screenRects.setRight(qMax(screenRects.right(), curScreen.right()));
	}

	qDebug() << "set up geometry: " << screenRects;

	this->setGeometry(screenRects);

	DkViewPortFrameless* vp = static_cast<DkViewPortFrameless*>(viewport());
	vp->setMainGeometry(mDesktop->screenGeometry());

}

void DkNoMacsFrameless::exitFullScreen() {

	// TODO: delete this function if we support menu in frameless mode
	if (isFullScreen())
		showNormal();

	if (viewport())
		viewport()->setFullScreen(false);
}


// >DIR diem: eating shortcut overrides
bool DkNoMacsFrameless::eventFilter(QObject* , QEvent* event) {

	if (event->type() == QEvent::ShortcutOverride) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

		// consume esc key if fullscreen is on
		if (keyEvent->key() == Qt::Key_Escape && isFullScreen()) {
			exitFullScreen();
			return true;
		}
		else if (keyEvent->key() == Qt::Key_Escape) {
			close();
			return true;
		}
	}
	if (event->type() == QEvent::Gesture) {
		return gestureEvent(static_cast<QGestureEvent*>(event));
	}

	return false;
}

void DkNoMacsFrameless::closeEvent(QCloseEvent *event) {

	// do not save the window size
	if (mSaveSettings)
		DkSettings::save();

	mSaveSettings = false;

	DkNoMacs::closeEvent(event);
}

// Transfer function:

DkNoMacsContrast::DkNoMacsContrast(QWidget *parent, Qt::WindowFlags flags)
	: DkNoMacsSync(parent, flags) {


		setObjectName("DkNoMacsContrast");

		// init members
		DkViewPortContrast* vp = new DkViewPortContrast(this);
		vp->setAlignment(Qt::AlignHCenter);

		DkCentralWidget* cw = new DkCentralWidget(vp, this);
		setCentralWidget(cw);

		mLocalClient = new DkLocalManagerThread(this);
		mLocalClient->setObjectName("localClient");
		mLocalClient->start();

		mLanClient = 0;
		mRcClient = 0;

		init();

		createTransferToolbar();

		setAcceptDrops(true);
		setMouseTracking (true);	//receive mouse event everytime

		// sync signals
		connect(vp, SIGNAL(newClientConnectedSignal(bool, bool)), this, SLOT(newClientConnected(bool, bool)));
		
		initLanClient();
		emit sendTitleSignal(windowTitle());

		DkSettings::app.appMode = DkSettings::mode_contrast;
		setObjectName("DkNoMacsContrast");

		// show it...
		show();

		// TODO: this should be checked but no event should be called
		mPanelActions[menu_panel_transfertoolbar]->blockSignals(true);
		mPanelActions[menu_panel_transfertoolbar]->setChecked(true);
		mPanelActions[menu_panel_transfertoolbar]->blockSignals(false);

		qDebug() << "mViewport (normal) created...";
}

DkNoMacsContrast::~DkNoMacsContrast() {
	release();
}

void DkNoMacsContrast::release() {
}

void DkNoMacsContrast::createTransferToolbar() {

	mTransferToolBar = new DkTransferToolBar(this);

	// add this toolbar below all previous toolbars
	addToolBarBreak();
	addToolBar(mTransferToolBar);
	mTransferToolBar->setObjectName("TransferToolBar");

	//transferToolBar->layout()->setSizeConstraint(QLayout::SetMinimumSize);
	
	connect(mTransferToolBar, SIGNAL(colorTableChanged(QGradientStops)),  viewport(), SLOT(changeColorTable(QGradientStops)));
	connect(mTransferToolBar, SIGNAL(channelChanged(int)),  viewport(), SLOT(changeChannel(int)));
	connect(mTransferToolBar, SIGNAL(pickColorRequest(bool)),  viewport(), SLOT(pickColor(bool)));
	connect(mTransferToolBar, SIGNAL(tFEnabled(bool)), viewport(), SLOT(enableTF(bool)));
	connect((DkViewPortContrast*)viewport(), SIGNAL(tFSliderAdded(qreal)), mTransferToolBar, SLOT(insertSlider(qreal)));
	connect((DkViewPortContrast*)viewport(), SIGNAL(imageModeSet(int)), mTransferToolBar, SLOT(setImageMode(int)));

	if (DkSettings::display.smallIcons)
		mTransferToolBar->setIconSize(QSize(16, 16));
	else
		mTransferToolBar->setIconSize(QSize(32, 32));


	if (DkSettings::display.toolbarGradient)
		mTransferToolBar->setObjectName("toolBarWithGradient");

}
}
