/*******************************************************************************************************
 DkImageStorage.cpp
 Created on:	12.07.2013
 
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

#include "DkImageStorage.h"
#include "DkActionManager.h"
#include "DkSettings.h"
#include "DkTimer.h"
#include "DkMath.h"
#include "DkThumbs.h"

#pragma warning(push, 0)	// no warnings from includes - begin
#include <QDebug>
#include <QThread>
#include <QPixmap>
#include <QPainter>
#include <QBitmap>
#include <qmath.h>
#include <QSvgRenderer>
#pragma warning(pop)		// no warnings from includes - end

#if defined(Q_OS_WIN) && !defined(SOCK_STREAM)
#include <winsock2.h>	// needed since libraw 0.16
#endif
namespace nmc {

// DkImage --------------------------------------------------------------------
#ifdef Q_OS_WIN

// this function is copied from Qt 4.8.5 qpixmap_win.cpp since Qt removed the conversion from
// the QPixmap class in Qt5 and we are not interested in more Qt5/4 conversions. In addition,
// we would need another module int Qt 5
QImage DkImage::fromWinHBITMAP(HDC hdc, HBITMAP bitmap, int w, int h) {
	
	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = w;
	bmi.bmiHeader.biHeight      = -h;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biSizeImage   = w * h * 4;

	QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull())
		return image;

	// Get bitmap bits
	uchar *data = (uchar *) malloc(bmi.bmiHeader.biSizeImage); // is it cool to use malloc here?

	if (GetDIBits(hdc, bitmap, 0, h, data, &bmi, DIB_RGB_COLORS)) {
		// Create image and copy data into image.
		for (int y=0; y<h; ++y) {
			void *dest = (void *) image.scanLine(y);
			void *src = data + y * image.bytesPerLine();
			memcpy(dest, src, image.bytesPerLine());
		}
	} else {
		qWarning("qt_fromWinHBITMAP(), failed to get bitmap bits");
	}
	free(data);

	return image;
}

// this function is copied from Qt 4.8.5 qpixmap_win.cpp since Qt removed the conversion from
// the QPixmap class in Qt5 and we are not interested in more Qt5/4 conversions. In addition,
// we would need another module int Qt 5
QPixmap DkImage::fromWinHICON(HICON icon) {
	
	bool foundAlpha = false;
	HDC screenDevice = GetDC(0);
	HDC hdc = CreateCompatibleDC(screenDevice);
	ReleaseDC(0, screenDevice);

	ICONINFO iconinfo;
	bool result = (GetIconInfo(icon, &iconinfo) != 0); //x and y Hotspot describes the icon center
	if (!result)
		qWarning("QPixmap::fromWinHICON(), failed to GetIconInfo()");

	int w = iconinfo.xHotspot * 2;
	int h = iconinfo.yHotspot * 2;

	BITMAPINFOHEADER bitmapInfo;
	bitmapInfo.biSize        = sizeof(BITMAPINFOHEADER);
	bitmapInfo.biWidth       = w;
	bitmapInfo.biHeight      = h;
	bitmapInfo.biPlanes      = 1;
	bitmapInfo.biBitCount    = 32;
	bitmapInfo.biCompression = BI_RGB;
	bitmapInfo.biSizeImage   = 0;
	bitmapInfo.biXPelsPerMeter = 0;
	bitmapInfo.biYPelsPerMeter = 0;
	bitmapInfo.biClrUsed       = 0;
	bitmapInfo.biClrImportant  = 0;
	DWORD* bits;

	HBITMAP winBitmap = CreateDIBSection(hdc, (BITMAPINFO*)&bitmapInfo, DIB_RGB_COLORS, (VOID**)&bits, NULL, 0);
	HGDIOBJ oldhdc = (HBITMAP)SelectObject(hdc, winBitmap);
	DrawIconEx( hdc, 0, 0, icon, iconinfo.xHotspot * 2, iconinfo.yHotspot * 2, 0, 0, DI_NORMAL);
	QImage image = fromWinHBITMAP(hdc, winBitmap, w, h);

	for (int y = 0 ; y < h && !foundAlpha ; y++) {
		QRgb *scanLine= reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < w ; x++) {
			if (qAlpha(scanLine[x]) != 0) {
				foundAlpha = true;
				break;
			}
		}
	}
	if (!foundAlpha) {
		//If no alpha was found, we use the mask to set alpha values
		DrawIconEx( hdc, 0, 0, icon, w, h, 0, 0, DI_MASK);
		QImage mask = fromWinHBITMAP(hdc, winBitmap, w, h);

		for (int y = 0 ; y < h ; y++){
			QRgb *scanlineImage = reinterpret_cast<QRgb *>(image.scanLine(y));
			QRgb *scanlineMask = mask.isNull() ? 0 : reinterpret_cast<QRgb *>(mask.scanLine(y));
			for (int x = 0; x < w ; x++){
				if (scanlineMask && qRed(scanlineMask[x]) != 0)
					scanlineImage[x] = 0; //mask out this pixel
				else
					scanlineImage[x] |= 0xff000000; // set the alpha channel to 255
			}
		}
	}
	//dispose resources created by iconinfo call
	DeleteObject(iconinfo.hbmMask);
	DeleteObject(iconinfo.hbmColor);

	SelectObject(hdc, oldhdc); //restore state
	DeleteObject(winBitmap);
	DeleteDC(hdc);
	return QPixmap::fromImage(image);
}

// this function is copied from Qt 4.8.5 qpixmap_win.cpp since Qt removed the conversion from
// the QPixmap class in Qt5 and we are not interested in more Qt5/4 conversions. In addition,
// we would need another module int Qt 5
HBITMAP DkImage::toWinHBITMAP(const QPixmap& pm) {
	
	if (pm.isNull())
		return 0;

	HBITMAP bitmap = 0;

	int w = pm.width();
	int h = pm.height();

	HDC display_dc = GetDC(0);

	// Define the header
	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biSizeImage = w * h * 4;

	// Create the pixmap
	uchar *pixels = 0;
	bitmap = CreateDIBSection(display_dc, &bmi, DIB_RGB_COLORS, (void **)&pixels, 0, 0);
	ReleaseDC(0, display_dc);
	if (!bitmap) {
		qErrnoWarning("QPixmap::toWinHBITMAP(), failed to create dibsection");
		return 0;
	}
	if (!pixels) {
		qErrnoWarning("QPixmap::toWinHBITMAP(), did not allocate pixel data");
		return 0;
	}

	// Copy over the data
	const QImage image = pm.toImage().convertToFormat(QImage::Format_ARGB32);
	
	int bytes_per_line = w * 4;
	for (int y = 0; y<h; ++y)
		memcpy(pixels + y * bytes_per_line, image.scanLine(y), bytes_per_line);

	return bitmap;
}

// this function is copied from Qt 4.8.5 qpixmap_win.cpp since Qt removed the conversion from
// the QPixmap class in Qt5 and we are not interested in more Qt5/4 conversions. In addition,
// we would need another module int Qt 5
HBITMAP DkImage::createIconMask(const QBitmap& bitmap) {
	
	QImage bm = bitmap.toImage().convertToFormat(QImage::Format_Mono);
	int w = bm.width();
	int h = bm.height();
	int bpl = ((w + 15) / 16) * 2;                        // bpl, 16 bit alignment
	uchar *bits = new uchar[bpl*h];
	bm.invertPixels();
	for (int y = 0; y<h; y++)
		memcpy(bits + y*bpl, bm.scanLine(y), bpl);
	HBITMAP hbm = CreateBitmap(w, h, 1, 1, bits);
	delete[] bits;
	
	return hbm;
}

// this function is copied from Qt 4.8.5 qpixmap_win.cpp since Qt removed the conversion from
// the QPixmap class in Qt5 and we are not interested in more Qt5/4 conversions. In addition,
// we would need another module int Qt 5
HICON DkImage::toWinHICON(const QPixmap& pm) {
	
	QBitmap maskBitmap = pm.mask();
	
	if (maskBitmap.isNull()) {
		maskBitmap = QBitmap(pm.size());
		maskBitmap.fill(Qt::color1);
	}

	ICONINFO ii;
	ii.fIcon = true;
	ii.hbmMask = createIconMask(maskBitmap);
	ii.hbmColor = toWinHBITMAP(pm);
	ii.xHotspot = 0;
	ii.yHotspot = 0;

	HICON hIcon = CreateIconIndirect(&ii);

	DeleteObject(ii.hbmColor);
	DeleteObject(ii.hbmMask);

	return hIcon;
}

#endif

/**
 * Returns a string with the buffer size of an image.
 * @param img a QImage
 * @return QString a human readable string containing the buffer size
 **/ 
