// VectorFieldVis.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "ColorMap.h"
#include "cmdline.h"


#define EPSILON 1e-10

#define SHOW_IMAGE(x) {cv::imshow(#x, x); cv::waitKey();}

bool is_valid(float value)
{
	return !isinf(value) && !isnan(value);
}

class WindField
{
public:
	WindField(std::string strVWnd, std::string strUWnd)
	{
		GDALAllRegister();
		CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "NO");
		pDataV = readData(strVWnd);
		pDataU = readData(strUWnd);
	}

	~WindField()
	{
		CPLFree(pDataV);
		CPLFree(pDataU);
	}

	int Width()	{	return nWidth;	}

	int Height() {	return nHeight;	}

	float GetWindDirX(int nX, int nY)
	{
		if (nX >= nWidth || nX < 0 || nY >= nHeight || nY < 0)
			return 0;

		float fltValue = pDataV[nY * nWidth + nX];

		return is_valid(fltValue) ? fltValue : 0;
	}

	float GetWindDirY(int nX, int nY)
	{
		if (nX >= nWidth || nX < 0 || nY >= nHeight || nY < 0)
			return 0;

		return is_valid(pDataU[nY * nWidth + nX]) ? pDataU[nY * nWidth + nX] : 0;
	}

	float GetWindLevel(int nX, int nY)
	{
		if (nX >= nWidth || nX < 0 || nY >= nHeight || nY < 0)
			return 0;

		float fltV = pDataV[nY * nWidth + nX];
		float fltU = pDataU[nY * nWidth + nX];

		return is_valid(sqrtf(fltV * fltV + fltU * fltU)) ? sqrtf(fltV * fltV + fltU * fltU) : 0;
	}

protected:
	float* readData(std::string strPath)
	{
		GDALDataset *poDataset = (GDALDataset *) GDALOpen(strPath.c_str(), GA_ReadOnly ); 
		GDALRasterBand *poBand = poDataset->GetRasterBand(1);

		nWidth = poDataset->GetRasterXSize(); 
		nHeight = poDataset->GetRasterYSize();

		float* pafScanblock1 = (float *)CPLMalloc(sizeof(float)*(nWidth)*(nHeight)); 
		poBand->RasterIO( GF_Read, 0, 0,nWidth,nHeight,pafScanblock1,nWidth,nHeight,GDALDataType(poBand->GetRasterDataType()),0, 0); 
		delete poDataset;

		return pafScanblock1;
	}

private:
	int nWidth;
	int nHeight;
	float* pDataV;
	float* pDataU;
};


cv::Vec3d Trace_Pixel(cv::Mat &imageColor, WindField &wf, double x, double y, int step)
{
	int width = imageColor.cols;
	int height = imageColor.rows;

	cv::Vec3d field_color;

	while(step >= 0)
	{

		field_color += imageColor.at<cv::Vec3b >((int)round(x), (int)round(y));

		double deltaX = wf.GetWindDirX((int)y, (int)x);
		double deltaY = wf.GetWindDirY((int)y, (int)x);
		double dist = sqrt( deltaX * deltaX + deltaY * deltaY ) + EPSILON;

		x += deltaX / dist;
		y += deltaY / dist;

		if (x < 0 )
			x = 0;
		if (x > height - 1)	
			x = height - 1;

		if (y < 0 )
			y = 0;
		if (y > width - 1)	
			y = width - 1;

		step--;
	}
	return field_color;
}



void Generate_LIC(cv::Mat &imageColor, cv::Mat &imageField, WindField &wf)
{
	int y, x;

#pragma omp parallel for
	for (x = 0; x < imageColor.rows; x ++)
	{
		for (y = 0; y < imageColor.cols; y++)
		{
			if (y == 1183 && x == 0)
			{
				int a = 0;
			}
			cv::Vec3d sum_val = Trace_Pixel(imageColor, wf, x, y, 5);

			imageField.at<cv::Vec3b>(x, y) = sum_val /21;
		}
	}
}


int _tmain(int argc, _TCHAR* argv[])
{
	cmdline::parser a;

	a.add<std::string>("uwnd", 'u', "uwnd file path", true, "");
	a.add<std::string>("vwnd", 'v', "vwnd file path", true, "");
	a.add<std::string>("output", 'o', "output file path", true, "");

	a.parse_check(argc, argv);

	std::string strVWnd = a.get<std::string>("vwnd");
	std::string strUWnd = a.get<std::string>("uwnd");
	std::string strOutPut = a.get<std::string>("output");


	//vwnd 经向风速 
	//std::string strVWnd = "C:\\Users\\xuzhimin\\Desktop\\vwnd_a.tif";

	//uwnd 纬向风速 
	//std::string strUWnd = "C:\\Users\\xuzhimin\\Desktop\\uwnd_a.tif"; 

	WindField wf(strVWnd, strUWnd);

	int nWidth = wf.Width();
	int nHeight = wf.Height();


	float maxLevel = -FLT_MAX;
	float minLevel = FLT_MAX;

#pragma omp parallel for
	for (int i = 0; i < nHeight; i++)
	{
		for (int j = 0; j < nWidth; j++)
		{
			float level = wf.GetWindLevel(j, i);
			if (level > maxLevel)
				maxLevel = level;
			if (level < minLevel)
				minLevel = level;
		}
	}

	cv::Mat imageNoise(nHeight, nWidth, CV_8UC3);
	cv::Mat imageColor(nHeight, nWidth, CV_8UC3);
	cv::Mat imageField(nHeight, nWidth, CV_8UC3);
	cv::Mat imageBlend(nHeight, nWidth, CV_8UC3);

#pragma omp parallel for
	for (int i = 0; i < imageColor.rows; i++)
	{
		for (int j = 0; j < imageColor.cols; j++)		
		{	
			float level = wf.GetWindLevel(j, i);
			float ratio = level / (maxLevel - minLevel);
			Color color = GetColor(ratio, ColormapType::Heat);

			imageColor.at<cv::Vec3b>(i, j)[0] = color.b() * 255;
			imageColor.at<cv::Vec3b>(i, j)[1] = color.g() * 255;
			imageColor.at<cv::Vec3b>(i, j)[2] = color.r() * 255;


			int rgb = rand()%256;
			imageNoise.at<cv::Vec3b>(i, j)[0] =rgb;
			imageNoise.at<cv::Vec3b>(i, j)[1] =rgb;
			imageNoise.at<cv::Vec3b>(i, j)[2] =rgb;
		}
	}

	//SHOW_IMAGE(imageColor);

	Generate_LIC(imageNoise, imageField, wf);

	addWeighted(imageColor, 0.3, imageField, 0.7, 0, imageBlend);
	//SHOW_IMAGE(imageBlend);

	cv::imwrite(strOutPut, imageBlend);

	return 0;
}
