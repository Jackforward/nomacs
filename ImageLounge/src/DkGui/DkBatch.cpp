/*******************************************************************************************************
 DkNoMacs.cpp
 Created on:	26.10.2014
 
 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances
 
 Copyright (C) 2011-2014 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2014 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2014 Florian Kleber <florian@nomacs.org>

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

#include "DkBatch.h"
#include "DkProcess.h"
#include "DkDialog.h"
#include "DkWidgets.h"
#include "DkThumbsWidgets.h"
#include "DkUtils.h"
#include "DkImageLoader.h"
#include "DkSettings.h"
#include "DkMessageBox.h"
#include "DkPluginManager.h"
#include "DkActionManager.h"
#include "DkImageStorage.h"

#pragma warning(push, 0)	// no warnings from includes - begin
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QFileDialog>
#include <QGroupBox>
#include <QComboBox>
#include <QButtonGroup>
#include <QProgressBar>
#include <QTextEdit>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QMessageBox>
#include <QApplication>
#include <QTextBlock>
#include <QDropEvent>
#include <QMimeData>
#include <QSplitter>
#include <QListWidget>
#include <QAction>
#include <QStackedLayout>
#include <QInputDialog>
#include <QStandardPaths>
#pragma warning(pop)		// no warnings from includes - end

namespace nmc {

// DkBatchTabButton --------------------------------------------------------------------
DkBatchTabButton::DkBatchTabButton(const QString& title, const QString& info, QWidget* parent) : QPushButton(title, parent) {

	// TODO: add info
	mInfo = info;
	setFlat(true);
	setCheckable(true);
}

void DkBatchTabButton::setInfo(const QString& info) {
	mInfo = info;
	update();

	emit infoChanged(mInfo);
}

QString DkBatchTabButton::info() const {
	return mInfo;
}

void DkBatchTabButton::paintEvent(QPaintEvent *event) {

	// fixes stylesheets which are not applied to custom widgets
	QStyleOption opt;
	opt.init(this);
	QPainter p(this);
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

	QFont f;
	f.setPointSize(9);
	f.setItalic(true);
	p.setFont(f);

	// change opacity
	QColor c = p.pen().color();
	c.setAlpha(200);
	QPen fPen = p.pen();
	fPen.setColor(c);
	p.setPen(fPen);

	p.drawText(QPoint(25, 50), mInfo);

	QPushButton::paintEvent(event);
}

// DkBatchContainer --------------------------------------------------------------------
DkBatchContainer::DkBatchContainer(const QString& titleString, const QString& headerString, QWidget* parent) : QObject(parent) {
	
	mHeaderButton = new DkBatchTabButton(titleString, headerString, parent);
	createLayout();
}

void DkBatchContainer::createLayout() {

}

void DkBatchContainer::setContentWidget(QWidget* batchContent) {
	
	mBatchContent = dynamic_cast<DkBatchContent*>(batchContent);

	connect(mHeaderButton, SIGNAL(toggled(bool)), this, SLOT(showContent(bool)));
	connect(batchContent, SIGNAL(newHeaderText(const QString&)), mHeaderButton, SLOT(setInfo(const QString&)));
}

QWidget* DkBatchContainer::contentWidget() const {
	
	return dynamic_cast<QWidget*>(mBatchContent);
}

DkBatchContent * DkBatchContainer::batchContent() const {
	return mBatchContent;
}

DkBatchTabButton* DkBatchContainer::headerWidget() const {

	return mHeaderButton;
}

void DkBatchContainer::showContent(bool show) const {

	if (show)
		emit showSignal();

	//mShowButton->click();
	//contentWidget()->setVisible(show);
}

//void DkBatchContainer::setTitle(const QString& titleString) {
//	mTitleString = titleString;
//	mTitleLabel->setText(titleString);
//}
//
//void DkBatchContainer::setHeader(const QString& headerString) {
//	mHeaderString = headerString;
//	mHeaderLabel->setText(headerString);
//}

// DkInputTextEdit --------------------------------------------------------------------
DkInputTextEdit::DkInputTextEdit(QWidget* parent /* = 0 */) : QTextEdit(parent) {

	setAcceptDrops(true);
	connect(this, SIGNAL(textChanged()), this, SIGNAL(fileListChangedSignal()));
}

void DkInputTextEdit::appendFiles(const QStringList& fileList) {

	QStringList cFileList = getFileList();
	QStringList newFiles;

	// unique!
	for (const QString& cStr : fileList) {

		if (!cFileList.contains(cStr))
			newFiles.append(cStr);
	}

	if (!newFiles.empty()) {
		append(newFiles.join("\n"));
		fileListChangedSignal();
	}
}

void DkInputTextEdit::appendDir(const QString& newDir, bool recursive) {

	if (recursive) {
		qDebug() << "adding recursive...";
		QDir tmpDir = newDir;
		QFileInfoList subDirs = tmpDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);

		for (QFileInfo cDir : subDirs)
			appendDir(cDir.absoluteFilePath(), recursive);
	}

	QDir tmpDir = newDir;
	tmpDir.setSorting(QDir::LocaleAware);
	QFileInfoList fileList = tmpDir.entryInfoList(DkSettingsManager::param().app().fileFilters);
	QStringList strFileList;

	for (QFileInfo entry : fileList) {
		strFileList.append(entry.absoluteFilePath());
	}

	qDebug() << "appending " << strFileList.size() << " files";

	appendFiles(strFileList);
}

void DkInputTextEdit::appendFromMime(const QMimeData* mimeData, bool recursive) {

	if (!mimeData || !mimeData->hasUrls())
		return;

	QStringList cFiles;

	for (QUrl url : mimeData->urls()) {

		QFileInfo cFile = DkUtils::urlToLocalFile(url);

		if (cFile.isDir())
			appendDir(cFile.absoluteFilePath(), recursive);
		else if (cFile.exists() && DkUtils::isValid(cFile))
			cFiles.append(cFile.absoluteFilePath());
	}

	if (!cFiles.empty())
		appendFiles(cFiles);
}

void DkInputTextEdit::insertFromMimeData(const QMimeData* mimeData) {

	appendFromMime(mimeData);
	QTextEdit::insertFromMimeData(mimeData);
}