QString DkImage::getBufferSize(const QImage& img) {

	return getBufferSize(img.size(), img.depth());
}

/**
 * Returns a string with the buffer size of an image.
 * @param imgSize the image size
 * @param depth the image depth
 * @return QString a human readable string containing the buffer size
 **/ 
QString DkImage::getBufferSize(const QSize& imgSize, const int depth) {

	double size = (double)imgSize.width() * (double)imgSize.height() * (double)(depth/8.0f);
	QString sizeStr;
	qDebug() << "dimension: " << size;

	if (size >= 1024*1024*1024) {
		return QString::number(size/(1024.0f*1024.0f*1024.0f), 'f', 2) + " GB";
	}
	else if (size >= 1024*1024) {
		return QString::number(size/(1024.0f*1024.0f), 'f', 2) + " MB";
	}
	else if (size >= 1024) {
		return QString::number(size/1024.0f, 'f', 2) + " KB";
	}
	else {
		return QString::number(size, 'f', 2) + " B";
	}
}

/**
 * Returns a the buffer size of an image.
 * @param imgSize the image size
 * @param depth the image depth
 * @return buffer size in MB
 **/ 
float DkImage::getBufferSizeFloat(const QSize& imgSize, const int depth) {

	double size = (double)imgSize.width() * (double)imgSize.height() * (double)(depth/8.0f);
	QString sizeStr;

	return (float)size/(1024.0f*1024.0f);
}

/**
 * This function resizes an image according to the interpolation method specified.
 * @param img the image to resize
 * @param newSize the new size
 * @param factor the resize factor
 * @param interpolation the interpolation method
 * @return QImage the resized image
 **/ 
