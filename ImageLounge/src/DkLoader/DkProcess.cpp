/*******************************************************************************************************
 DkProcess.cpp
 Created on:	27.12.2014
 
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

#include "DkProcess.h"
#include "DkUtils.h"
#include "DkImageContainer.h"
#include "DkImageStorage.h"
#include "DkPluginManager.h"
#include "DkSettings.h"
#include "DkMath.h"

#pragma warning(push, 0)	// no warnings from includes - begin
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrentMap>
#include <QWidget>
#pragma warning(pop)		// no warnings from includes - end

namespace nmc {

/// <summary>
/// Generic compute method for DkBatch Info <see cref="DkBatchInfo"/>.
/// This method allows for a simplified interface if a derived class
/// just needs to process the image itself (not meta data).
/// </summary>
/// <param name="container">Container the image container to be processed.</param>
/// <param name="logStrings">log strings.</param>
/// <param name="batchInfo">The batch information. You can save any information into this class.</param>
/// <returns>
/// true on success
/// </returns>
bool DkAbstractBatch::compute(QSharedPointer<DkImageContainer> container, const DkSaveInfo& , QStringList& logStrings, QVector<QSharedPointer<DkBatchInfo> >&) const {

	return compute(container, logStrings);
}

/// <summary>
/// Generic compute method.
/// This method allows for a simplified interface if a derived class
/// just needs to process the image itself (not meta data).
/// </summary>
/// <param name="container">Container the image container to be processed.</param>
/// <param name="logStrings">log strings.</param>
/// <returns>true on success</returns>
bool DkAbstractBatch::compute(QSharedPointer<DkImageContainer> container, QStringList& logStrings) const {
	
	QImage img = container->image();
	
	bool isOk = compute(img, logStrings);

	if (isOk)
		container->setImage(img, QObject::tr("Batch Action"));

	return isOk;
}

QString DkAbstractBatch::settingsName() const {
	
	// make name() settings friendly
	QString sn = name();
	sn.replace("[", "");
	sn.replace("]", "");
	sn.replace(" ", "");
	
	return sn;
}

QSharedPointer<DkAbstractBatch> DkAbstractBatch::createFromName(const QString& settingsName) {

	QSharedPointer<DkAbstractBatch> batch(new DkResizeBatch());

	if (batch->settingsName() == settingsName)
		return batch;
	
	batch = QSharedPointer<DkBatchTransform>::create();

	if (batch->settingsName() == settingsName)
		return batch;

#ifdef WITH_PLUGINS
	batch = QSharedPointer<DkPluginBatch>::create();

	if (batch->settingsName() == settingsName)
		return batch;
#endif

	qCritical() << "cannot instantiate batch, illegal settings name: " << settingsName;
	return QSharedPointer<DkAbstractBatch>();
}

// DkResizeBatch --------------------------------------------------------------------
DkResizeBatch::DkResizeBatch() {

	mIplMethod = DkImage::ipl_area;	// define here because of included
}

QString DkResizeBatch::name() const {

	return QObject::tr("[Resize Batch]");
}

void DkResizeBatch::setProperties(float scaleFactor, int mode, int prop, int iplMethod, bool correctGamma) {

	mScaleFactor = scaleFactor;
	mMode = mode;
	mProperty = prop;
	mIplMethod = iplMethod;
	mCorrectGamma = correctGamma;
}

void DkResizeBatch::saveSettings(QSettings & settings) const {

	settings.beginGroup(settingsName());
	settings.setValue("ScaleFactor", mScaleFactor);
	settings.setValue("Mode", mMode);
	settings.setValue("Property", mProperty);
	settings.setValue("IplMethod", mIplMethod);
	settings.setValue("CorrectGamma", mCorrectGamma);
	settings.endGroup();

}

void DkResizeBatch::loadSettings(QSettings & settings) {

	settings.beginGroup(settingsName());
	mScaleFactor	= settings.value("ScaleFactor", mScaleFactor).toFloat();
	mMode			= settings.value("Mode", mMode).toInt();
	mProperty		= settings.value("Property", mProperty).toInt();
	mIplMethod		= settings.value("IplMethod", mIplMethod).toInt();
	mCorrectGamma	= settings.value("Correct Gamma", mCorrectGamma).toBool();
	settings.endGroup();
}

bool DkResizeBatch::isActive() const {

	if (mMode != mode_default)
		return true;

	if (mScaleFactor != 1.0f)
		return true;

	return false;
}

int DkResizeBatch::mode() const {
	return mMode;
}

int DkResizeBatch::property() const {
	return mProperty;
}

int DkResizeBatch::iplMethod() const {
	return mIplMethod;
}

float DkResizeBatch::scaleFactor() const {
	return mScaleFactor;
}

bool DkResizeBatch::correctGamma() const {
	return mCorrectGamma;
}

bool DkResizeBatch::compute(QImage& img, QStringList& logStrings) const {

	if (mScaleFactor == 1.0f) {
		logStrings.append(QObject::tr("%1 scale factor is 1 -> ignoring").arg(name()));
		return true;
	}

	QSize size;
	float sf = 1.0f;
	QImage tmpImg;

	if (prepareProperties(img.size(), size, sf, logStrings))
		tmpImg = DkImage::resizeImage(img, size, sf, mIplMethod, mCorrectGamma);
	else {
		logStrings.append(QObject::tr("%1 no need for resizing.").arg(name()));
		return true;
	}

	if (tmpImg.isNull()) {
		logStrings.append(QObject::tr("%1 could not resize image.").arg(name()));
		return false;
	}

	if (mMode == mode_default)
		logStrings.append(QObject::tr("%1 image resized, scale factor: %2%").arg(name()).arg(mScaleFactor*100.0f));
	else
		logStrings.append(QObject::tr("%1 image resized, new side: %2 px").arg(name()).arg(mScaleFactor));

	img = tmpImg;

	return true;
}

bool DkResizeBatch::prepareProperties(const QSize& imgSize, QSize& size, float& scaleFactor, QStringList& logStrings) const {

	float sf = 1.0f;
	QSize normalizedSize = imgSize; 

	if (mMode == mode_default) {
		scaleFactor = this->mScaleFactor;
		return true;
	}
	else if (mMode == mode_long_side) {
		
		if (imgSize.width() < imgSize.height())
			normalizedSize.transpose();
	}
	else if (mMode == mode_short_side) {

		if (imgSize.width() > imgSize.height())
			normalizedSize.transpose();
	}
	else if (mMode == mode_height)
		normalizedSize.transpose();

	sf = this->mScaleFactor/normalizedSize.width();

	if (sf > 1.0 && this->mProperty == prop_decrease_only) {
		
		logStrings.append(QObject::tr("%1 I need to increase the image, but the option is set to decrease only -> skipping.").arg(name()));
		return false;
	}
	else if (sf < 1.0f && this->mProperty == prop_increase_only) {
		logStrings.append(QObject::tr("%1 I need to decrease the image, but the option is set to increase only -> skipping.").arg(name()));
		return false;
	}
	else if (sf == 1.0f) {
		logStrings.append(QObject::tr("%1 image size matches scale factor -> skipping.").arg(name()));
		return false;
	}

	size.setWidth(qRound(this->mScaleFactor));
	size.setHeight(qRound(sf*normalizedSize.height()));

	if (normalizedSize != imgSize)
		size.transpose();

	return true;
}

// DkTransformBatch --------------------------------------------------------------------
DkBatchTransform::DkBatchTransform() {
}

QString DkBatchTransform::name() const {
	return QObject::tr("[Transform Batch]");
}

void DkBatchTransform::setProperties(int angle, bool horizontalFlip /* = false */, bool verticalFlip /* = false */, bool cropFromMetadata /* = false */) {
	
	mAngle = angle;
	mHorizontalFlip = horizontalFlip;
	mVerticalFlip = verticalFlip;
	mCropFromMetadata = cropFromMetadata;
}