void DkInputTextEdit::dragEnterEvent(QDragEnterEvent *event) {

	QTextEdit::dragEnterEvent(event);

	if (event->source() == this)
		event->acceptProposedAction();
	else if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void DkInputTextEdit::dragMoveEvent(QDragMoveEvent *event) {

	QTextEdit::dragMoveEvent(event);

	if (event->source() == this)
		event->acceptProposedAction();
	else if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}


void DkInputTextEdit::dropEvent(QDropEvent *event) {
	
	if (event->source() == this) {
		event->accept();
		return;
	}

	appendFromMime(event->mimeData(), (event->keyboardModifiers() & Qt::ControlModifier) != 0);

	// do not propagate!
	//QTextEdit::dropEvent(event);
}

QStringList DkInputTextEdit::getFileList() const {

	QStringList fileList;
	QString textString;
	QTextStream textStream(&textString);
	textStream << toPlainText();

	QString line;
	do
	{
		line = textStream.readLine();	// we don't want to get into troubles with carriage returns of different OS
		if (!line.isNull() && !line.trimmed().isEmpty())
			fileList.append(line);
	} while(!line.isNull());

	return fileList;
}

void DkInputTextEdit::clear() {
	
	mResultList.clear();
	QTextEdit::clear();
}

// File Selection --------------------------------------------------------------------
DkBatchInput::DkBatchInput(QWidget* parent /* = 0 */, Qt::WindowFlags f /* = 0 */) : QWidget(parent, f) {

	setObjectName("DkBatchInput");
	createLayout();
	setMinimumHeight(300);

}

void DkBatchInput::createLayout() {
	
	mDirectoryEdit = new DkDirectoryEdit(this);

	QWidget* upperWidget = new QWidget(this);
	QGridLayout* upperWidgetLayout = new QGridLayout(upperWidget);
	upperWidgetLayout->setContentsMargins(0,0,0,0);
	upperWidgetLayout->addWidget(mDirectoryEdit, 0, 1);

	mInputTextEdit = new DkInputTextEdit(this);

	mResultTextEdit = new QTextEdit(this);
	mResultTextEdit->setReadOnly(true);
	mResultTextEdit->setVisible(false);

	mThumbScrollWidget = new DkThumbScrollWidget(this);
	mThumbScrollWidget->setVisible(true);
	mThumbScrollWidget->getThumbWidget()->setImageLoader(mLoader);

	// add explorer
	mExplorer = new DkExplorer(tr("File Explorer"));
	mExplorer->getModel()->setFilter(QDir::Dirs|QDir::Drives|QDir::NoDotAndDotDot|QDir::AllDirs);
	mExplorer->getModel()->setNameFilters(QStringList());
	mExplorer->setMaximumWidth(300);

	QStringList folders = DkSettingsManager::param().global().recentFiles;

	if (folders.size() > 0)
		mExplorer->setCurrentPath(folders[0]);

	// tab widget
	mInputTabs = new QTabWidget(this);
	mInputTabs->addTab(mThumbScrollWidget,  QIcon(":/nomacs/img/thumbs-view.svg"), tr("Thumbnails"));
	mInputTabs->addTab(mInputTextEdit, QIcon(":/nomacs/img/batch-processing.svg"), tr("File List"));

	QGridLayout* widgetLayout = new QGridLayout(this);
	widgetLayout->setContentsMargins(0, 0, 0, 0);
	widgetLayout->addWidget(mExplorer, 0, 0, 2, 1);
	widgetLayout->addWidget(upperWidget, 0, 1);
	widgetLayout->addWidget(mInputTabs, 1, 1);
	setLayout(widgetLayout);

	connect(mThumbScrollWidget->getThumbWidget(), SIGNAL(selectionChanged()), this, SLOT(selectionChanged()));
	connect(mThumbScrollWidget, SIGNAL(batchProcessFilesSignal(const QStringList&)), mInputTextEdit, SLOT(appendFiles(const QStringList&)));
	connect(mThumbScrollWidget, SIGNAL(updateDirSignal(const QString&)), this, SLOT(setDir(const QString&)));
	connect(mThumbScrollWidget, SIGNAL(filterChangedSignal(const QString &)), mLoader.data(), SLOT(setFolderFilter(const QString&)), Qt::UniqueConnection);
	
	connect(mInputTextEdit, SIGNAL(fileListChangedSignal()), this, SLOT(selectionChanged()));

	connect(mDirectoryEdit, SIGNAL(textChanged(const QString&)), this, SLOT(parameterChanged()));
	connect(mDirectoryEdit, SIGNAL(directoryChanged(const QString&)), this, SLOT(setDir(const QString&)));
	connect(mExplorer, SIGNAL(openDir(const QString&)), this, SLOT(setDir(const QString&)));
	connect(mLoader.data(), SIGNAL(updateDirSignal(QVector<QSharedPointer<DkImageContainerT> >)), mThumbScrollWidget, SLOT(updateThumbs(QVector<QSharedPointer<DkImageContainerT> >)));

}

void DkBatchInput::applyDefault() {
	
	mInputTextEdit->clear();
	selectionChanged();
}

void DkBatchInput::changeTab(int tabIdx) const {

	if (tabIdx < 0 || tabIdx >= mInputTabs->count())
		return;

	mInputTabs->setCurrentIndex(tabIdx);
}

void DkBatchInput::updateDir(QVector<QSharedPointer<DkImageContainerT> > thumbs) {
	emit updateDirSignal(thumbs);
}

void DkBatchInput::setVisible(bool visible) {

	QWidget::setVisible(visible);
	mThumbScrollWidget->getThumbWidget()->updateLayout();
}

void DkBatchInput::browse() {

	// load system default open dialog
	QString dirName = QFileDialog::getExistingDirectory(this, tr("Open an Image Directory"),
		mCDirPath);

	if (dirName.isEmpty())
		return;

	setDir(dirName);
}

QString DkBatchInput::getDir() const {

	return mDirectoryEdit->existsDirectory() ? QDir(mDirectoryEdit->text()).absolutePath() : "";
}

QStringList DkBatchInput::getSelectedFiles() const {
	
	QStringList textList = mInputTextEdit->getFileList();

	if (textList.empty())
		return mThumbScrollWidget->getThumbWidget()->getSelectedFiles();
	else
		return textList;
}

QStringList DkBatchInput::getSelectedFilesBatch() {

	QStringList textList = mInputTextEdit->getFileList();

	if (textList.empty()) {
		textList = mThumbScrollWidget->getThumbWidget()->getSelectedFiles();
		mInputTextEdit->appendFiles(textList);
	}

	return textList;
}


DkInputTextEdit* DkBatchInput::getInputEdit() const {

	return mInputTextEdit;
}

void DkBatchInput::setFileInfo(QFileInfo file) {

	setDir(file.absoluteFilePath());
}

void DkBatchInput::setDir(const QString& dirPath) {

	mExplorer->setCurrentPath(dirPath);

	mCDirPath = dirPath;
	qDebug() << "setting directory to:" << dirPath;
	mDirectoryEdit->setText(mCDirPath);
	emit newHeaderText(mCDirPath);
	emit updateInputDir(mCDirPath);
	mLoader->loadDir(mCDirPath, false);
	mThumbScrollWidget->updateThumbs(mLoader->getImages());
}

void DkBatchInput::selectionChanged() {

	QString msg;
	if (getSelectedFiles().empty())
		msg = tr("No Files Selected");
	else if (getSelectedFiles().size() == 1)
		msg = tr("%1 File Selected").arg(getSelectedFiles().size());
	else
		msg = tr("%1 Files Selected").arg(getSelectedFiles().size());
	
	emit newHeaderText(msg);
	emit changed();
}

void DkBatchInput::parameterChanged() {
	
	QString newDirPath = mDirectoryEdit->text();
		
	qDebug() << "edit text newDir: " << newDirPath << " mCDir " << mCDirPath;

	if (QDir(newDirPath).exists() && newDirPath != mCDirPath) {
		setDir(newDirPath);
		emit changed();
	}
}

void DkBatchInput::setResults(const QStringList& results) {

	if (mInputTabs->count() < 3) {
		mInputTabs->addTab(mResultTextEdit, tr("Results"));
	}

	mResultTextEdit->clear();
	mResultTextEdit->setHtml(results.join("<br> "));
	QTextCursor c = mResultTextEdit->textCursor();
	c.movePosition(QTextCursor::End);
	mResultTextEdit->setTextCursor(c);
	mResultTextEdit->setVisible(true);
}

void DkBatchInput::startProcessing() {

	if (mInputTabs->count() < 3) {
		mInputTabs->addTab(mResultTextEdit, tr("Results"));
	}

	changeTab(tab_results);
	mInputTextEdit->setEnabled(false);
	mResultTextEdit->clear();
}

void DkBatchInput::stopProcessing() {

	//mInputTextEdit->clear();
	mInputTextEdit->setEnabled(true);
}

// DkFileNameWdiget --------------------------------------------------------------------
DkFilenameWidget::DkFilenameWidget(QWidget* parent) : QWidget(parent) {

	createLayout();
	showOnlyFilename();
}

void DkFilenameWidget::createLayout() {
	
	mLayout = new QGridLayout(this);
	mLayout->setContentsMargins(0,0,0,5);
	setMaximumWidth(500);

	mCbType = new QComboBox(this);
	mCbType->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	mCbType->insertItem(fileNameTypes_fileName, tr("Current Filename"));
	mCbType->insertItem(fileNameTypes_Text, tr("Text"));
	mCbType->insertItem(fileNameTypes_Number, tr("Number"));
	connect(mCbType, SIGNAL(currentIndexChanged(int)), this, SLOT(typeCBChanged(int)));
	connect(mCbType, SIGNAL(currentIndexChanged(int)), this, SLOT(checkForUserInput()));
	connect(mCbType, SIGNAL(currentIndexChanged(int)), this, SIGNAL(changed()));

	mCbCase = new QComboBox(this);
	mCbCase->addItem(tr("Keep Case"));
	mCbCase->addItem(tr("To lowercase"));
	mCbCase->addItem(tr("To UPPERCASE"));
	connect(mCbCase, SIGNAL(currentIndexChanged(int)), this, SLOT(checkForUserInput()));
	connect(mCbCase, SIGNAL(currentIndexChanged(int)), this, SIGNAL(changed()));

	mSbNumber = new QSpinBox(this);
	mSbNumber->setValue(1);
	mSbNumber->setMinimum(0);
	mSbNumber->setMaximum(999);	// changes - if cbDigits->setCurrentIndex() is changed!
	connect(mSbNumber, SIGNAL(valueChanged(int)), this, SIGNAL(changed()));

	mCbDigits = new QComboBox(this);
	mCbDigits->addItem(tr("1 digit"));
	mCbDigits->addItem(tr("2 digits"));
	mCbDigits->addItem(tr("3 digits"));
	mCbDigits->addItem(tr("4 digits"));
	mCbDigits->addItem(tr("5 digits"));
	mCbDigits->setCurrentIndex(2);	// see sBNumber->setMaximum()
	connect(mCbDigits, SIGNAL(currentIndexChanged(int)), this, SLOT(digitCBChanged(int)));

	mLeText = new QLineEdit(this);
	connect(mCbCase, SIGNAL(currentIndexChanged(int)), this, SIGNAL(changed()));
	connect(mLeText, SIGNAL(textChanged(const QString&)), this, SIGNAL(changed()));

	mPbPlus = new QPushButton("+", this);
	mPbPlus->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	mPbPlus->setMinimumSize(10,10);
	mPbPlus->setMaximumSize(30,30);
	mPbMinus = new QPushButton("-", this);
	mPbMinus->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	mPbMinus->setMinimumSize(10,10);
	mPbMinus->setMaximumSize(30,30);
	connect(mPbPlus, SIGNAL(clicked()), this, SLOT(pbPlusPressed()));
	connect(mPbMinus, SIGNAL(clicked()), this, SLOT(pbMinusPressed()));
	connect(mPbPlus, SIGNAL(clicked()), this, SIGNAL(changed()));
	connect(mPbMinus, SIGNAL(clicked()), this, SIGNAL(changed()));
}

void DkFilenameWidget::typeCBChanged(int index) {
	switch (index) {
		case fileNameTypes_fileName: {showOnlyFilename(); break;};
		case fileNameTypes_Text: {showOnlyText(); break;};
		case fileNameTypes_Number: {showOnlyNumber(); break;};
		default:
			break;
	}
}

void DkFilenameWidget::showOnlyFilename() {
	mCbCase->show();

	mSbNumber->hide();
	mCbDigits->hide();
	mLeText->hide();

	mLayout->addWidget(mCbType, 0, fileNameWidget_type);
	mLayout->addWidget(mCbCase, 0, fileNameWidget_input1);
	//curLayout->addWidget(new QWidget(this), 0, fileNameWidget_input2 );
	mLayout->addWidget(mPbPlus, 0, fileNameWidget_plus);
	mLayout->addWidget(mPbMinus, 0, fileNameWidget_minus);

}

void DkFilenameWidget::showOnlyNumber() {
	mSbNumber->show();
	mCbDigits->show();

	mCbCase->hide();
	mLeText->hide();

	mLayout->addWidget(mCbType, 0, fileNameWidget_type);
	mLayout->addWidget(mSbNumber, 0, fileNameWidget_input1);
	mLayout->addWidget(mCbDigits, 0, fileNameWidget_input2);
	mLayout->addWidget(mPbPlus, 0, fileNameWidget_plus);
	mLayout->addWidget(mPbMinus, 0, fileNameWidget_minus);
}

void DkFilenameWidget::showOnlyText() {
	mLeText->show();

	mSbNumber->hide();
	mCbDigits->hide();
	mCbCase->hide();
	

	mLayout->addWidget(mCbType, 0, fileNameWidget_type);
	mLayout->addWidget(mLeText, 0, fileNameWidget_input1);
	//curLayout->addWidget(new QWidget(this), 0, fileNameWidget_input2);
	mLayout->addWidget(mPbPlus, 0, fileNameWidget_plus);
	mLayout->addWidget(mPbMinus, 0, fileNameWidget_minus);
}

void DkFilenameWidget::pbPlusPressed() {
	emit plusPressed(this);
}

void DkFilenameWidget::pbMinusPressed() {
	emit minusPressed(this);
}

void DkFilenameWidget::enableMinusButton(bool enable) {
	mPbMinus->setEnabled(enable);
}

void DkFilenameWidget::enablePlusButton(bool enable) {
	mPbPlus->setEnabled(enable);
}

void DkFilenameWidget::checkForUserInput() {
	if(mCbType->currentIndex() == 0 && mCbCase->currentIndex() == 0)
		hasChanged = false;
	else
		hasChanged = true;
	//emit changed();
}

void DkFilenameWidget::digitCBChanged(int index) {
	mSbNumber->setMaximum(qRound(pow(10, index+1)-1));
	emit changed();
}

QString DkFilenameWidget::getTag() const {

	QString tag;

	switch (mCbType->currentIndex()) {
		
	case fileNameTypes_Number: 
		{
			tag += "<d:"; 
			tag += QString::number(mCbDigits->currentIndex());	// is sensitive to the index
			tag += ":" + QString::number(mSbNumber->value());
			tag += ">";
			break;
		}
	case fileNameTypes_fileName: 
		{
			tag += "<c:"; 
			tag += QString::number(mCbCase->currentIndex());	// is sensitive to the index
			tag += ">";
			break;
		}
	case fileNameTypes_Text:
		{
			tag += mLeText->text();
		}
	}

	return tag;
}

bool DkFilenameWidget::setTag(const QString & tag) {

	QString cTag = tag;
	QStringList cmds = cTag.split(":");

	if (cmds.size() == 1) {
		mCbType->setCurrentIndex(fileNameTypes_Text);
		mLeText->setText(tag);
	}
	else {

		if (cmds[0] == "c") {
			mCbType->setCurrentIndex(fileNameTypes_fileName);
			mCbCase->setCurrentIndex(cmds[1].toInt());
		}
		else if (cmds[0] == "d") {
			mCbType->setCurrentIndex(fileNameTypes_Number);
			mCbDigits->setCurrentIndex(cmds[1].toInt());
			mSbNumber->setValue(cmds[2].toInt());
		}
		else {
			qWarning() << "cannot parse" << cmds;
			return false;
		}
	}

	return true;
}

// DkBatchOutput --------------------------------------------------------------------
DkBatchOutput::DkBatchOutput(QWidget* parent , Qt::WindowFlags f ) : QWidget(parent, f) {

	setObjectName("DkBatchOutput");
	createLayout();
}

void DkBatchOutput::createLayout() {

	// Output Directory Groupbox
	QLabel* outDirLabel = new QLabel(tr("Output Directory"), this);
	outDirLabel->setObjectName("subTitle");

	mOutputBrowseButton = new QPushButton(tr("Browse"));
	mOutputlineEdit = new DkDirectoryEdit(this);
	mOutputlineEdit->setPlaceholderText(tr("Select a Directory"));
	connect(mOutputBrowseButton , SIGNAL(clicked()), this, SLOT(browse()));
	connect(mOutputlineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(setDir(const QString&)));

	// overwrite existing
	mCbOverwriteExisting = new QCheckBox(tr("Overwrite Existing Files"));
	mCbOverwriteExisting->setToolTip(tr("If checked, existing files are overwritten.\nThis option might destroy your images - so be careful!"));
	connect(mCbOverwriteExisting, SIGNAL(clicked()), this, SIGNAL(changed()));

	// overwrite existing
	mCbDoNotSave = new QCheckBox(tr("Do not Save Output Images"));
	mCbDoNotSave->setToolTip(tr("If checked, output images are not saved at all.\nThis option is only usefull if plugins save sidecar files - so be careful!"));
	connect(mCbDoNotSave, SIGNAL(clicked()), this, SIGNAL(changed()));

	// Use Input Folder
	mCbUseInput = new QCheckBox(tr("Use Input Folder"));
	mCbUseInput->setToolTip(tr("If checked, the batch is applied to the input folder - so be careful!"));
	connect(mCbUseInput, SIGNAL(clicked(bool)), this, SLOT(useInputFolderChanged(bool)));

	// delete original
	mCbDeleteOriginal = new QCheckBox(tr("Delete Input Files"));
	mCbDeleteOriginal->setToolTip(tr("If checked, the original file will be deleted if the conversion was successful.\n So be careful!"));

	QWidget* cbWidget = new QWidget(this);
	QVBoxLayout* cbLayout = new QVBoxLayout(cbWidget);
	cbLayout->setContentsMargins(0,0,0,0);
	cbLayout->addWidget(mCbUseInput);
	cbLayout->addWidget(mCbOverwriteExisting);
	cbLayout->addWidget(mCbDoNotSave);
	cbLayout->addWidget(mCbDeleteOriginal);

	QWidget* outDirWidget = new QWidget(this);
	QGridLayout* outDirLayout = new QGridLayout(outDirWidget);
	//outDirLayout->setContentsMargins(0, 0, 0, 0);
	outDirLayout->addWidget(mOutputBrowseButton, 0, 0);
	outDirLayout->addWidget(mOutputlineEdit, 0, 1);
	outDirLayout->addWidget(cbWidget, 1, 0);

	// Filename Groupbox
	QLabel* fileNameLabel = new QLabel(tr("Filename"), this);
	fileNameLabel->setObjectName("subTitle");

	QWidget* fileNameWidget = new QWidget(this);
	mFilenameVBLayout = new QVBoxLayout(fileNameWidget);
	mFilenameVBLayout->setSpacing(0);
	
	DkFilenameWidget* fwidget = new DkFilenameWidget(this);
	fwidget->enableMinusButton(false);
	mFilenameWidgets.push_back(fwidget);
	mFilenameVBLayout->addWidget(fwidget);
	connect(fwidget, SIGNAL(plusPressed(DkFilenameWidget*)), this, SLOT(plusPressed(DkFilenameWidget*)));
	connect(fwidget, SIGNAL(minusPressed(DkFilenameWidget*)), this, SLOT(minusPressed(DkFilenameWidget*)));
	connect(fwidget, SIGNAL(changed()), this, SLOT(parameterChanged()));

	QWidget* extensionWidget = new QWidget(this);
	QHBoxLayout* extensionLayout = new QHBoxLayout(extensionWidget);
	extensionLayout->setAlignment(Qt::AlignLeft);
	//extensionLayout->setSpacing(0);
	extensionLayout->setContentsMargins(0,0,0,0);
	mCbExtension = new QComboBox(this);
	mCbExtension->addItem(tr("Keep Extension"));
	mCbExtension->addItem(tr("Convert To"));
	connect(mCbExtension, SIGNAL(currentIndexChanged(int)), this, SLOT(extensionCBChanged(int)));

	mCbNewExtension = new QComboBox(this);
	mCbNewExtension->addItems(DkSettingsManager::param().app().saveFilters);
	mCbNewExtension->setFixedWidth(150);
	mCbNewExtension->setEnabled(false);
	connect(mCbNewExtension, SIGNAL(currentIndexChanged(int)), this, SLOT(parameterChanged()));

	QLabel* compressionLabel = new QLabel(tr("Quality"), this);

	mSbCompression = new QSpinBox(this);
	mSbCompression->setMinimum(1);
	mSbCompression->setMaximum(100);
	mSbCompression->setEnabled(false);

	extensionLayout->addWidget(mCbExtension);
	extensionLayout->addWidget(mCbNewExtension);
	extensionLayout->addWidget(compressionLabel);
	extensionLayout->addWidget(mSbCompression);
	//extensionLayout->addStretch();
	mFilenameVBLayout->addWidget(extensionWidget);
	
	QLabel* previewLabel = new QLabel(tr("Preview"), this);
	previewLabel->setObjectName("subTitle");

	QLabel* oldLabel = new QLabel(tr("Old Filename: "));
	oldLabel->setObjectName("FileNamePreviewLabel");
	mOldFileNameLabel = new QLabel("");
	mOldFileNameLabel->setObjectName("FileNamePreviewLabel");

	QLabel* newLabel = new QLabel(tr("New Filename: "));
	newLabel->setObjectName("FileNamePreviewLabel");
	mNewFileNameLabel = new QLabel("");
	mNewFileNameLabel->setObjectName("FileNamePreviewLabel");

	// Preview Widget
	QWidget* previewWidget = new QWidget(this);
	QGridLayout* previewGBLayout = new QGridLayout(previewWidget);
	//previewWidget->hide();	// show if more than 1 file is selected
	previewGBLayout->addWidget(oldLabel, 0, 0);
	previewGBLayout->addWidget(mOldFileNameLabel, 0, 1);
	previewGBLayout->addWidget(newLabel, 1, 0);
	previewGBLayout->addWidget(mNewFileNameLabel, 1, 1);
	previewGBLayout->setColumnStretch(3, 10);
	previewGBLayout->setAlignment(Qt::AlignTop);
	
	QGridLayout* contentLayout = new QGridLayout(this);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	contentLayout->addWidget(outDirLabel, 2, 0);
	contentLayout->addWidget(outDirWidget, 3, 0);
	contentLayout->addWidget(fileNameLabel, 4, 0);
	contentLayout->addWidget(fileNameWidget, 5, 0);
	contentLayout->addWidget(previewLabel, 6, 0);
	contentLayout->addWidget(previewWidget, 7, 0);
	setLayout(contentLayout);
}

void DkBatchOutput::browse() {

	QString dirGuess = (mOutputlineEdit->text().isEmpty()) ? mInputDirectory : mOutputlineEdit->text();
	
	// load system default open dialog
	QString dirName = QFileDialog::getExistingDirectory(this, tr("Open an Image Directory"),
		dirGuess);

	if (dirName.isEmpty())
		return;

	setDir(dirName);
}

void DkBatchOutput::setDir(const QString& dirPath, bool updateLineEdit) {

	mOutputDirectory = dirPath;
	emit newHeaderText(dirPath);
	
	if (updateLineEdit)
		mOutputlineEdit->setText(dirPath);
}

void DkBatchOutput::setInputDir(const QString& dirPath) {
	mInputDirectory = dirPath;

	if (mCbUseInput->isChecked())
		setDir(mInputDirectory);
}

void DkBatchOutput::useInputFolderChanged(bool checked) {

	mOutputlineEdit->setEnabled(!checked);
	mOutputBrowseButton->setEnabled(!checked);

	if (checked)
		setDir(mInputDirectory);
}

void DkBatchOutput::plusPressed(DkFilenameWidget* widget, const QString& tag) {

	DkFilenameWidget* fw = createFilenameWidget(tag);

	int index = mFilenameVBLayout->indexOf(widget);
	mFilenameWidgets.insert(index + 1, fw);
	if (mFilenameWidgets.size() > 4) {
		for (int i = 0; i  < mFilenameWidgets.size(); i++)
			mFilenameWidgets[i]->enablePlusButton(false);
	}
	mFilenameVBLayout->insertWidget(index + 1, fw); // append to current widget

	parameterChanged();
}

void DkBatchOutput::addFilenameWidget(const QString& tag) {

	DkFilenameWidget* fw = createFilenameWidget(tag);
	mFilenameWidgets.append(fw);
	mFilenameVBLayout->insertWidget(mFilenameWidgets.size()-1, fw); // append to current widget
}

DkFilenameWidget * DkBatchOutput::createFilenameWidget(const QString & tag) {

	DkFilenameWidget* fw = new DkFilenameWidget(this);
	fw->setTag(tag);

	connect(fw, SIGNAL(plusPressed(DkFilenameWidget*)), this, SLOT(plusPressed(DkFilenameWidget*)));
	connect(fw, SIGNAL(minusPressed(DkFilenameWidget*)), this, SLOT(minusPressed(DkFilenameWidget*)));
	connect(fw, SIGNAL(changed()), this, SLOT(parameterChanged()));

	return fw;
}

void DkBatchOutput::minusPressed(DkFilenameWidget* widget) {
	
	mFilenameVBLayout->removeWidget(widget);
	mFilenameWidgets.remove(mFilenameWidgets.indexOf(widget));
	if (mFilenameWidgets.size() <= 4) {
		for (int i = 0; i  < mFilenameWidgets.size(); i++)
			mFilenameWidgets[i]->enablePlusButton(true);
	}

	widget->hide();

	parameterChanged();
}

void DkBatchOutput::extensionCBChanged(int index) {
	
	mCbNewExtension->setEnabled(index > 0);
	mSbCompression->setEnabled(index > 0);
	parameterChanged();
}


bool DkBatchOutput::hasUserInput() const {
	// TODO add output directory 
	return mFilenameWidgets.size() > 1 || mFilenameWidgets[0]->hasUserInput() || mCbExtension->currentIndex() == 1;
}

void DkBatchOutput::parameterChanged() {

	updateFileLabelPreview();
	emit changed();
}

void DkBatchOutput::updateFileLabelPreview() {

	qDebug() << "updating file label, example name: " << mExampleName;

	if (mExampleName.isEmpty())
		return;

	DkFileNameConverter converter(mExampleName, getFilePattern(), 0);

	mOldFileNameLabel->setText(mExampleName);
	mNewFileNameLabel->setText(converter.getConvertedFileName());
}

QString DkBatchOutput::getOutputDirectory() {
	qDebug() << "ouptut dir: " << QDir(mOutputlineEdit->text()).absolutePath();

	return mOutputlineEdit->text();
}

QString DkBatchOutput::getFilePattern() {

	QString pattern = "";

	for (int idx = 0; idx < mFilenameWidgets.size(); idx++)
		pattern += mFilenameWidgets.at(idx)->getTag();	

	if (mCbExtension->currentIndex() == 0) {
		pattern += ".<old>";
	}
	else {
		QString ext = mCbNewExtension->itemText(mCbNewExtension->currentIndex());

		QStringList tmp = ext.split("(");

		if (tmp.size() == 2) {

			QString filters = tmp.at(1);
			filters.replace(")", "");
			filters.replace("*", "");

			QStringList extList = filters.split(" ");

			if (!extList.empty())
				pattern += extList[0];
		}
	}

	return pattern;
}

void DkBatchOutput::loadFilePattern(const QString & pattern) {

	QStringList nameList = pattern.split(".");
	QString ext = nameList.last();

	QString p = pattern;
	p = p.replace("." + ext, "");		// remove extension
	p = p.replace(">", "<");
		
	QStringList cmdsRaw = p.split("<");
	QStringList cmds;

	for (const QString& c : cmdsRaw) {
		if (!c.trimmed().isEmpty())
			cmds << c;
	}

	// uff - first is special
	if (!cmds.empty() && !mFilenameWidgets.empty()) {
		mFilenameWidgets.first()->setTag(cmds.first());
		cmds.pop_front();
	}

	for (const QString& c : cmds) {

		if (c.isEmpty())
			continue;
		
		qDebug() << "processing: " << c;
		addFilenameWidget(c);
	}

	if (ext != "<old>") {
		mCbExtension->setCurrentIndex(1);
		int idx = mCbNewExtension->findText(ext, Qt::MatchContains);
		mCbNewExtension->setCurrentIndex(idx);
	}
	else {
		mCbExtension->setCurrentIndex(0);
	}
}

int DkBatchOutput::getCompression() const {

	if (!mSbCompression->isEnabled())
		return -1;

	return mSbCompression->value();
}

void DkBatchOutput::applyDefault() {

	mCbUseInput->setChecked(false);
	mCbDeleteOriginal->setChecked(false);
	mCbOverwriteExisting->setChecked(false);
	mCbDoNotSave->setChecked(false);
	mCbExtension->setCurrentIndex(0);
	mCbNewExtension->setCurrentIndex(0);
	mSbCompression->setValue(90);
	mOutputDirectory = "";
	mInputDirectory = "";
	mHUserInput = false;
	mRUserInput = false;

	// remove all but the first
	for (int idx = mFilenameWidgets.size()-1; idx > 0; idx--) {
		mFilenameWidgets[idx]->deleteLater();
		mFilenameWidgets.pop_back();
	}

	if (!mFilenameWidgets.empty()) {
		mFilenameWidgets[0]->setTag("c:0");	// current filename
	}
	else
		qWarning() << "no filename widgets...";

	mOutputlineEdit->setText(mOutputDirectory);
}

void DkBatchOutput::loadProperties(const DkBatchConfig & config) {

	DkSaveInfo si = config.saveInfo();
	mCbOverwriteExisting->setChecked(si.mode() == DkSaveInfo::mode_overwrite);
	mCbDoNotSave->setChecked(si.mode() == DkSaveInfo::mode_do_not_save_output);
	mCbDeleteOriginal->setChecked(si.isDeleteOriginal());
	mCbUseInput->setChecked(si.isInputDirOutputDir());
	mOutputlineEdit->setText(config.getOutputDirPath());
	mSbCompression->setValue(si.compression());

	loadFilePattern(config.getFileNamePattern());

	parameterChanged();
}

DkSaveInfo::OverwriteMode DkBatchOutput::overwriteMode() const {

	if (mCbOverwriteExisting->isChecked())
		return DkSaveInfo::mode_overwrite;
	else if (mCbDoNotSave->isChecked())
		return DkSaveInfo::mode_do_not_save_output;

	return DkSaveInfo::mode_skip_existing;
}

bool DkBatchOutput::useInputDir() const {
	return mCbUseInput->isChecked();
}

bool DkBatchOutput::deleteOriginal() const {

	return mCbDeleteOriginal->isChecked();
}

void DkBatchOutput::setExampleFilename(const QString& exampleName) {

	mExampleName = exampleName;
	qDebug() << "example name: " << exampleName;
	updateFileLabelPreview();
}

// DkResizeWidget --------------------------------------------------------------------
DkBatchResizeWidget::DkBatchResizeWidget(QWidget* parent /* = 0 */, Qt::WindowFlags f /* = 0 */) : QWidget(parent, f) {

	createLayout();
	applyDefault();
}

void DkBatchResizeWidget::createLayout() {

	mComboMode = new QComboBox(this);
	QStringList modeItems;
	modeItems << tr("Percent") << tr("Long Side") << tr("Short Side") << tr("Width") << tr("Height");
	mComboMode->addItems(modeItems);

	mComboProperties = new QComboBox(this);
	QStringList propertyItems;
	propertyItems << tr("Transform All") << tr("Shrink Only") << tr("Enlarge Only");
	mComboProperties->addItems(propertyItems);

	mSbPercent = new QDoubleSpinBox(this);
	mSbPercent->setSuffix(tr("%"));
	mSbPercent->setMaximum(1000);
	mSbPercent->setMinimum(0.1);

	mSbPx = new QSpinBox(this);
	mSbPx->setSuffix(tr(" px"));
	mSbPx->setMaximum(SHRT_MAX);
	mSbPx->setMinimum(1);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	layout->addWidget(mComboMode);
	layout->addWidget(mSbPercent);
	layout->addWidget(mSbPx);
	layout->addWidget(mComboProperties);
	layout->addStretch();

	connect(mComboMode, SIGNAL(currentIndexChanged(int)), this, SLOT(modeChanged(int)));
	connect(mSbPercent, SIGNAL(valueChanged(double)), this, SLOT(percentChanged(double)));
	connect(mSbPx, SIGNAL(valueChanged(int)), this, SLOT(pxChanged(int)));
}

void DkBatchResizeWidget::modeChanged(int) {

	if (mComboMode->currentIndex() == DkResizeBatch::mode_default) {
		mSbPx->hide();
		mSbPercent->show();
		mComboProperties->hide();
		percentChanged(mSbPercent->value());
	}
	else {
		mSbPx->show();
		mSbPercent->hide();
		mComboProperties->show();
		pxChanged(mSbPx->value());
	}
}

void DkBatchResizeWidget::percentChanged(double val) {

	if (val == 100.0)
		emit newHeaderText(tr("inactive"));
	else
		emit newHeaderText(QString::number(val) + "%");
}

void DkBatchResizeWidget::pxChanged(int val) {

	emit newHeaderText(mComboMode->itemText(mComboMode->currentIndex()) + ": " + QString::number(val) + " px");
}

void DkBatchResizeWidget::applyDefault() {

	mSbPercent->setValue(100.0);
	mSbPx->setValue(1920);
	mComboMode->setCurrentIndex(0);
	mComboProperties->setCurrentIndex(0);
	modeChanged(0);	// init gui
}

void DkBatchResizeWidget::transferProperties(QSharedPointer<DkResizeBatch> batchResize) const {

	if (mComboMode->currentIndex() == DkResizeBatch::mode_default) {
		batchResize->setProperties((float)mSbPercent->value()/100.0f, mComboMode->currentIndex());
	}
	else {
		batchResize->setProperties((float)mSbPx->value(), mComboMode->currentIndex(), mComboProperties->currentIndex());
	}
}

bool DkBatchResizeWidget::loadProperties(QSharedPointer<DkResizeBatch> batchResize) const {

	if (!batchResize) {
		qWarning() << "cannot load properties, DkResizeBatch is NULL";
		return false;
	}

	mComboMode->setCurrentIndex(batchResize->mode());
	mComboProperties->setCurrentIndex(batchResize->property());
	
	float sf = batchResize->scaleFactor();
	if (batchResize->mode() == DkResizeBatch::mode_default)
		mSbPercent->setValue(sf*100.0f);
	else
		mSbPx->setValue(qRound(sf));

	return false;
}

bool DkBatchResizeWidget::hasUserInput() const {

	return !(mComboMode->currentIndex() == DkResizeBatch::mode_default && mSbPercent->value() == 100.0);
}

bool DkBatchResizeWidget::requiresUserInput() const {

	return false;
}

// DkProfileWidget --------------------------------------------------------------------
DkProfileWidget::DkProfileWidget(QWidget* parent, Qt::WindowFlags f) : QWidget(parent, f) {

	createLayout();
	QMetaObject::connectSlotsByName(this);
}

void DkProfileWidget::createLayout() {

	mProfileCombo = new QComboBox(this);
	mProfileCombo->setObjectName("profileCombo");

	QPushButton* saveButton = new QPushButton(tr("Save Profile"), this);
	saveButton->setObjectName("saveButton");

	QPushButton* exportButton = new QPushButton(tr("Export Profile"), this);
	exportButton->setObjectName("exportButton");

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	layout->addWidget(mProfileCombo);
	layout->addWidget(saveButton);
	layout->addWidget(exportButton);

	updateProfileCombo();
}


bool DkProfileWidget::hasUserInput() const {
	return false;
}

bool DkProfileWidget::requiresUserInput() const {
	return false;
}

void DkProfileWidget::applyDefault() {
	// nothing todo here
}

void DkProfileWidget::profileSaved(const QString& profileName) {

	updateProfileCombo();

	int idx = mProfileCombo->findText(profileName);
	qDebug() << "profile index: " << idx;

	if (idx >= 0)
		mProfileCombo->setCurrentIndex(idx);
}

void DkProfileWidget::on_profileCombo_currentIndexChanged(const QString& text) {

	// first is the 'no profile'
	if (text == mProfileCombo->itemText(0)) {
		emit applyDefaultSignal();
	}
	else {
		QString profilePath = DkBatchProfile::profileNameToPath(text);
		emit loadProfileSignal(profilePath);
	}
	
	emit newHeaderText(text);
}

void DkProfileWidget::updateProfileCombo() {

	mProfileCombo->clear();

	DkBatchProfile bp;
	QStringList pn = bp.profileNames();

	mProfileCombo->addItem(tr("<no profile selected>"));

	for (const QString& p : pn) {
		mProfileCombo->addItem(p);
	}
}

void DkProfileWidget::on_saveButton_clicked() {

	saveProfile();
}

void DkProfileWidget::on_exportButton_clicked() {

	QString sPath = QFileDialog::getSaveFileName(this, 
		tr("Export Batch Profile"), 
		QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
		tr("nomacs Batch Profile (*.%1)").arg(DkBatchProfile::extension()));

	emit saveProfileSignal(sPath);
}

void DkProfileWidget::saveProfile() {

	// default mode is overwrite (UI is asking anyway)
	QString cn = mProfileCombo->currentText();
	QString dName = cn.isEmpty() || cn == mProfileCombo->itemText(0) ? "Profile 1" : mProfileCombo->currentText();

	bool ok;
	QString text = QInputDialog::getText(this, tr("Profile Name"),
		tr("Profile Name:"), QLineEdit::Normal,
		dName, &ok);

	if (!ok || text.isEmpty())
		return;	// user canceled

				// is the profile name unique?
	if (mProfileCombo->findText(text) != -1) {

		QMessageBox::StandardButton button = QMessageBox::information(
			this, 
			tr("Profile Already Exists"), 
			tr("Do you want to overwrite %1?").arg(text), 
			QMessageBox::Yes | QMessageBox::No);

		if (button == QMessageBox::No) {
			saveProfile(); // start over
			return;
		}
	}
	
	emit saveProfileSignal(DkBatchProfile::profileNameToPath(text));
}

#ifdef WITH_PLUGINS
// DkBatchPlugin --------------------------------------------------------------------
DkBatchPluginWidget::DkBatchPluginWidget(QWidget* parent /* = 0 */, Qt::WindowFlags f /* = 0 */) : QWidget(parent, f) {

	DkPluginManager::instance().loadPlugins();
	createLayout();
}

void DkBatchPluginWidget::transferProperties(QSharedPointer<DkPluginBatch> batchPlugin) const {

	QStringList pluginList;
	for (int idx = 0; idx < mSelectedPluginList->count(); idx++) {
		pluginList.append(mSelectedPluginList->item(idx)->text());
	}

	batchPlugin->setProperties(pluginList);
}

void DkBatchPluginWidget::createLayout() {

	QLabel* loadedLabel = new QLabel("All Plugins");
	loadedLabel->setObjectName("subTitle");

	mLoadedPluginList = new DkListWidget(this);
	mLoadedPluginList->setEmptyText(tr("Sorry, no Plugins found."));
	mLoadedPluginList->addItems(getPluginActionNames());

	QLabel* selectedLabel = new QLabel("Selected Plugins");
	selectedLabel->setObjectName("subTitle");

	mSelectedPluginList = new DkListWidget(this);
	mSelectedPluginList->setEmptyText(tr("Drag Plugin Actions here."));

	QGridLayout* layout = new QGridLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(loadedLabel, 0, 0);
	layout->addWidget(mLoadedPluginList, 1, 0);
	layout->addWidget(selectedLabel, 0, 1);
	layout->addWidget(mSelectedPluginList, 1, 1);

	// connections
	connect(mLoadedPluginList, SIGNAL(dataDroppedSignal()), this, SLOT(updateHeader()));
	connect(mSelectedPluginList, SIGNAL(dataDroppedSignal()), this, SLOT(updateHeader()));
}

bool DkBatchPluginWidget::loadProperties(QSharedPointer<DkPluginBatch> batchPlugin) {

	if (!batchPlugin) {
		qWarning() << "cannot load properties, DkPluginBatch is NULL";
		return false;
	}

	QStringList appliedPlugins = batchPlugin->pluginList();
	QStringList loadedPlugins = getPluginActionNames();
	bool errored = false;

	for (const QString& plugin : appliedPlugins) {
		if (loadedPlugins.contains(plugin)) {
			selectPlugin(plugin);
		}
		else {
			errored = true;
			qWarning() << "I could not find" << plugin;
		}
	}

	return !errored;
}

void DkBatchPluginWidget::selectPlugin(const QString& actionName, bool select) {

	if (select) {
		mSelectedPluginList->addItem(actionName);
		auto items = mLoadedPluginList->findItems(actionName, Qt::MatchExactly);
		for (auto i : items)
			delete i;
	}
	else {
		mLoadedPluginList->addItem(actionName);
		auto items = mSelectedPluginList->findItems(actionName, Qt::MatchExactly);
		for (auto i : items)
			delete i;
	}

	updateHeader();
}

bool DkBatchPluginWidget::hasUserInput() const {
	return !mSelectedPluginList->isEmpty();
}

bool DkBatchPluginWidget::requiresUserInput() const {
	return false;
}

void DkBatchPluginWidget::applyDefault() {

	QStringList selectedPlugins;
	
	for (int idx = 0; idx < mSelectedPluginList->count(); idx++) {
		selectPlugin(mSelectedPluginList->item(idx)->text(), false);
	}
}

QStringList DkBatchPluginWidget::getPluginActionNames() const {

	QStringList pluginActions;
	QVector<QSharedPointer<DkPluginContainer> > plugins = DkPluginManager::instance().getBatchPlugins();

	for (auto p : plugins) {

		QList<QAction*> actions = p->plugin()->pluginActions();

		for (const QAction* a : actions) {
			pluginActions.append(p->pluginName() + " | " + a->text());
		}
	}

	return pluginActions;
}

void DkBatchPluginWidget::updateHeader() const {
	
	int c = mSelectedPluginList->count();
	if (!c)
		emit newHeaderText(tr("inactive"));
	else
		emit newHeaderText(tr("%1 plugins selected").arg(c));

	// TODO: counting is wrong! (if you remove plugins
}
#endif

// DkBatchTransform --------------------------------------------------------------------
DkBatchTransformWidget::DkBatchTransformWidget(QWidget* parent /* = 0 */, Qt::WindowFlags f /* = 0 */) : QWidget(parent, f) {

	createLayout();
	applyDefault();
}

void DkBatchTransformWidget::createLayout() {

	QLabel* rotateLabel = new QLabel(tr("Orientation"), this);
	rotateLabel->setObjectName("subTitle");

	mRbRotate0 = new QRadioButton(tr("Do &Not Rotate"));
	mRbRotate0->setChecked(true);
	mRbRotateLeft = new QRadioButton(tr("90%1 Counter Clockwise").arg(dk_degree_str));
	mRbRotateRight = new QRadioButton(tr("90%1 Clockwise").arg(dk_degree_str));
	mRbRotate180 = new QRadioButton(tr("180%1").arg(dk_degree_str));

	mRotateGroup = new QButtonGroup(this);

	mRotateGroup->addButton(mRbRotate0);
	mRotateGroup->addButton(mRbRotateLeft);
	mRotateGroup->addButton(mRbRotateRight);
	mRotateGroup->addButton(mRbRotate180);

	QLabel* transformLabel = new QLabel(tr("Transformations"), this);
	transformLabel->setObjectName("subTitle");

	mCbFlipH = new QCheckBox(tr("Flip &Horizontal"));
	mCbFlipV = new QCheckBox(tr("Flip &Vertical"));
	mCbCropMetadata = new QCheckBox(tr("&Crop from Metadata"));

	QGridLayout* layout = new QGridLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	layout->addWidget(rotateLabel, 0, 0);
	layout->addWidget(mRbRotate0, 1, 0);
	layout->addWidget(mRbRotateRight, 2, 0);
	layout->addWidget(mRbRotateLeft, 3, 0);
	layout->addWidget(mRbRotate180, 4, 0);

	layout->addWidget(transformLabel, 5, 0);
	layout->addWidget(mCbFlipH, 6, 0);
	layout->addWidget(mCbFlipV, 7, 0);
	layout->addWidget(mCbCropMetadata, 8, 0);
	layout->setColumnStretch(3, 10);

	connect(mRotateGroup, SIGNAL(buttonClicked(int)), this, SLOT(updateHeader()));
	connect(mCbFlipV, SIGNAL(clicked()), this, SLOT(updateHeader()));
	connect(mCbFlipH, SIGNAL(clicked()), this, SLOT(updateHeader()));
	connect(mCbCropMetadata, SIGNAL(clicked()), this, SLOT(updateHeader()));
}

void DkBatchTransformWidget::applyDefault() {

	mRbRotate0->setChecked(true);
	mCbFlipH->setChecked(false);
	mCbFlipV->setChecked(false);
	mCbCropMetadata->setChecked(false);

	updateHeader();
}

bool DkBatchTransformWidget::hasUserInput() const {
	
	return !mRbRotate0->isChecked() || mCbFlipH->isChecked() || mCbFlipV->isChecked() || mCbCropMetadata->isChecked();
}

bool DkBatchTransformWidget::requiresUserInput() const {

	return false;
}

void DkBatchTransformWidget::updateHeader() const {

	if (!hasUserInput())
		emit newHeaderText(tr("inactive"));
	else {
		
		QString txt;
		if (getAngle() != 0)
			txt += tr("Rotating by: %1").arg(getAngle());
		if (mCbFlipH->isChecked() || mCbFlipV->isChecked()) {
			if (!txt.isEmpty())
				txt += " | ";
			txt += tr("Flipping");
		}
		if(mCbCropMetadata->isChecked()) {
			if (!txt.isEmpty())
				txt += " | ";
			txt += tr("Crop");
		}
		emit newHeaderText(txt);
	}
}

void DkBatchTransformWidget::transferProperties(QSharedPointer<DkBatchTransform> batchTransform) const {

	batchTransform->setProperties(getAngle(), mCbFlipH->isChecked(), mCbFlipV->isChecked(), mCbCropMetadata->isChecked());
}

bool DkBatchTransformWidget::loadProperties(QSharedPointer<DkBatchTransform> batchTransform) {
	
	if (!batchTransform) {
		qWarning() << "cannot load settings, DkBatchTransform is NULL";
		return false;
	}

	bool errored = false;

	switch (batchTransform->angle()) {
	case -90:	mRbRotateLeft->setChecked(true); break;
	case 90:	mRbRotateLeft->setChecked(true); break;
	case 180:	mRbRotateLeft->setChecked(true); break;
	case 0:	break;	// nothing todo
	default: errored = true;
	}

	mCbFlipH->setChecked(batchTransform->horizontalFlip());
	mCbFlipV->setChecked(batchTransform->verticalFlip());
	mCbCropMetadata->setChecked(batchTransform->cropMetatdata());

	updateHeader();

	return !errored;
}

int DkBatchTransformWidget::getAngle() const {

	if (mRbRotate0->isChecked())
		return 0;
	else if (mRbRotateLeft->isChecked())
		return -90;
	else if (mRbRotateRight->isChecked())
		return 90;
	else if (mRbRotate180->isChecked())
		return 180;

	return 0;
}

// Batch Buttons --------------------------------------------------------------------
DkBatchButtonsWidget::DkBatchButtonsWidget(QWidget* parent) : DkWidget(parent) {
	createLayout();
	setPaused();
}

void DkBatchButtonsWidget::createLayout() {

	// play - pause button
	QIcon icon;
	icon.addPixmap(QIcon(":/nomacs/img/player-play.svg").pixmap(100), QIcon::Normal, QIcon::Off);
	icon.addPixmap(QIcon(":/nomacs/img/player-stop.svg").pixmap(100), QIcon::Normal, QIcon::On);

	mPlayButton = new QPushButton(icon, "", this);
	mPlayButton->setIconSize(QSize(100, 50));
	mPlayButton->setCheckable(true);
	mPlayButton->setFlat(true);
	mPlayButton->setShortcut(Qt::ALT + Qt::Key_Return);
	mPlayButton->setToolTip(tr("Start/Cancel Batch Processing (%1)").arg(mPlayButton->shortcut().toString()));

	icon = QIcon();
	QPixmap pm = QIcon(":/nomacs/img/batch-processing.svg").pixmap(100);
	icon.addPixmap(DkImage::colorizePixmap(pm, QColor(255, 255, 255)), QIcon::Normal, QIcon::On);
	icon.addPixmap(DkImage::colorizePixmap(pm, QColor(100, 100, 100)), QIcon::Disabled, QIcon::On);

	mLogButton = new QPushButton(icon, "", this);
	mLogButton->setIconSize(QSize(100, 50));
	mLogButton->setFlat(true);
	mLogButton->setEnabled(false);
	
	// connect
	connect(mPlayButton, SIGNAL(clicked(bool)), this, SIGNAL(playSignal(bool)));
	connect(mLogButton, SIGNAL(clicked()), this, SIGNAL(showLogSignal()));

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->addWidget(mPlayButton);
	layout->addWidget(mLogButton);
}

void DkBatchButtonsWidget::setPaused(bool paused) {
	mPlayButton->setChecked(!paused);
}

QPushButton * DkBatchButtonsWidget::logButton() {
	return mLogButton;
}

QPushButton * DkBatchButtonsWidget::playButton() {
	return mPlayButton;
}

// DkBatchInfo --------------------------------------------------------------------
DkBatchInfoWidget::DkBatchInfoWidget(QWidget* parent) : DkWidget(parent) {
	createLayout();
}

void DkBatchInfoWidget::createLayout() {

	mInfo = new QLabel(this);
	mInfo->setObjectName("BatchInfo");

	mIcon = new QLabel(this);
	
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setAlignment(Qt::AlignLeft);
	layout->addWidget(mIcon);
	layout->addWidget(mInfo);
}

void DkBatchInfoWidget::setInfo(const QString& message, const InfoMode& mode) {

	if (message == "")
		hide();
	else
		show();

	QPixmap pm;
	switch (mode) {
	case info_warning:	pm = QIcon(":/nomacs/img/warning.svg").pixmap(24); break;
	case info_critical:	pm = QIcon(":/nomacs/img/warning.svg").pixmap(24); break;
	default:			pm = QIcon(":/nomacs/img/info.svg").pixmap(24); break;
	}
	pm = DkImage::colorizePixmap(pm, QColor(255, 255, 255));
	mIcon->setPixmap(pm);

	mInfo->setText(message);
}

// Batch Widget --------------------------------------------------------------------
DkBatchWidget::DkBatchWidget(const QString& currentDirectory, QWidget* parent /* = 0 */) : DkWidget(parent) {
	
	mCurrentDirectory = currentDirectory;
	mBatchProcessing = new DkBatchProcessing(DkBatchConfig(), this);

	connect(mBatchProcessing, SIGNAL(progressValueChanged(int)), this, SLOT(updateProgress(int)));
	connect(mBatchProcessing, SIGNAL(finished()), this, SLOT(processingFinished()));

	createLayout();

	connect(inputWidget(), SIGNAL(updateInputDir(const QString&)), outputWidget(), SLOT(setInputDir(const QString&)));
	connect(&mLogUpdateTimer, SIGNAL(timeout()), this, SLOT(updateLog()));
	connect(profileWidget(), SIGNAL(saveProfileSignal(const QString&)), this, SLOT(saveProfile(const QString&)));
	connect(profileWidget(), SIGNAL(loadProfileSignal(const QString&)), this, SLOT(loadProfile(const QString&)));
	connect(profileWidget(), SIGNAL(applyDefaultSignal()), this, SLOT(applyDefault()));

	inputWidget()->setDir(currentDirectory);
	outputWidget()->setInputDir(currentDirectory);

	// change tabs with page up page down
	QAction* nextAction = new QAction(tr("next"), this);
	nextAction->setShortcut(Qt::Key_PageDown);
	connect(nextAction, SIGNAL(triggered()), this, SLOT(nextTab()));
	addAction(nextAction);

	QAction* previousAction = new QAction(tr("previous"), this);
	previousAction->setShortcut(Qt::Key_PageUp);
	previousAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(previousAction, SIGNAL(triggered()), this, SLOT(previousTab()));
	addAction(previousAction);
}

DkBatchWidget::~DkBatchWidget() {

	// close cancels the current process
	if (!cancel())
		mBatchProcessing->waitForFinished();
}

void DkBatchWidget::createLayout() {

	//setStyleSheet("QWidget{border: 1px solid #000000;}");

	mWidgets.resize(batchWidgets_end);

	// Input Directory
	mWidgets[batch_input] = new DkBatchContainer(tr("Input"), tr("no files selected"), this);
	mWidgets[batch_input]->setContentWidget(new DkBatchInput(this));
	inputWidget()->setDir(mCurrentDirectory);

	// fold content
	mWidgets[batch_resize] = new DkBatchContainer(tr("Resize"), tr("inactive"), this);
	mWidgets[batch_resize]->setContentWidget(new DkBatchResizeWidget(this));

	mWidgets[batch_transform] = new DkBatchContainer(tr("Transform"), tr("inactive"), this);
	mWidgets[batch_transform]->setContentWidget(new DkBatchTransformWidget(this));

#ifdef WITH_PLUGINS
	mWidgets[batch_plugin] = new DkBatchContainer(tr("Plugins"), tr("inactive"), this);
	mWidgets[batch_plugin]->setContentWidget(new DkBatchPluginWidget(this));
#endif

	mWidgets[batch_output] = new DkBatchContainer(tr("Output"), tr("not set"), this);
	mWidgets[batch_output]->setContentWidget(new DkBatchOutput(this));

	// profiles
	mWidgets[batch_profile] = new DkBatchContainer(tr("Profiles"), tr("inactive"), this);
	mWidgets[batch_profile]->setContentWidget(new DkProfileWidget(this));

	mProgressBar = new DkProgressBar(this);
	mProgressBar->setVisible(false);
	mProgressBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);	// progressbar is greedy otherwise

	QWidget* centralWidget = new QWidget(this);
	mCentralLayout = new QStackedLayout(centralWidget);
	mCentralLayout->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
	for (DkBatchContainer* w : mWidgets) {
		if (!w)
			continue;
		mCentralLayout->addWidget(w->contentWidget());
		connect(w, SIGNAL(showSignal()), this, SLOT(changeWidget()));
	}

	connect(mWidgets[batch_input]->contentWidget(), SIGNAL(changed()), this, SLOT(widgetChanged()));
	connect(mWidgets[batch_output]->contentWidget(), SIGNAL(changed()), this, SLOT(widgetChanged())); 

	mContentTitle = new QLabel("", this);
	mContentTitle->setObjectName("batchContentTitle");
	mContentInfo = new QLabel("", this);
	mContentInfo->setObjectName("batchContentInfo");

	QWidget* contentWidget = new QWidget(this);
	QVBoxLayout* dialogLayout = new QVBoxLayout(contentWidget);
	dialogLayout->addWidget(mContentTitle);
	dialogLayout->addWidget(mContentInfo);
	dialogLayout->addWidget(centralWidget);		// almost everything
	//dialogLayout->addStretch(10);
	//dialogLayout->addWidget(mButtons);

	// the tabs left
	QWidget* tabWidget = new QWidget(this);
	tabWidget->setObjectName("DkBatchTabs");

	QVBoxLayout* tabLayout = new QVBoxLayout(tabWidget);
	tabLayout->setAlignment(Qt::AlignTop);
	tabLayout->setContentsMargins(0, 0, 0, 0);
	tabLayout->setSpacing(0);

	// tab buttons must be checked exclusively
	QButtonGroup* tabGroup = new QButtonGroup(this);

	for (DkBatchContainer* w : mWidgets) {

		if (!w)
			continue;
		tabLayout->addWidget(w->headerWidget());
		tabGroup->addButton(w->headerWidget());
	}

	mInfoWidget = new DkBatchInfoWidget(this);

	mButtonWidget = new DkBatchButtonsWidget(this);
	mButtonWidget->show();
	tabLayout->addStretch();
	tabLayout->addWidget(mInfoWidget);
	tabLayout->addWidget(mProgressBar);
	tabLayout->addWidget(mButtonWidget);

	DkResizableScrollArea* tabScroller = new DkResizableScrollArea(this);
	tabScroller->setWidgetResizable(true);
	tabScroller->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
	tabScroller->setWidget(tabWidget);
	tabScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	DkResizableScrollArea* contentScroller = new DkResizableScrollArea(this);
	contentScroller->setWidgetResizable(true);
	contentScroller->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
	contentScroller->setWidget(contentWidget);
	//contentScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(tabScroller);
	layout->addWidget(contentScroller);

	// open the first tab
	if (!mWidgets.empty())
		mWidgets[0]->headerWidget()->click();

	connect(mButtonWidget, SIGNAL(playSignal(bool)), this, SLOT(toggleBatch(bool)));
	connect(mButtonWidget, SIGNAL(showLogSignal()), this, SLOT(showLog()));
	connect(this, SIGNAL(infoSignal(const QString&, const DkBatchInfoWidget::InfoMode&)), mInfoWidget, SLOT(setInfo(const QString&, const DkBatchInfoWidget::InfoMode&)));
}