QImage DkImage::resizeImage(const QImage& img, const QSize& newSize, float factor /* = 1.0f */, int interpolation /* = ipl_cubic */, bool correctGamma /* = true */) {

	QSize nSize = newSize;

	// nothing to do
	if (img.size() == nSize && factor == 1.0f)
		return img;

	if (factor != 1.0f)
		nSize = QSize(qRound(img.width()*factor), qRound(img.height()*factor));

	if (nSize.width() < 1 || nSize.height() < 1) {
		return QImage();
	}

	Qt::TransformationMode iplQt = Qt::FastTransformation;
	switch(interpolation) {
	case ipl_nearest:	
	case ipl_area:		iplQt = Qt::FastTransformation; break;
	case ipl_linear:	
	case ipl_cubic:		
	case ipl_lanczos:	iplQt = Qt::SmoothTransformation; break;
	}
#ifdef WITH_OPENCV

	int ipl = CV_INTER_CUBIC;
	switch(interpolation) {
	case ipl_nearest:	ipl = CV_INTER_NN; break;
	case ipl_area:		ipl = CV_INTER_AREA; break;
	case ipl_linear:	ipl = CV_INTER_LINEAR; break;
	case ipl_cubic:		ipl = CV_INTER_CUBIC; break;
	case ipl_lanczos:	ipl = CV_INTER_LANCZOS4; break;
	}


	try {
		
		QImage qImg;
		cv::Mat resizeImage = DkImage::qImage2Mat(img);
		
		if (correctGamma) {
			resizeImage.convertTo(resizeImage, CV_16U, USHRT_MAX/255.0f);
			DkImage::gammaToLinear(resizeImage);
		}

		// is the image convertible?
		if (resizeImage.empty()) {
			qImg = img.scaled(newSize, Qt::IgnoreAspectRatio, iplQt);
		}
		else {

			cv::Mat tmp;
			cv::resize(resizeImage, tmp, cv::Size(nSize.width(), nSize.height()), 0, 0, ipl);
			resizeImage = tmp;
			
			if (correctGamma) {
				DkImage::linearToGamma(resizeImage);
				resizeImage.convertTo(resizeImage, CV_8U, 255.0f/USHRT_MAX);
			}

			qImg = DkImage::mat2QImage(resizeImage);
		}

		if (!img.colorTable().isEmpty())
			qImg.setColorTable(img.colorTable());

		return qImg;

	} catch (std::exception se) {

		return QImage();
	}

#else

	QImage qImg = img.copy();
	
	if (correctGamma)
		DkImage::gammaToLinear(qImg);
	qImg.scaled(nSize, Qt::IgnoreAspectRatio, iplQt);
	
	if (correctGamma)
		DkImage::linearToGamma(qImg);
	return qImg;
#endif
}
	
bool DkImage::alphaChannelUsed(const QImage& img) {

	if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
		return false;

	// number of used bytes per line
	int bpl = (img.width() * img.depth() + 7) / 8;
	int pad = img.bytesPerLine() - bpl;
	const uchar* ptr = img.bits();

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {

		for (int cIdx = 0; cIdx < bpl; cIdx++, ptr++) {

			if (cIdx % 4 == 3 && *ptr != 255)
				return true;
		}

		ptr += pad;
	}

	return false;
}

template <typename numFmt>
QVector<numFmt> DkImage::getLinear2GammaTable(int maxVal) {

	QVector<numFmt> gammaTable;
	double a = 0.055;

	for (int idx = 0; idx <= maxVal; idx++) {

		double i = idx/(double)maxVal;
		if (i <= 0.0031308) {
			gammaTable.append((numFmt)(qRound(i*12.92*(double)maxVal)));
		}
		else {
			gammaTable.append((numFmt)(qRound(((1+a)*pow(i,1/2.4)-a)*(double)maxVal)));
		}
	}

	return gammaTable;
}

template <typename numFmt>
QVector<numFmt> DkImage::getGamma2LinearTable(int maxVal) {

	// the formula should be:
	// i = px/255
	// i <= 0.04045 -> i/12.92
	// i > 0.04045 -> (i+0.055)/(1+0.055)^2.4

	qDebug() << "gamma2Linear: ";
	QVector<numFmt> gammaTable;
	double a = 0.055;

	for (int idx = 0; idx <= maxVal; idx++) {

		double i = idx/(double)maxVal;
		if (i <= 0.04045) {
			gammaTable.append((numFmt)(qRound(i/12.92*maxVal)));
		}
		else {
			gammaTable.append(pow((i+a)/(1+a),2.4)*maxVal > 0 ? (numFmt)(pow((i+a)/(1+a),2.4)*maxVal) : 0);
		}
	}

	return gammaTable;
}

void DkImage::gammaToLinear(QImage& img) {

	QVector<uchar> gt = getGamma2LinearTable<uchar>(255);
	mapGammaTable(img, gt);
}

void DkImage::linearToGamma(QImage& img) {

	QVector<uchar> gt = getLinear2GammaTable<uchar>(255);
	mapGammaTable(img, gt);
}

void DkImage::mapGammaTable(QImage& img, const QVector<uchar>& gammaTable) {

	DkTimer dt;

	// number of bytes per line used
	int bpl = (img.width() * img.depth() + 7) / 8;
	int pad = img.bytesPerLine() - bpl;

	//int channels = (img.hasAlphaChannel() || img.format() == QImage::Format_RGB32) ? 4 : 3;

	uchar* mPtr = img.bits();

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {

		for (int cIdx = 0; cIdx < bpl; cIdx++, mPtr++) {

			if (*mPtr < 0 || *mPtr > gammaTable.size()) {
				qDebug() << "WRONG VALUE: " << *mPtr;
				continue;
			}
			if ((int)gammaTable[*mPtr] < 0 || (int)gammaTable[*mPtr] > USHRT_MAX) {
				qDebug() << "WRONG VALUE: " << *mPtr;
				continue;
			}

			*mPtr = gammaTable[*mPtr];
		}
		mPtr += pad;
	}

	qDebug() << "gamma computation takes: " << dt;
}