void DkBatchTransform::saveSettings(QSettings & settings) const {

	settings.beginGroup(settingsName());
	settings.setValue("Angle", mAngle);
	settings.setValue("HorizontalFlip", mHorizontalFlip);
	settings.setValue("VerticalFlip", mVerticalFlip);
	settings.setValue("CropFromMetadata", mCropFromMetadata);
	settings.endGroup();
}

void DkBatchTransform::loadSettings(QSettings & settings) {

	settings.beginGroup(settingsName());
	mAngle = settings.value("Angle", mAngle).toInt();
	mHorizontalFlip = settings.value("HorizontalFlip", mHorizontalFlip).toBool();
	mVerticalFlip = settings.value("VerticalFlip", mVerticalFlip).toBool();
	mCropFromMetadata = settings.value("CropFromMetadata", mCropFromMetadata).toBool();
	settings.endGroup();
}

bool DkBatchTransform::isActive() const {

	return mHorizontalFlip || mVerticalFlip || mAngle != 0 || mCropFromMetadata;
}

int DkBatchTransform::angle() const {
	return mAngle;
}

bool DkBatchTransform::horizontalFlip() const {
	return mHorizontalFlip;
}

bool DkBatchTransform::verticalFlip() const {
	return mVerticalFlip;
}

bool DkBatchTransform::cropMetatdata() const {
	return mCropFromMetadata;
}