DkBatchInput* DkBatchWidget::inputWidget() const {

	DkBatchInput* w = dynamic_cast<DkBatchInput*>(mWidgets[batch_input]->contentWidget());
	if (!w)
		qCritical() << "cannot cast to DkBatchInput";

	return w;
}

DkBatchOutput* DkBatchWidget::outputWidget() const {
	
	DkBatchOutput* w = dynamic_cast<DkBatchOutput*>(mWidgets[batch_output]->contentWidget());
	if (!w)
		qCritical() << "cannot cast to DkBatchOutput";

	return w;
}

DkBatchResizeWidget* DkBatchWidget::resizeWidget() const {

	DkBatchResizeWidget* w = dynamic_cast<DkBatchResizeWidget*>(mWidgets[batch_resize]->contentWidget());
	if (!w)
		qCritical() << "cannot cast to DkBatchResizeWidget";

	return w;
}

DkProfileWidget* DkBatchWidget::profileWidget() const {
	DkProfileWidget* w = dynamic_cast<DkProfileWidget*>(mWidgets[batch_profile]->contentWidget());
	if (!w)
		qCritical() << "cannot cast to DkBatchProfileWidget";

	return w;

}

#ifdef WITH_PLUGINS
DkBatchPluginWidget* DkBatchWidget::pluginWidget() const {

	DkBatchPluginWidget* w = dynamic_cast<DkBatchPluginWidget*>(mWidgets[batch_plugin]->contentWidget());
	if (!w)
		qCritical() << "cannot cast to DkBatchPluginWidget";

	return w;

}
#endif