QImage DkImage::normImage(const QImage& img) {

	QImage imgN = img.copy();
	normImage(imgN);

	return imgN;
}

bool DkImage::normImage(QImage& img) {

	uchar maxVal = 0;
	uchar minVal = 255;

	// number of used bytes per line
	int bpl = (img.width() * img.depth() + 7) / 8;
	int pad = img.bytesPerLine() - bpl;
	uchar* mPtr = img.bits();
	bool hasAlpha = img.hasAlphaChannel() || img.format() == QImage::Format_RGB32;

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {
		
		for (int cIdx = 0; cIdx < bpl; cIdx++, mPtr++) {
			
			if (hasAlpha && cIdx % 4 == 3)
				continue;

			if (*mPtr > maxVal)
				maxVal = *mPtr;
			if (*mPtr < minVal)
				minVal = *mPtr;
		}
		
		mPtr += pad;
	}

	if ((minVal == 0 && maxVal == 255) || maxVal-minVal == 0)
		return false;

	uchar* ptr = img.bits();
	
	for (int rIdx = 0; rIdx < img.height(); rIdx++) {
	
		for (int cIdx = 0; cIdx < bpl; cIdx++, ptr++) {

			if (hasAlpha && cIdx % 4 == 3)
				continue;

			*ptr = (uchar)qRound(255.0f*(*ptr-minVal)/(maxVal-minVal));
		}
		
		ptr += pad;
	}

	return true;

}

QImage DkImage::autoAdjustImage(const QImage& img) {

	QImage imgA = img.copy();
	autoAdjustImage(imgA);

	return imgA;
}

bool DkImage::autoAdjustImage(QImage& img) {

	//return DkImage::unsharpMask(img, 30.0f, 1.5f);

	DkTimer dt;
	qDebug() << "[Auto Adjust] image format: " << img.format();

	// for grayscale image - normalize is the same
	if (img.format() <= QImage::Format_Indexed8) {
		qDebug() << "[Auto Adjust] Grayscale - switching to Normalize: " << img.format();
		return normImage(img);
	}
	else if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied && 
		img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_RGB888) {
		qDebug() << "[Auto Adjust] Format not supported: " << img.format();
		return false;
	}

	int channels = (img.hasAlphaChannel() || img.format() == QImage::Format_RGB32) ? 4 : 3;

	uchar maxR = 0,		maxG = 0,	maxB = 0;
	uchar minR = 255,	minG = 255, minB = 255;

	// number of bytes per line used
	int bpl = (img.width() * img.depth() + 7) / 8;
	int pad = img.bytesPerLine() - bpl;

	uchar* mPtr = img.bits();
	uchar r,g,b;

	int histR[256] = {0};
	int histG[256] = {0};
	int histB[256] = {0};

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {

		for (int cIdx = 0; cIdx < bpl; ) {

			r = *mPtr; mPtr++;
			g = *mPtr; mPtr++;
			b = *mPtr; mPtr++;
			cIdx += 3;

			if (r > maxR)	maxR = r;
			if (r < minR)	minR = r;

			if (g > maxG)	maxG = g;
			if (g < minG)	minG = g;

			if (b > maxB)	maxB = b;
			if (b < minB)	minB = b;

			histR[r]++;
			histG[g]++;
			histB[b]++;


			// ?? strange but I would expect the alpha channel to be the first (big endian?)
			if (channels == 4) {
				mPtr++;
				cIdx++;
			}

		}
		mPtr += pad;
	}

	QColor ignoreChannel;
	bool ignoreR = maxR-minR == 0 || maxR-minR == 255;
	bool ignoreG = maxR-minR == 0 || maxG-minG == 255;
	bool ignoreB = maxR-minR == 0 || maxB-minB == 255;

	uchar* ptr = img.bits();

	if (ignoreR) {
		maxR = findHistPeak(histR);
		ignoreR = maxR-minR == 0 || maxR-minR == 255;
	}
	if (ignoreG) {
		maxG = findHistPeak(histG);
		ignoreG = maxG-minG == 0 || maxG-minG == 255;
	}
	if (ignoreB) {
		maxB = findHistPeak(histB);
		ignoreB = maxB-minB == 0 || maxB-minB == 255;
	}

	//qDebug() << "red max: " << maxR << " min: " << minR << " ignored: " << ignoreR;
	//qDebug() << "green max: " << maxG << " min: " << minG << " ignored: " << ignoreG;
	//qDebug() << "blue max: " << maxB << " min: " << minB << " ignored: " << ignoreB;
	//qDebug() << "computed in: " << dt;

	if (ignoreR && ignoreG && ignoreB) {
		qDebug() << "[Auto Adjust] There is no need to adjust the image";
		return false;
	}

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {

		for (int cIdx = 0; cIdx < bpl; ) {

			// don't check values - speed (but you see under-/overflows anyway)
			if (!ignoreR && *ptr < maxR)
				*ptr = (uchar)qRound(255.0f*((float)*ptr-minR)/(maxR-minR));
			else if (!ignoreR)
				*ptr = 255;

			ptr++;
			cIdx++;

			if (!ignoreG && *ptr < maxG)
				*ptr = (uchar)qRound(255.0f*((float)*ptr-minG)/(maxG-minG));
			else if (!ignoreG)
				*ptr = 255;

			ptr++;
			cIdx++;

			if (!ignoreB && *ptr < maxB)
				*ptr = (uchar)qRound(255.0f*((float)*ptr-minB)/(maxB-minB));
			else if (!ignoreB)
				*ptr = 255;
			ptr++;
			cIdx++;

			if (channels == 4) {
				ptr++;
				cIdx++;
			}

		}
		ptr += pad;
	}

	qDebug() << "[Auto Adjust] image adjusted in: " << dt;
	
	return true;
}