bool DkBatchTransform::compute(QSharedPointer<DkImageContainer> container, QStringList& logStrings) const {

	
	if (!isActive()) {
		logStrings.append(QObject::tr("%1 inactive -> skipping").arg(name()));
		return true;
	}

	DkRotatingRect rect = container->cropRect();
	if (mCropFromMetadata) {
		if (!rect.isEmpty())
			container->cropImage(rect, QColor(), false);
	}
	
	QImage img = container->image();
	QImage tmpImg;

	if (mAngle != 0) {
		QTransform rotationMatrix;
		rotationMatrix.rotate((double)mAngle);
		tmpImg = img.transformed(rotationMatrix);
	}
	else
		tmpImg = img;

	tmpImg = tmpImg.mirrored(mHorizontalFlip, mVerticalFlip);
	
	if (!tmpImg.isNull()) {
	
		container->setImage(tmpImg, QObject::tr("transformed"));

		if (rect.isEmpty() && mCropFromMetadata)
			logStrings.append(QObject::tr("%1 image transformed.").arg(name()));
		else
			logStrings.append(QObject::tr("%1 image transformed and cropped.").arg(name()));

	}
	else {
		logStrings.append(QObject::tr("%1 error, could not transform image.").arg(name()));
		return false;
	}

	return true;
}

#ifdef WITH_PLUGINS
// DkPluginBatch --------------------------------------------------------------------
DkPluginBatch::DkPluginBatch() {
}

void DkPluginBatch::setProperties(const QStringList & pluginList) {
	mPluginList = pluginList;
}

void DkPluginBatch::saveSettings(QSettings & settings) const {

	settings.beginGroup(settingsName());
	settings.setValue("pluginList", mPluginList.join(";"));

	for (const QSharedPointer<DkPluginContainer> plugin : mPlugins) {
		DkBatchPluginInterface* bPlugin = plugin->batchPlugin();
		assert(bPlugin);
		bPlugin->saveSettings(settings);
	}

	settings.endGroup();
}

void DkPluginBatch::loadSettings(QSettings & settings) {

	settings.beginGroup(settingsName());
	mPluginList = settings.value("pluginList", mPluginList).toString().split(";");

	loadAllPlugins();

	for (const QSharedPointer<DkPluginContainer> plugin : mPlugins) {
		DkBatchPluginInterface* bPlugin = plugin->batchPlugin();
		assert(bPlugin);
		bPlugin->loadSettings(settings);
	}

	settings.endGroup();
}


void DkPluginBatch::preLoad() {

	loadAllPlugins();
}

void DkPluginBatch::postLoad(const QVector<QSharedPointer<DkBatchInfo> >& batchInfo) const {

	for (int idx = 0; idx < mPlugins.size(); idx++) {

		QSharedPointer<DkPluginContainer> pluginContainer = mPlugins[idx];
		QString runID = mRunIDs[idx];

		if (pluginContainer) {
	
			// get plugin
			DkBatchPluginInterface* plugin = pluginContainer->batchPlugin();

			qDebug() << "[POST LOAD]" << pluginContainer->pluginName() << "id:" << runID;
			QVector<QSharedPointer<DkBatchInfo> > fInfos = DkBatchInfo::filter(batchInfo, runID);

			// check if it is ok
			if (plugin) {
				plugin->postLoadPlugin(fInfos);
			}
		}
	}
}