DkBatchTransformWidget* DkBatchWidget::transformWidget() const {

	DkBatchTransformWidget* w = dynamic_cast<DkBatchTransformWidget*>(mWidgets[batch_transform]->contentWidget());

	if (!w)
		qCritical() << "cannot cast to DkBatchTransformWidget";

	return w;
}


void DkBatchWidget::toggleBatch(bool start) {

	if (start)
		startBatch();
	else
		cancel();
}

void DkBatchWidget::startBatch() {

	const DkBatchConfig& bc = createBatchConfig();

	if (!bc.isOk()) {
		mButtonWidget->setPaused();
		qWarning() << "could not create batch config...";
		return;
	}

	mBatchProcessing->setBatchConfig(bc);

	// reopen the input widget to show the status
	if (!mWidgets.empty())
		mWidgets[0]->headerWidget()->click();

	startProcessing();
	mBatchProcessing->compute();
}

DkBatchConfig DkBatchWidget::createBatchConfig(bool strict) const {

	//QMainWindow* mw = DkActionManager::instance().getMainWindow();

	// check if we are good to go
	if (strict && inputWidget()->getSelectedFiles().empty()) {
		emit infoSignal(tr("Please select files for processing."), DkBatchInfoWidget::InfoMode::info_warning);
		//QMessageBox::information(mw, tr("Wrong Configuration"), tr("Please select files for processing."), QMessageBox::Ok, QMessageBox::Ok);
		return DkBatchConfig();
	}

	if (!outputWidget()) {
		qDebug() << "FATAL ERROR: could not cast output widget";
		emit infoSignal(tr("I am missing a widget."), DkBatchInfoWidget::InfoMode::info_critical);
		//QMessageBox::critical(mw, tr("Fatal Error"), tr("I am missing a widget."), QMessageBox::Ok, QMessageBox::Ok);
		return DkBatchConfig();
	}

	if (strict && mWidgets[batch_output] && mWidgets[batch_input])  {
		bool outputChanged = outputWidget()->hasUserInput();
		QString inputDirPath = inputWidget()->getDir();
		QString outputDirPath = outputWidget()->getOutputDirectory();

		
		if (!outputChanged && inputDirPath.toLower() == outputDirPath.toLower() && 
			!(outputWidget()->overwriteMode() == DkSaveInfo::mode_overwrite ||
			 outputWidget()->overwriteMode() == DkSaveInfo::mode_do_not_save_output)) {
			emit infoSignal(tr("Please check 'Overwrite Existing Files' or choose a different output directory."));
			//QMessageBox::information(mw, tr("Wrong Configuration"), 
			//	tr("Please check 'Overwrite Existing Files' or choose a different output directory."), 
			//	QMessageBox::Ok, QMessageBox::Ok);
			return DkBatchConfig();
		}
	}


	DkSaveInfo si;
	si.setMode(outputWidget()->overwriteMode());
	si.setDeleteOriginal(outputWidget()->deleteOriginal());
	si.setInputDirIsOutputDir(outputWidget()->useInputDir());
	si.setCompression(outputWidget()->getCompression());

	DkBatchConfig config(inputWidget()->getSelectedFilesBatch(), outputWidget()->getOutputDirectory(), outputWidget()->getFilePattern());
	config.setSaveInfo(si);

	if (!config.getOutputDirPath().isEmpty() && !QDir(config.getOutputDirPath()).exists()) {

		DkMessageBox* msgBox = new DkMessageBox(
			QMessageBox::Question, tr("Create Output Directory"), 
			tr("Should I create:\n%1").arg(config.getOutputDirPath()), 
			(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel), 
			DkUtils::getMainWindow());

		msgBox->setDefaultButton(QMessageBox::Yes);
		msgBox->setObjectName("batchOutputDirDialog");

		int answer = msgBox->exec();

		if (answer != QMessageBox::Accepted && answer != QMessageBox::Yes) {
			return DkBatchConfig();
		}
	}

	if (strict && !config.isOk()) {

		if (config.getOutputDirPath().isEmpty()) {
			emit infoSignal(tr("Please select an output directory."), DkBatchInfoWidget::InfoMode::info_warning);
			//QMessageBox::information(mw, tr("Info"), tr("Please select an output directory."), QMessageBox::Ok, QMessageBox::Ok);
			return DkBatchConfig();
		}
		else if (!QDir(config.getOutputDirPath()).exists()) {
			emit infoSignal(tr("Sorry, I cannot create %1.").arg(config.getOutputDirPath()), DkBatchInfoWidget::InfoMode::info_critical);
			//QMessageBox::critical(mw, tr("Error"), tr("Sorry, I cannot create %1.").arg(config.getOutputDirPath()), QMessageBox::Ok, QMessageBox::Ok);
			return DkBatchConfig();
		}
		else if (config.getFileList().empty()) {
			emit infoSignal(tr("Sorry, I cannot find files to process."), DkBatchInfoWidget::InfoMode::info_critical);
			//QMessageBox::critical(mw, tr("Error"), tr("Sorry, I cannot find files to process."), QMessageBox::Ok, QMessageBox::Ok);
			return DkBatchConfig();
		}
		else if (config.getFileNamePattern().isEmpty()) {
			emit infoSignal(tr("Sorry, the file pattern is empty."), DkBatchInfoWidget::InfoMode::info_critical);
			//QMessageBox::critical(mw, tr("Error"), tr("Sorry, the file pattern is empty."), QMessageBox::Ok, QMessageBox::Ok);
			return DkBatchConfig();
		}
		//else if (config.getOutputDir() == QDir()) {
		//	QMessageBox::information(this, tr("Input Missing"), tr("Please choose a valid output directory\n%1").arg(config.getOutputDir().absolutePath()), QMessageBox::Ok, QMessageBox::Ok);
		//	return;
		//}

		qDebug() << "config not ok - canceling";
		emit infoSignal(tr("Sorry, I cannot start processing - please check the configuration."), DkBatchInfoWidget::InfoMode::info_critical);
		//QMessageBox::critical(mw, tr("Fatal Error"), tr("Sorry, I cannot start processing - please check the configuration."), QMessageBox::Ok, QMessageBox::Ok);
		return DkBatchConfig();
	}

	// create processing functions
	QSharedPointer<DkResizeBatch> resizeBatch(new DkResizeBatch);
	resizeWidget()->transferProperties(resizeBatch);

	// create processing functions
	QSharedPointer<DkBatchTransform> transformBatch(new DkBatchTransform);
	transformWidget()->transferProperties(transformBatch);

#ifdef WITH_PLUGINS
	// create processing functions
	QSharedPointer<DkPluginBatch> pluginBatch(new DkPluginBatch);
	pluginWidget()->transferProperties(pluginBatch);
#endif

	QVector<QSharedPointer<DkAbstractBatch> > processFunctions;

	if (resizeBatch->isActive())
		processFunctions.append(resizeBatch);

	if (transformBatch->isActive())
		processFunctions.append(transformBatch);

#ifdef WITH_PLUGINS
	if (pluginBatch->isActive()) {
		processFunctions.append(pluginBatch);
		pluginBatch->preLoad();
	}
#endif

	config.setProcessFunctions(processFunctions);

	return config;
}