uchar DkImage::findHistPeak(const int* hist, float quantile) {

	int histArea = 0;

	for (int idx = 0; idx < 256; idx++)
		histArea += hist[idx];

	int sumBins = 0;

	for (int idx = 255; idx >= 0; idx--) {

		sumBins += hist[idx];
		
		if (sumBins/(float)histArea > quantile) {
			qDebug() << "max bin: " << idx;
			return (uchar)idx;
		}

	}

	qDebug() << "no max bin found... sum: " << sumBins;

	return 255;
}

QPixmap DkImage::makeSquare(const QPixmap & pm) {

	QRect r(QPoint(), pm.size());

	if (r.width() > r.height()) {
		r.setX(qFloor((r.width()-r.height())*0.5f));
		r.setWidth(r.height());
	}
	else {
		r.setY(qFloor((r.height()-r.width())*0.5f));
		r.setHeight(r.width());
	}

	return pm.copy(r);
}

QPixmap DkImage::merge(const QVector<QImage>& imgs) {

	if (imgs.size() > 10) {
		qWarning() << "DkImage::merge is built for a small amount of images, you gave me: " << imgs.size();
	}

	QPixmap pm;
	int margin = 10;
	int x = 0;
	QPainter p;

	for (const QImage& img : imgs) {

		// init on first
		if (pm.isNull()) {
			pm = QPixmap(img.height()*imgs.size() + margin*(imgs.size()-1), img.height());
			pm.fill(QColor(0,0,0,0));
			
			p.begin(&pm);
		}

		QPixmap cpm = DkImage::makeSquare(QPixmap::fromImage(img));
		QRect r(QPoint(x, 0), QSize(pm.height(), pm.height()));
		p.drawPixmap(r, cpm);
		x += r.width() + margin;
	}

	return pm;
}

QImage DkImage::cropToImage(const QImage & src, const DkRotatingRect & rect, const QColor & fillColor) {

	QTransform tForm; 
	QPointF cImgSize;
	rect.getTransform(tForm, cImgSize);

	// illegal?
	if (cImgSize.x() < 0.5f || cImgSize.y() < 0.5f)
		return src;

	qDebug() << cImgSize;

	double angle = DkMath::normAngleRad(rect.getAngle(), 0, CV_PI*0.5);
	double minD = qMin(std::abs(angle), std::abs(angle-CV_PI*0.5));

	QImage img = QImage(qRound(cImgSize.x()), qRound(cImgSize.y()), QImage::Format_ARGB32);
	img.fill(fillColor.rgba());

	// render the image into the new coordinate system
	QPainter painter(&img);
	painter.setWorldTransform(tForm);

	// for rotated rects we want perfect anti-aliasing
	if (minD > FLT_EPSILON)
		painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

	painter.drawImage(QRect(QPoint(), src.size()), src, QRect(QPoint(), src.size()));
	painter.end();

	return img;
}

QPixmap DkImage::colorizePixmap(const QPixmap& icon, const QColor& col, float opacity) {

	if (icon.isNull())
		return icon;

	QPixmap glow = icon.copy();
	QPixmap sGlow = glow.copy();
	sGlow.fill(col);

	QPainter painter(&glow);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);	// check if this is the right composition mode
	painter.setOpacity(opacity);
	painter.drawPixmap(glow.rect(), sGlow);

	return glow;
}

QPixmap DkImage::loadIcon(const QString & filePath) {
	
	if (filePath.isEmpty())
		return QPixmap();

	int s = DkSettingsManager::param().effectiveIconSize();
	QPixmap icon = loadFromSvg(filePath, QSize(s, s));
	
	if (!DkSettingsManager::param().display().defaultIconColor || DkSettingsManager::param().app().privateMode)
		icon = colorizePixmap(icon, DkSettingsManager::param().display().iconColor);

	return icon;
}

QPixmap DkImage::loadIcon(const QString & filePath, const QColor& col) {

	int s = DkSettingsManager::param().effectiveIconSize();
	QPixmap icon = loadFromSvg(filePath, QSize(s, s));
	icon = colorizePixmap(icon, col);

	return icon;
}

QPixmap DkImage::loadFromSvg(const QString & filePath, const QSize & size) {

	QSharedPointer<QSvgRenderer> svg(new QSvgRenderer(filePath));
	
	QPixmap pm(size);
	pm.fill(QColor(0, 0, 0, 0));	// clear background

	QPainter p(&pm);
	svg->render(&p);

	return pm;
}


#ifdef WITH_OPENCV

/**
 * Converts a QImage to a Mat
 * @param img formats supported: ARGB32 | RGB32 | RGB888 | Indexed8
 * @return cv::Mat the corresponding Mat
 **/ 