bool DkPluginBatch::compute(
	QSharedPointer<DkImageContainer> container, 
	const DkSaveInfo& saveInfo,
	QStringList& logStrings, 
	QVector<QSharedPointer<DkBatchInfo> >& batchInfos) const {

	if (!isActive()) {
		logStrings.append(QObject::tr("%1 inactive -> skipping").arg(name()));
		return true;
	}

	for (int idx = 0; idx < mPlugins.size(); idx++) {

		QSharedPointer<DkPluginContainer> pluginContainer = mPlugins[idx];
		QString runID = mRunIDs[idx];

		if (pluginContainer) {
			// get plugin
			DkPluginInterface* plugin = pluginContainer->plugin();

			// check if it is ok
			if ( plugin && 
				(plugin->interfaceType() == DkPluginInterface::interface_basic || 
				 plugin->interfaceType() == DkPluginInterface::interface_batch)) {

				// apply the plugin
				QSharedPointer<DkImageContainer> result;
				
				if (plugin->interfaceType() == DkPluginInterface::interface_basic)
					result = plugin->runPlugin(runID, container);
				else if (plugin->interfaceType() == DkPluginInterface::interface_batch) {

					DkBatchPluginInterface* bPlugin = pluginContainer->batchPlugin();
					QSharedPointer<DkBatchInfo> info;

					if (bPlugin)
						result = bPlugin->runPlugin(runID, container, saveInfo, info);
					else 
						logStrings.append(QObject::tr("%1 Cannot cast batch plugin %2.").arg(name()).arg(pluginContainer->pluginName()));

					batchInfos << info;
				}

				if (result && result->hasImage())
					container = result;
				else
					logStrings.append(QObject::tr("%1 Cannot apply %2.").arg(name()).arg(pluginContainer->pluginName()));
			}
			else
				logStrings.append(QObject::tr("%1 illegal plugin interface: %2").arg(name()).arg(pluginContainer->pluginName()));
		}
		else 
			logStrings.append(QObject::tr("%1 Cannot apply plugin because it is NULL.").arg(name()));
	}

	if (!container || !container->hasImage()) {
		logStrings.append(QObject::tr("%1 error, could not apply plugins.").arg(name()));
		return false;
	}
	else
		logStrings.append(QObject::tr("%1 plugins applied.").arg(name()));

	return true;
}

QString DkPluginBatch::name() const {
	return QObject::tr("[Plugin Batch]");
}

bool DkPluginBatch::isActive() const {
	
	return !mPluginList.empty();
}

QStringList DkPluginBatch::pluginList() const {
	return mPluginList;
}

void DkPluginBatch::loadAllPlugins() {

	// already loaded?
	if (mPlugins.size() == mPluginList.size())
		return;

	QString runId;

	for (const QString& cPluginString : mPluginList) {

		// load plugin
		QSharedPointer<DkPluginContainer> pluginContainer;
		QString runID;
		loadPlugin(cPluginString, pluginContainer, runID);
		mPlugins << pluginContainer;	// also add the empty ones...
		mRunIDs << runID;

		if (pluginContainer) {

			qDebug() << "loading" << pluginContainer->pluginName() << "id:" << runID;

			// get plugin
			DkBatchPluginInterface* plugin = pluginContainer->batchPlugin();

			// check if it is ok
			if (plugin) {
				plugin->preLoadPlugin();
			}
		}
		else
			qWarning() << "could not load: " << cPluginString;
	}
}

void DkPluginBatch::loadPlugin(const QString & pluginString, QSharedPointer<DkPluginContainer> & plugin, QString& runID) const {

	QString uiSeparator = " | ";	// TODO: make a nice define

	QStringList ids = pluginString.split(uiSeparator);

	if (ids.size() != 2) {
		qWarning() << "plugin string does not match:" << pluginString;
	}
	else {
		plugin = DkPluginManager::instance().getPluginByName(ids[0]);

		if (plugin)
			runID = plugin->actionNameToRunId(ids[1]);
	}
}
#endif

// DkBatchProcess --------------------------------------------------------------------
DkBatchProcess::DkBatchProcess(const DkSaveInfo& saveInfo) {
	mSaveInfo = saveInfo;
}

void DkBatchProcess::setProcessChain(const QVector<QSharedPointer<DkAbstractBatch> > processes) {

	mProcessFunctions = processes;
}

QString DkBatchProcess::inputFile() const {

	return mSaveInfo.inputFilePath();
}

QString DkBatchProcess::outputFile() const {

	return mSaveInfo.outputFilePath();
}

QVector<QSharedPointer<DkBatchInfo> > DkBatchProcess::batchInfo() const {

	return mInfos;
}