bool DkBatchWidget::cancel() {

	if (mBatchProcessing->isComputing()) {
		emit infoSignal(tr("Canceling..."), DkBatchInfoWidget::InfoMode::info_message);
		mBatchProcessing->cancel();
		//mButtonWidget->playButton()->setEnabled(false);
		//stopProcessing();
		return false;
	}

	return true;
}

void DkBatchWidget::processingFinished() {

	stopProcessing();

}

void DkBatchWidget::startProcessing() {

	inputWidget()->startProcessing();
	mInfoWidget->setInfo("");

	//mProgressBar->setFixedWidth(100);
	qDebug() << "progressbar width: " << mProgressBar->width();
	mProgressBar->show();
	mProgressBar->reset();
	mProgressBar->setMaximum(inputWidget()->getSelectedFiles().size());
	mProgressBar->setTextVisible(false);
	mButtonWidget->logButton()->setEnabled(false);
	mButtonWidget->setPaused(false);

	DkGlobalProgress::instance().start();

	mLogUpdateTimer.start(1000);
}

void DkBatchWidget::stopProcessing() {

	inputWidget()->stopProcessing();

	if (mBatchProcessing)
		mBatchProcessing->postLoad();

	DkGlobalProgress::instance().stop();

	mProgressBar->hide();
	mProgressBar->reset();
	mButtonWidget->logButton()->setEnabled(true);
	mButtonWidget->setPaused(true);
	
	int numFailures = mBatchProcessing->getNumFailures();
	int numProcessed = mBatchProcessing->getNumProcessed();
	int numItems = mBatchProcessing->getNumItems();

	DkBatchInfoWidget::InfoMode im = (numFailures > 0) ? DkBatchInfoWidget::InfoMode::info_warning : DkBatchInfoWidget::InfoMode::info_message;
	mInfoWidget->setInfo(tr("%1/%2 files processed... %3 failed.").arg(numProcessed).arg(numItems).arg(numFailures), im);

	mLogNeedsUpdate = false;
	mLogUpdateTimer.stop();

	updateLog();
}