cv::Mat DkImage::qImage2Mat(const QImage& img) {

	cv::Mat mat2;
	QImage cImg;	// must be initialized here!	(otherwise the data is lost before clone())

	try {
		if (img.format() == QImage::Format_RGB32)
			qDebug() << "we have an RGB32 in memory...";

		if (img.format() == QImage::Format_ARGB32 || img.format() == QImage::Format_RGB32) {
			mat2 = cv::Mat(img.height(), img.width(), CV_8UC4, (uchar*)img.bits(), img.bytesPerLine());
			//qDebug() << "ARGB32 or RGB32";
		}
		else if (img.format() == QImage::Format_RGB888) {
			mat2 = cv::Mat(img.height(), img.width(), CV_8UC3, (uchar*)img.bits(), img.bytesPerLine());
			//qDebug() << "RGB888";
		}
		//// converting to indexed8 causes bugs in the qpainter
		//// see: http://qt-project.org/doc/qt-4.8/qimage.html
		//else if (img.format() == QImage::Format_Indexed8) {
		//	mat2 = Mat(img.height(), img.width(), CV_8UC1, (uchar*)img.bits(), img.bytesPerLine());
		//	//qDebug() << "indexed...";
		//}
		else {
			//qDebug() << "image flag: " << img.format();
			cImg = img.convertToFormat(QImage::Format_ARGB32);
			mat2 = cv::Mat(cImg.height(), cImg.width(), CV_8UC4, (uchar*)cImg.bits(), cImg.bytesPerLine());
			//qDebug() << "I need to convert the QImage to ARGB32";
		}

		mat2 = mat2.clone();	// we need to own the pointer
	}
	catch (...) {	// something went seriously wrong (e.g. out of memory)
		//DkNoMacs::dialog(QObject::tr("Sorry, could not convert image."));
		qDebug() << "[DkImage::qImage2Mat] could not convert image - something is seriously wrong down here...";

	}

	return mat2; 
}

/**
 * Converts a cv::Mat to a QImage.
 * @param img supported formats CV8UC1 | CV_8UC3 | CV_8UC4
 * @return QImage the corresponding QImage
 **/ 
QImage DkImage::mat2QImage(cv::Mat img) {

	QImage qImg;

	// since Mat header is copied, a new buffer should be allocated (check this!)
	if (img.depth() == CV_32F)
		img.convertTo(img, CV_8U, 255);

	if (img.type() == CV_8UC1) {
		qImg = QImage(img.data, (int)img.cols, (int)img.rows, (int)img.step, QImage::Format_Indexed8);	// opencv uses size_t for scaling in x64 applications
		//Mat tmp;
		//cvtColor(img, tmp, CV_GRAY2RGB);	// Qt does not support writing to index8 images
		//img = tmp;
	}
	if (img.type() == CV_8UC3) {
			
		//cv::cvtColor(img, img, CV_RGB2BGR);
		qImg = QImage(img.data, (int)img.cols, (int)img.rows, (int)img.step, QImage::Format_RGB888);
	}
	if (img.type() == CV_8UC4) {
		qImg = QImage(img.data, (int)img.cols, (int)img.rows, (int)img.step, QImage::Format_ARGB32);
	}

	qImg = qImg.copy();

	return qImg;
}

cv::Mat DkImage::get1DGauss(double sigma) {

	// correct -> checked with matlab reference
	int kernelsize = qCeil(sigma*3*2)+1;
	if (kernelsize < 3) kernelsize = 3;
	if ((kernelsize % 2) != 1) kernelsize+=1;

	cv::Mat gKernel = cv::Mat(1, kernelsize, CV_32F);
	float* kernelPtr = gKernel.ptr<float>();

	for (int idx = 0, x = -qFloor(kernelsize/2); idx < kernelsize; idx++,x++) {

		kernelPtr[idx] = (float)(exp(-(x*x)/(2*sigma*sigma)));	// 1/(sqrt(2pi)*sigma) -> discrete normalization
	}

	if (sum(gKernel).val[0] != 0)
		gKernel *= 1.0f/sum(gKernel).val[0];
	else
		qWarning() << "The kernel sum is zero\n";

	return gKernel;
}

void DkImage::linearToGamma(cv::Mat& img) {

	QVector<unsigned short> gt = getLinear2GammaTable<unsigned short>();
	mapGammaTable(img, gt);
}

void DkImage::gammaToLinear(cv::Mat& img) {

	QVector<unsigned short> gt = getGamma2LinearTable<unsigned short>();
	mapGammaTable(img, gt);
}

void DkImage::mapGammaTable(cv::Mat& img, const QVector<unsigned short>& gammaTable) {

	DkTimer dt;

	for (int rIdx = 0; rIdx < img.rows; rIdx++) {

		unsigned short* mPtr = img.ptr<unsigned short>(rIdx);

		for (int cIdx = 0; cIdx < img.cols; cIdx++) {

			for (int channelIdx = 0; channelIdx < img.channels(); channelIdx++, mPtr++) {

				if (*mPtr < 0 || *mPtr > gammaTable.size()) {
					qDebug() << "WRONG VALUE: " << *mPtr;
					continue;
				}
				if ((int)gammaTable[*mPtr] < 0 || (int)gammaTable[*mPtr] > USHRT_MAX) {
					qDebug() << "WRONG VALUE: " << *mPtr;
					continue;
				}

				*mPtr = gammaTable[*mPtr];
			}
		}
	}

	qDebug() << "gamma computation takes: " << dt;
}