bool DkBatchProcess::hasFailed() const {

	return mFailure != 0;
}

bool DkBatchProcess::wasProcessed() const {
	
	return mIsProcessed;
}

bool DkBatchProcess::compute() {

	mIsProcessed = true;

	QFileInfo fInfoIn(mSaveInfo.inputFilePath());
	QFileInfo fInfoOut(mSaveInfo.outputFilePath());

	// check errors
	if (fInfoOut.exists() && mSaveInfo.mode() == DkSaveInfo::mode_skip_existing) {
		mLogStrings.append(QObject::tr("%1 already exists -> skipping (check 'overwrite' if you want to overwrite the file)").arg(mSaveInfo.outputFilePath()));
		mFailure++;
		return mFailure == 0;
	}
	else if (!fInfoIn.exists()) {
		mLogStrings.append(QObject::tr("Error: input file does not exist"));
		mLogStrings.append(QObject::tr("Input: %1").arg(mSaveInfo.inputFilePath()));
		mFailure++;
		return mFailure == 0;
	}
	else if (mSaveInfo.inputFilePath() == mSaveInfo.outputFilePath() && mProcessFunctions.empty()) {
		mLogStrings.append(QObject::tr("Skipping: nothing to do here."));
		mFailure++;
		return mFailure == 0;
	}
	
	// do the work
	if (mProcessFunctions.empty() && mSaveInfo.inputFilePath() == mSaveInfo.outputFilePath() && fInfoIn.suffix() == fInfoOut.suffix()) {	// rename?
		if (!renameFile())
			mFailure++;
		return mFailure == 0;
	}
	else if (mProcessFunctions.empty() && fInfoIn.suffix() == fInfoOut.suffix()) {	// copy?
		if (!copyFile())
			mFailure++;
		else
			deleteOriginalFile();

		return mFailure == 0;
	}

	process();

	return mFailure == 0;
}

QStringList DkBatchProcess::getLog() const {

	return mLogStrings;
}

bool DkBatchProcess::process() {

	mLogStrings.append(QObject::tr("processing %1").arg(mSaveInfo.inputFilePath()));

	QSharedPointer<DkImageContainer> imgC(new DkImageContainer(mSaveInfo.inputFilePath()));

	if (!imgC->loadImage() || imgC->image().isNull()) {
		mLogStrings.append(QObject::tr("Error while loading..."));
		mFailure++;
		return false;
	}

	for (QSharedPointer<DkAbstractBatch> batch : mProcessFunctions) {

		if (!batch) {
			mLogStrings.append(QObject::tr("Error: cannot process a NULL function."));
			continue;
		}

		QVector<QSharedPointer<DkBatchInfo> > cInfos;
		if (!batch->compute(imgC, mSaveInfo, mLogStrings, cInfos)) {
			mLogStrings.append(QObject::tr("%1 failed").arg(batch->name()));
			mFailure++;
		}

		mInfos << cInfos;
	}

	// report we could not back-up & break here
	if (!prepareDeleteExisting()) {
		mFailure++;
		return false;
	}

	// early break
	if (mSaveInfo.mode() == DkSaveInfo::mode_do_not_save_output) {
		mLogStrings.append(QObject::tr("%1 not saved - option 'Do not Save' is checked...").arg(mSaveInfo.outputFilePath()));
		return true;
	}

	if (imgC->saveImage(mSaveInfo.outputFilePath(), mSaveInfo.compression())) {
		mLogStrings.append(QObject::tr("%1 saved...").arg(mSaveInfo.outputFilePath()));
	}
	else {
		mLogStrings.append(QObject::tr("Could not save: %1").arg(mSaveInfo.outputFilePath()));
		mFailure++;
	}

	if (!deleteOrRestoreExisting()) {
		mFailure++;
		return false;
	}

	return true;
}

bool DkBatchProcess::renameFile() {

	if (QFileInfo(mSaveInfo.outputFilePath()).exists()) {
		mLogStrings.append(QObject::tr("Error: could not rename file, the target file exists already."));
		return false;
	}

	QFile file(mSaveInfo.inputFilePath());

	// Note: if two images are renamed at the same time to the same name, one image is lost -> see Qt comment Race Condition
	if (!file.rename(mSaveInfo.outputFilePath())) {
		mLogStrings.append(QObject::tr("Error: could not rename file"));
		mLogStrings.append(file.errorString());
		return false;
	}
	else
		mLogStrings.append(QObject::tr("Renaming: %1 -> %2").arg(mSaveInfo.inputFilePath()).arg(mSaveInfo.outputFilePath()));

	return true;
}