void DkBatchWidget::updateLog() {

	inputWidget()->setResults(mBatchProcessing->getResultList());
}

void DkBatchWidget::updateProgress(int progress) {

	mProgressBar->setValue(progress);
	mLogNeedsUpdate = true;

	DkGlobalProgress::instance().setProgressValue(qRound((double)progress / inputWidget()->getSelectedFiles().size()*100));
}

void DkBatchWidget::showLog() {

	QStringList log = mBatchProcessing->getLog();

	DkTextDialog* textDialog = new DkTextDialog(this);
	textDialog->setWindowTitle(tr("Batch Log"));
	textDialog->getTextEdit()->setReadOnly(true);
	textDialog->setText(log);

	textDialog->exec();
}

void DkBatchWidget::setSelectedFiles(const QStringList& selFiles) {

	if (!selFiles.empty()) {
		inputWidget()->getInputEdit()->appendFiles(selFiles);
		inputWidget()->changeTab(DkBatchInput::tab_text_input);
	}
}

void DkBatchWidget::changeWidget(DkBatchContainer* widget) {

	if (!widget)
		widget = dynamic_cast<DkBatchContainer*>(sender());

	if (!widget) {
		qWarning() << "changeWidget() called with NULL widget";
		return;
	}

	for (DkBatchContainer* cw : mWidgets) {

		if (cw == widget) {
			mCentralLayout->setCurrentWidget(cw->contentWidget());
			mContentTitle->setText(cw->headerWidget()->text());
			mContentInfo->setText(cw->headerWidget()->info());
			cw->headerWidget()->setChecked(true);
			connect(cw->headerWidget(), SIGNAL(infoChanged(const QString&)), mContentInfo, SLOT(setText(const QString&)), Qt::UniqueConnection);
		}
	}

}