void DkImage::logPolar(const cv::Mat& src, cv::Mat& dst, CvPoint2D32f center, double scaleLog, double angle, double scale) {

	cv::Mat mapx, mapy;

	cv::Size ssize, dsize;
	ssize = src.size();
	dsize = dst.size();

	mapx = cv::Mat(dsize.height, dsize.width, CV_32F);
	mapy = cv::Mat(dsize.height, dsize.width, CV_32F);

	float xDist = dst.cols - center.x;
	float yDist = dst.rows - center.y;

	double radius = std::sqrt(xDist*xDist + yDist*yDist);

	scale *= src.cols / std::log(radius / scaleLog + 1.0);

	int x, y;
	cv::Mat bufx, bufy, bufp, bufa;
	double ascale = ssize.height / (2 * CV_PI);
	cv::AutoBuffer<float> _buf(4 * dsize.width);
	float* buf = _buf;

	bufx = cv::Mat(1, dsize.width, CV_32F, buf);
	bufy = cv::Mat(1, dsize.width, CV_32F, buf + dsize.width);
	bufp = cv::Mat(1, dsize.width, CV_32F, buf + dsize.width * 2);
	bufa = cv::Mat(1, dsize.width, CV_32F, buf + dsize.width * 3);

	for (x = 0; x < dsize.width; x++)
		bufx.ptr<float>()[x] = (float)x - center.x;

	for (y = 0; y < dsize.height; y++) {
		float* mx = mapx.ptr<float>(y);
		float* my = mapy.ptr<float>(y);

		for (x = 0; x < dsize.width; x++)
			bufy.ptr<float>()[x] = (float)y - center.y;

		cv::cartToPolar(bufx, bufy, bufp, bufa);

		for (x = 0; x < dsize.width; x++) {
			bufp.ptr<float>()[x] /= (float)scaleLog;
			bufp.ptr<float>()[x] += 1.0f;
		}

		cv::log(bufp, bufp);

		for (x = 0; x < dsize.width; x++) {
			double rho = bufp.ptr<float>()[x] * scale;
			double phi = bufa.ptr<float>()[x] + angle;

			if (phi < 0)
				phi += 2 * CV_PI;
			else if (phi > 2 * CV_PI)
				phi -= 2 * CV_PI;

			phi *= ascale;

			//qDebug() << "phi: " << bufa.data.fl[x];

			mx[x] = (float)rho;
			my[x] = (float)phi;
		}
	}

	cv::remap(src, dst, mapx, mapy, CV_INTER_AREA, IPL_BORDER_REPLICATE);
}