bool DkBatchProcess::copyFile() {

	QFile file(mSaveInfo.inputFilePath());

	if (mSaveInfo.mode() == DkSaveInfo::mode_do_not_save_output) {
		mLogStrings.append(QObject::tr("I should copy the file, but 'Do not Save' is checked - so I will do nothing..."));
		return false;
	}

	if (QFileInfo(mSaveInfo.outputFilePath()).exists() && mSaveInfo.mode() == DkSaveInfo::mode_overwrite) {
		if (!deleteOrRestoreExisting())
			return false;	// early break
	}

	if (!file.copy(mSaveInfo.outputFilePath())) {
		mLogStrings.append(QObject::tr("Error: could not copy file"));
		mLogStrings.append(QObject::tr("Input: %1").arg(mSaveInfo.inputFilePath()));
		mLogStrings.append(QObject::tr("Output: %1").arg(mSaveInfo.outputFilePath()));
		mLogStrings.append(file.errorString());
		return false;
	}
	else
		mLogStrings.append(QObject::tr("Copying: %1 -> %2").arg(mSaveInfo.inputFilePath()).arg(mSaveInfo.outputFilePath()));

	return true;
}

bool DkBatchProcess::prepareDeleteExisting() {

	if (QFileInfo(mSaveInfo.outputFilePath()).exists() && mSaveInfo.mode() == DkSaveInfo::mode_overwrite) {

		mSaveInfo.createBackupFilePath();

		// check the uniqueness : )
		if (QFileInfo(mSaveInfo.backupFilePath()).exists()) {
			mLogStrings.append(QObject::tr("Error: back-up (%1) file already exists").arg(mSaveInfo.backupFilePath()));
			mSaveInfo.clearBackupFilePath();
			return false;
		}

		QFile file(mSaveInfo.outputFilePath());

		if (!file.rename(mSaveInfo.backupFilePath())) {
			mLogStrings.append(QObject::tr("Error: could not rename existing file to %1").arg(mSaveInfo.backupFilePath()));
			mLogStrings.append(file.errorString());
			mSaveInfo.clearBackupFilePath();
			return false;
		}
	}

	return true;
}

bool DkBatchProcess::deleteOrRestoreExisting() {

	QFileInfo outInfo(mSaveInfo.outputFilePath());

	if (outInfo.exists() && !mSaveInfo.backupFilePath().isEmpty() && mSaveInfo.backupFileInfo().exists()) {
		QFile file(mSaveInfo.backupFilePath());

		if (!file.remove()) {
			mLogStrings.append(QObject::tr("Error: could not delete existing file"));
			mLogStrings.append(file.errorString());
			return false;
		}
	}
	// fall-back
	else if (!outInfo.exists()) {
		
		QFile file(mSaveInfo.backupFilePath());

		if (!file.rename(mSaveInfo.outputFilePath())) {
			mLogStrings.append(QObject::tr("Ui - a lot of things went wrong sorry, your original file can be found here: %1").arg(mSaveInfo.backupFilePath()));
			mLogStrings.append(file.errorString());
			return false;
		}
		else {
			mLogStrings.append(QObject::tr("I could not save to %1 so I restored the original file.").arg(mSaveInfo.outputFilePath()));
		}
	}

	return true;
}

bool DkBatchProcess::deleteOriginalFile() {

	if (mSaveInfo.inputFilePath() == mSaveInfo.outputFilePath())
		return true;

	if (!mFailure && mSaveInfo.isDeleteOriginal()) {
		QFile oFile(mSaveInfo.inputFilePath());

		if (oFile.remove())
			mLogStrings.append(QObject::tr("%1 deleted.").arg(mSaveInfo.inputFilePath()));
		else {
			mFailure++;
			mLogStrings.append(QObject::tr("I could not delete %1").arg(mSaveInfo.inputFilePath()));
			return false;
		}
	}
	else if (mFailure)
		mLogStrings.append(QObject::tr("I did not delete the original because I detected %1 failure(s).").arg(mFailure));

	return true;
}