void DkBatchWidget::nextTab() {

	int idx = mCentralLayout->currentIndex() + 1;
	idx %= mWidgets.size();

	changeWidget(mWidgets[idx]);
}

void DkBatchWidget::previousTab() {

	int idx = mCentralLayout->currentIndex() - 1;
	if (idx < 0)
		idx = mWidgets.size()-1;

	changeWidget(mWidgets[idx]);
}

void DkBatchWidget::saveProfile(const QString & profilePath) const {

	DkBatchConfig bc = createBatchConfig(false);	// false: no input/output must be profided

	//if (!bc.isOk()) {
	//	QMessageBox::critical(DkActionManager::instance().getMainWindow(), tr("Error"), tr("Sorry, I cannot save the settings, since they are incomplete..."));
	//	return;
	//}

	if (bc.getProcessFunctions().empty()) {
		QMessageBox::information(DkUtils::getMainWindow(), tr("Save Profile"), tr("Cannot save empty profile."));
		return;
	}

	if (!DkBatchProfile::saveProfile(profilePath, bc)) {
		QMessageBox::critical(DkUtils::getMainWindow(), tr("Error"), tr("Sorry, I cannot save the settings..."));
		return;
	}
	else
		qInfo() << "batch profile written to: " << profilePath;

	profileWidget()->profileSaved(DkBatchProfile::makeUserFriendly(profilePath));
}