void DkImage::tinyPlanet(QImage& img, double scaleLog, double angle, QSize s, bool invert /* = false */) {

	QTransform rotationMatrix;
	rotationMatrix.rotate((invert) ? (double)-90 : (double)90);
	img = img.transformed(rotationMatrix);

	// make square
	img = img.scaled(s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	cv::Mat mImg = DkImage::qImage2Mat(img);

	qDebug() << "scale log: " << scaleLog << " inverted: " << invert;
	logPolar(mImg, mImg, cv::Point2d(mImg.cols*0.5, mImg.rows*0.5), scaleLog, angle);

	img = DkImage::mat2QImage(mImg);
}

#endif

bool DkImage::unsharpMask(QImage& img, float sigma, float weight) {

#ifdef WITH_OPENCV
	DkTimer dt;
	//DkImage::gammaToLinear(img);
	cv::Mat imgCv = DkImage::qImage2Mat(img);

	cv::Mat imgG;
	cv::Mat gx = cv::getGaussianKernel(qRound(4*sigma+1), sigma);
	cv::Mat gy = gx.t();
	cv::sepFilter2D(imgCv, imgG, CV_8U, gx, gy);
	//cv::GaussianBlur(imgCv, imgG, cv::Size(4*sigma+1, 4*sigma+1), sigma);		// this is awesomely slow
	cv::addWeighted(imgCv, weight, imgG, 1-weight, 0, imgCv);
	img = DkImage::mat2QImage(imgCv);

	qDebug() << "unsharp mask takes: " << dt;
	//DkImage::linearToGamma(img);
#endif


	return true;
}

QImage DkImage::createThumb(const QImage& image) {

	if (image.isNull())
		return image;

	int maxThumbSize = (int)(max_thumb_size * DkSettingsManager::param().dPIScaleFactor());
	int imgW = image.width();
	int imgH = image.height();

	if (imgW > maxThumbSize || imgH > maxThumbSize) {
		if (imgW > imgH) {
			imgH = qRound((float)maxThumbSize / imgW * imgH);
			imgW = maxThumbSize;
		} 
		else if (imgW < imgH) {
			imgW = qRound((float)maxThumbSize / imgH * imgW);
			imgH = maxThumbSize;
		}
		else {
			imgW = maxThumbSize;
			imgH = maxThumbSize;
		}
	}

	// fast downscaling
	QImage thumb = image.scaled(QSize(imgW*2, imgH*2), Qt::KeepAspectRatio, Qt::FastTransformation);
	thumb = thumb.scaled(QSize(imgW, imgH), Qt::KeepAspectRatio, Qt::SmoothTransformation);

	//qDebug() << "thumb size in createThumb: " << thumb.size() << " format: " << thumb.format();

	return thumb;
};

bool DkImage::addToImage(QImage& img, unsigned char val) {

	// number of bytes per line used
	int bpl = (img.width() * img.depth() + 7) / 8;
	int pad = img.bytesPerLine() - bpl;
	uchar* ptr = img.bits();
	bool done = false;

	for (int rIdx = 0; rIdx < img.height(); rIdx++) {

		for (int cIdx = 0; cIdx < bpl; cIdx++) {

			// add it & we're done
			if (*ptr <= 255-val) {
				*ptr += val;
				done = true;
				break;
			}

			int ov = *ptr+(int)val;	// compute the overflow
			val = (char)(ov-255);

			*ptr = val;
			ptr++;
		}

		if (done)
			break;

		ptr += pad;
	}

	return done;
};

QColor DkImage::getMeanColor(const QImage& img) {

	// some speed-up params
	int nC = qRound(img.depth()/8.0f);
	int rStep = qRound(img.height()/100.0f)+1;
	int cStep = qRound(img.width()/100.0f)+1;
	int numCols = 42;

	int offset = (nC > 1) ? 1 : 0;	// no offset for grayscale images
	QMap<QRgb, int> colLookup;
	int maxColCount = 0;
	QRgb maxCol = 0;

	for (int rIdx = 0; rIdx < img.height(); rIdx += rStep) {

		const unsigned char* pixel = img.constScanLine(rIdx);

		for (int cIdx = 0; cIdx < img.width()*nC; cIdx += cStep*nC) {

			QColor cColC(qRound(pixel[cIdx+2*offset]/255.0f*numCols), 
				qRound(pixel[cIdx+offset]/255.0f*numCols), 
				qRound(pixel[cIdx]/255.0f*numCols));
			QRgb cCol = cColC.rgb();

			//// skip black
			//if (cColC.saturation() < 10)
			//	continue;
			if (qRed(cCol) < 3 && qGreen(cCol) < 3 && qBlue(cCol) < 3)
				continue;
			if (qRed(cCol) > numCols-3 && qGreen(cCol) > numCols-3 && qBlue(cCol) > numCols-3)
				continue;

			if (colLookup.contains(cCol)) {
				colLookup[cCol]++;
			}
			else
				colLookup[cCol] = 1;

			if (colLookup[cCol] > maxColCount) {
				maxCol = cCol;
				maxColCount = colLookup[cCol];
			}
		}
	}

	if (maxColCount > 0)
		return QColor(qRound((float)qRed(maxCol)/numCols*255), qRound((float)qGreen(maxCol)/numCols*255), qRound((float)qBlue(maxCol)/numCols*255));
	else
		return DkSettingsManager::param().display().hudBgColor;
}


// DkImageStorage --------------------------------------------------------------------
DkImageStorage::DkImageStorage(const QImage& img) {
	mImg = img;

	mComputeThread = new QThread;
	mComputeThread->start();
	moveToThread(mComputeThread);

	connect(DkActionManager::instance().action(DkActionManager::menu_view_anti_aliasing), SIGNAL(toggled(bool)), this, SLOT(antiAliasingChanged(bool)));
}

void DkImageStorage::setImage(const QImage& img) {

	mStop = true;
	mImgs.clear();	// is it save (if the thread is still working?)
	mImg = img;
}

void DkImageStorage::antiAliasingChanged(bool antiAliasing) {

	DkSettingsManager::param().display().antiAliasing = antiAliasing;

	if (!antiAliasing) {
		mStop = true;
		mImgs.clear();
	}

	emit infoSignal((antiAliasing) ? tr("Anti Aliasing Enabled") : tr("Anti Aliasing Disabled"));
	emit imageUpdated();

}

QImage DkImageStorage::getImageConst() const {
	
	return mImg;
}

QImage DkImageStorage::getImage(float factor) {

	if (factor >= 0.5f || mImg.isNull() || !DkSettingsManager::param().display().antiAliasing)
		return mImg;

	// check if we have an image similar to that requested
	for (int idx = 0; idx < mImgs.size(); idx++) {

		if ((float)mImgs.at(idx).height()/mImg.height() >= factor)
			return mImgs.at(idx);
	}

	// if the image does not exist - create it
	if (!mBusy && mImgs.empty() && /*img.colorTable().isEmpty() &&*/ mImg.width() > 32 && mImg.height() > 32) {
		mStop = false;
		// nobody is busy so start working
		QMetaObject::invokeMethod(this, "computeImage", Qt::QueuedConnection);
	}

	// currently no alternative is available
	return mImg;
}

void DkImageStorage::computeImage() {

	// obviously, computeImage gets called multiple times in some wired cases...
	if (!mImgs.empty())
		return;

	DkTimer dt;
	mBusy = true;
	QImage resizedImg = mImg;
	

	// down sample the image until it is twice times full HD
	QSize iSize = mImg.size();
	while (iSize.width() > 2*1920 && iSize.height() > 2*1920)	// in general we need less than 200 ms for the whole downscaling if we start at 1500 x 1500
		iSize *= 0.5;

	// for extreme panorama images the Qt scaling crashes (if we have a width > 30000) so we simply 
	if (qMax(iSize.width(), iSize.height()) < 20000)
		resizedImg = resizedImg.scaled(iSize, Qt::KeepAspectRatio, Qt::FastTransformation);

	// it would be pretty strange if we needed more than 30 sub-images
	for (int idx = 0; idx < 30; idx++) {

		QSize s = resizedImg.size();
		s *= 0.5;

		if (s.width() < 32 || s.height() < 32)
			break;

#ifdef WITH_OPENCV
		cv::Mat rImgCv = DkImage::qImage2Mat(resizedImg);
		cv::Mat tmp;
		cv::resize(rImgCv, tmp, cv::Size(s.width(), s.height()), 0, 0, CV_INTER_AREA);
		resizedImg = DkImage::mat2QImage(tmp);
#else
		resizedImg = resizedImg.scaled(s, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#endif

		// new image assigned?
		if (mStop)
			break;

		mMutex.lock();
		mImgs.push_front(resizedImg);
		mMutex.unlock();
	}

	mBusy = false;

	// tell my caller I did something
	emit imageUpdated();

	qDebug() << "pyramid computation took me: " << dt << " layers: " << mImgs.size();

	if (mImgs.size() > 6)
		qDebug() << "layer size > 6: " << mImg.size();

}

}