// DkBatchConfig --------------------------------------------------------------------
DkBatchConfig::DkBatchConfig(const QStringList& fileList, const QString& outputDir, const QString& fileNamePattern) {

	mFileList = fileList;
	mOutputDirPath = outputDir;
	mFileNamePattern = fileNamePattern;
	
};

bool DkBatchConfig::isOk() const {

	if (mOutputDirPath.isEmpty())
		return false;

	QDir oDir(mOutputDirPath);

	if (!oDir.exists()) {
		if (!oDir.mkpath("."))
			return false;	// output dir does not exist & I cannot create it
	}

	if (mFileList.empty())
		return false;

	if (mFileNamePattern.isEmpty())
		return false;

	return true;
}


// DkBatchProcessing --------------------------------------------------------------------
DkBatchProcessing::DkBatchProcessing(const DkBatchConfig& config, QWidget* parent /*= 0*/) : QObject(parent) {

	mBatchConfig = config;

	connect(&mBatchWatcher, SIGNAL(progressValueChanged(int)), this, SIGNAL(progressValueChanged(int)));
	connect(&mBatchWatcher, SIGNAL(finished()), this, SIGNAL(finished()));
}

void DkBatchProcessing::init() {

	mBatchItems.clear();
	
	QStringList fileList = mBatchConfig.getFileList();

	for (int idx = 0; idx < fileList.size(); idx++) {

		DkSaveInfo si = mBatchConfig.saveInfo();

		QFileInfo cFileInfo = QFileInfo(fileList.at(idx));
		QString outDir = si.isInputDirOutputDir() ? cFileInfo.absolutePath() : mBatchConfig.getOutputDirPath();

		DkFileNameConverter converter(cFileInfo.fileName(), mBatchConfig.getFileNamePattern(), idx);
		QString outputFilePath = QFileInfo(outDir, converter.getConvertedFileName()).absoluteFilePath();

		// set input/output file path
		si.setInputFilePath(fileList.at(idx));
		si.setOutputFilePath(outputFilePath);

		DkBatchProcess cProcess(si);
		cProcess.setProcessChain(mBatchConfig.getProcessFunctions());

		mBatchItems.push_back(cProcess);
	}
}

void DkBatchConfig::saveSettings(QSettings & settings) const {

	settings.beginGroup("General");
	settings.setValue("FileList", mFileList.join(";"));
	settings.setValue("OutputDirPath", mOutputDirPath);
	settings.setValue("FileNamePattern", mFileNamePattern);

	mSaveInfo.saveSettings(settings);

	for (auto pf : mProcessFunctions)
		pf->saveSettings(settings);

	settings.endGroup();
}

void DkBatchConfig::loadSettings(QSettings & settings) {

	settings.beginGroup("General");
	mFileList = settings.value("FileList", mFileList).toString().split(";");
	mOutputDirPath = settings.value("OutputDirPath", mOutputDirPath).toString();
	mFileNamePattern = settings.value("FileNamePattern", mFileNamePattern).toString();

	mSaveInfo.loadSettings(settings);

	QStringList groups = settings.childGroups();
	
	for (const QString& name : groups) {

		// known groups that are not batch processes
		if (name == "SaveInfo")
			continue;

		QSharedPointer<DkAbstractBatch> batch = DkAbstractBatch::createFromName(name);

		// if it is valid - append the process
		if (batch) {
			batch->loadSettings(settings);
			mProcessFunctions << batch;
		}
	}

	for (auto pf : mProcessFunctions)
		pf->saveSettings(settings);

	settings.endGroup();
}

void DkBatchProcessing::compute() {

	init();

	qDebug() << "computing...";

	if (mBatchWatcher.isRunning())
		mBatchWatcher.waitForFinished();

	QFuture<void> future = QtConcurrent::map(mBatchItems, &nmc::DkBatchProcessing::computeItem);
	mBatchWatcher.setFuture(future);
}

bool DkBatchProcessing::computeItem(DkBatchProcess& item) {

	return item.compute();
}

void DkBatchProcessing::postLoad() {

	// collect batch infos
	QVector<QSharedPointer<DkBatchInfo> > batchInfo;

	for (DkBatchProcess batch : mBatchItems) {
		batchInfo << batch.batchInfo();
	}

	for (QSharedPointer<DkAbstractBatch> fun : mBatchConfig.getProcessFunctions()) {
		fun->postLoad(batchInfo);
	}
}