void DkBatchWidget::loadProfile(const QString & profilePath) {

	DkBatchConfig bc = DkBatchProfile::loadProfile(profilePath);

	if (bc.getProcessFunctions().empty()) {
		
		QMessageBox::critical(DkUtils::getMainWindow(), 
			tr("Error Loading Profile"), 
			tr("Sorry, I cannot load batch settings from: \n%1").arg(profilePath));
		return;
	}

	applyDefault();

	if (!bc.getFileList().empty())
		setSelectedFiles(bc.getFileList());

	outputWidget()->loadProperties(bc);

	int warnings = 0;
	auto functions = bc.getProcessFunctions();
	for (QSharedPointer<DkAbstractBatch> cf : functions) {

		if (!cf) {
			qWarning() << "processing function is NULL - ignoring";
			continue;
		}
		
		// apply resize batch settings
		if (QSharedPointer<DkResizeBatch> rf = qSharedPointerDynamicCast<DkResizeBatch>(cf)) {
			if (!resizeWidget()->loadProperties(rf)) {
				warnings++;
			}
		}
		// apply resize batch settings
		else if (QSharedPointer<DkBatchTransform> tf = qSharedPointerDynamicCast<DkBatchTransform>(cf)) {
			if (!transformWidget()->loadProperties(tf)) {
				warnings++;
			}
		}
#ifdef WITH_PLUGINS
		// apply plugin batch settings
		else if (QSharedPointer<DkPluginBatch> pf = qSharedPointerDynamicCast<DkPluginBatch>(cf)) {
			if (!pluginWidget()->loadProperties(pf)) {
				warnings++;
			}
		}
#endif
		else {
			qWarning() << "illegal processing function: " << cf->name() << " - ignoring";
			warnings++;
		}
	}

	// TODO: feedback 
	qInfo() << "settings loaded with" << warnings << "warnings";

}

void DkBatchWidget::applyDefault() {

	for (DkBatchContainer* bc : mWidgets)
		bc->batchContent()->applyDefault();
}

void DkBatchWidget::widgetChanged() {
	
	if (mWidgets[batch_output] && mWidgets[batch_input])  {
		QString inputDirPath = dynamic_cast<DkBatchInput*>(mWidgets[batch_input]->contentWidget())->getDir();
		QString outputDirPath = dynamic_cast<DkBatchOutput*>(mWidgets[batch_output]->contentWidget())->getOutputDirectory();
	
		// TODO: shouldn't we enable it always?
		//mButtonWidget->playButton()->setEnabled(inputDirPath == "" || outputDirPath == "");
	}

	if (!inputWidget()->getSelectedFiles().isEmpty()) {

		QUrl url = inputWidget()->getSelectedFiles().first();
		QString fString = url.toString();
		fString = fString.replace("file:///", "");

		QFileInfo cFileInfo = QFileInfo(fString);
		if (!cFileInfo.exists())	// try an alternative conversion
			cFileInfo = QFileInfo(url.toLocalFile());

		outputWidget()->setExampleFilename(cFileInfo.fileName());
		mButtonWidget->playButton()->setEnabled(true);
	}
}

}