QStringList DkBatchProcessing::getLog() const {

	QStringList log;

	for (DkBatchProcess batch : mBatchItems) {

		log << batch.getLog();
		log << "";	// add empty line between images
	}

	return log;
}

int DkBatchProcessing::getNumFailures() const {

	int numFailures = 0;

	for (DkBatchProcess batch : mBatchItems) {
		
		if (batch.hasFailed())
			numFailures++;
	}

	return numFailures;
}

int DkBatchProcessing::getNumProcessed() const {

	int numProcessed = 0;

	for (DkBatchProcess batch : mBatchItems) {

		if (batch.wasProcessed())
			numProcessed++;
	}

	return numProcessed;
}

QList<int> DkBatchProcessing::getCurrentResults() {

	if (mResList.empty()) {
		for (int idx = 0; idx < mBatchItems.size(); idx++)
			mResList.append(batch_item_not_computed);
	}

	for (int idx = 0; idx < mResList.size(); idx++) {

		if (mResList.at(idx) != batch_item_not_computed)
			continue;

		if (mBatchItems.at(idx).wasProcessed())
			mResList[idx] = mBatchItems.at(idx).hasFailed() ? batch_item_failed : batch_item_succeeded;
	}

	return mResList;
}

QStringList DkBatchProcessing::getResultList() const {

	QStringList results;

	for (DkBatchProcess batch : mBatchItems) {

		if (batch.wasProcessed())
			results.append(getBatchSummary(batch));
	}

	return results;
}

QString DkBatchProcessing::getBatchSummary(const DkBatchProcess& batch) const {

	QString res = batch.inputFile() + "\t";

	if (!batch.hasFailed())
		res += " <span style=\" color:#00aa00;\">" + tr("[OK]") + "</span>";
	else
		res += " <span style=\" color:#aa0000;\">" + tr("[FAIL]") + "</span>";

	return res;
}

void DkBatchProcessing::waitForFinished() {
	mBatchWatcher.waitForFinished();
}

int DkBatchProcessing::getNumItems() const {

	return mBatchItems.size();
}

bool DkBatchProcessing::isComputing() const {

	return mBatchWatcher.isRunning();
}

void DkBatchProcessing::cancel() {

	mBatchWatcher.cancel();
}

// DkBatchProfile --------------------------------------------------------------------
QString DkBatchProfile::ext = "pnm";	// profile file extension

DkBatchProfile::DkBatchProfile(const QString& profileDir) {

	mProfileDir = (profileDir.isEmpty()) ? defaultProfilePath() : profileDir;
}

DkBatchConfig DkBatchProfile::loadProfile(const QString & profilePath) {

	QFileInfo fi(profilePath);
	if (!fi.exists() || !fi.isFile()) {
		qInfo() << "cannot read profile from:" << profilePath;
		return DkBatchConfig();
	}

	QSettings s(profilePath, QSettings::IniFormat);
	DkBatchConfig bc;
	bc.loadSettings(s);

	return bc;
}

bool DkBatchProfile::saveProfile(const QString & profilePath, const DkBatchConfig & batchConfig) {
	
	QSettings s(profilePath, QSettings::IniFormat);
	s.clear();
	batchConfig.saveSettings(s);

	return true;
}

QString DkBatchProfile::defaultProfilePath() {

	return DkUtils::getAppDataPath() + QDir::separator() + "Profiles";
}

QString DkBatchProfile::profileNameToPath(const QString & profileName) {
	return defaultProfilePath() + QDir::separator() + profileName + "." + ext;
}

QStringList DkBatchProfile::profileNames() {

	if (mProfilePaths.empty())
		mProfilePaths = index(mProfileDir);

	QStringList userNames;
	for (const QString& p : mProfilePaths)
		userNames << makeUserFriendly(p);

	return userNames;
}

QStringList DkBatchProfile::index(const QString & profileDir) const {

	QStringList exts;
	exts << "*." + ext;

	QDir pd(profileDir);
	QStringList profiles = pd.entryList(exts, QDir::Files, QDir::Name);

	qDebug() << "I have found these profiles: " << profiles;

	return profiles;
}

QString DkBatchProfile::makeUserFriendly(const QString & profilePath) {
	
	QString pName = QFileInfo(profilePath).baseName();
	return pName;
}

QString DkBatchProfile::extension() {
	return ext;
}


}