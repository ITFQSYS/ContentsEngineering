#define _CRT_SECURE_NO_WARNINGS//OpenCV内部のfopenエラーを黙らせる

//ヘッダファイル
#include <iostream>
#include <fstream>
#include<opencv2\\opencv.hpp>


#ifdef _DEBUG
#pragma comment(lib,"c:\\dev\\opencv-3.2.0\\build\\x64\\vc14\\lib\\opencv_world320d.lib")
#else
#pragma comment(lib,"c:\\dev\\opencv-3.2.0\\build\\x64\\vc14\\lib\\opencv_world320.lib")
#endif // DEBUG

//Kinect SDKに含まれるヘッダ
#include <windows.h>
#include <NuiApi.h>
#include <NuiSkeleton.h>

#include<chrono>

using namespace std;
using namespace cv;

//処理時間をmsec単位で計測するクラス
class Clock {
private:
	std::chrono::time_point<std::chrono::system_clock> start_time;//開始時刻
public:
	//スタート 計測中なら0からリセット
	void start() {
		start_time = std::chrono::system_clock::now();      // 計測スタート時刻を保存
	}

	//経過時間
	double elapsed() {

		auto end = std::chrono::system_clock::now();       // 計測終了時刻を保存
		auto dur = end - start_time;        // 要した時間を計算
		auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();

		return double(msec);
	}

	Clock() {
		start();
	}

	~Clock() {

	}
};

//∠abcを求める
double angle(const Point &a, const Point &b,const Point &c) {

	Point alpha = a - b;
	Point beta = c - b;

	double  rad = acos(alpha.dot(beta) / sqrt(alpha.dot(alpha)*beta.dot(beta)));

	return rad / CV_PI * 180;
}

int main(void) {

	// Kinectのインスタンス生成、初期化
	INuiSensor* pSensor;
	HRESULT hResult = S_OK;
	hResult = NuiCreateSensorByIndex(0, &pSensor);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiCreateSensorByIndex" << std::endl;
		exit(1);
	}

	hResult = pSensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_SKELETON);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiInitialize" << std::endl;
		exit(1);
	}

	// Colorストリーム
	HANDLE hColorEvent = INVALID_HANDLE_VALUE;
	HANDLE hColorHandle = INVALID_HANDLE_VALUE;
	hColorEvent = CreateEvent(nullptr, true, false, nullptr);
	hResult = pSensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, 0, 2, hColorEvent, &hColorHandle);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiImageStreamOpen( COLOR )" << std::endl;
		exit(1);
	}

	// Depth&Playerストリーム
	HANDLE hDepthPlayerEvent = INVALID_HANDLE_VALUE;
	HANDLE hDepthPlayerHandle = INVALID_HANDLE_VALUE;
	hDepthPlayerEvent = CreateEvent(nullptr, true, false, nullptr);
	hResult = pSensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, NUI_IMAGE_RESOLUTION_640x480, 0, 2, hDepthPlayerEvent, &hDepthPlayerHandle);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiImageStreamOpen( DEPTH&PLAYER )" << std::endl;
		exit(1);
	}

	// Skeletonストリーム
	HANDLE hSkeletonEvent = INVALID_HANDLE_VALUE;
	hSkeletonEvent = CreateEvent(nullptr, true, false, nullptr);
	hResult = pSensor->NuiSkeletonTrackingEnable(hSkeletonEvent, 0);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiSkeletonTrackingEnable" << std::endl;
		exit(1);
	}

	// 解像度の取得
	unsigned long refWidth = 0;
	unsigned long refHeight = 0;
	NuiImageResolutionToSize(NUI_IMAGE_RESOLUTION_640x480, refWidth, refHeight);
	int width = static_cast<int>(refWidth);
	int height = static_cast<int>(refHeight);

	// 位置合わせの設定
	INuiCoordinateMapper* pCordinateMapper;
	hResult = pSensor->NuiGetCoordinateMapper(&pCordinateMapper);
	if (FAILED(hResult)) {
		std::cerr << "Error : NuiGetCoordinateMapper" << std::endl;
		exit(1);
	}
	std::vector<NUI_COLOR_IMAGE_POINT> pColorPoint(width * height);

	HANDLE hEvents[3] = { hColorEvent, hDepthPlayerEvent, hSkeletonEvent };

	// カラーテーブル
	cv::Vec3b color[7];
	color[0] = cv::Vec3b(0, 0, 0);
	color[1] = cv::Vec3b(255, 0, 0);
	color[2] = cv::Vec3b(0, 255, 0);
	color[3] = cv::Vec3b(0, 0, 255);
	color[4] = cv::Vec3b(255, 255, 0);
	color[5] = cv::Vec3b(255, 0, 255);
	color[6] = cv::Vec3b(0, 255, 255);


	Clock clock;

	int num_correct = 0;
	int num_incorrect = 0;

	while (1) {
		// フレームの更新待ち
		ResetEvent(hColorEvent);
		ResetEvent(hDepthPlayerEvent);
		ResetEvent(hSkeletonEvent);
		WaitForMultipleObjects(ARRAYSIZE(hEvents), hEvents, true, INFINITE);

		// Colorカメラからフレームを取得
		NUI_IMAGE_FRAME colorImageFrame = { 0 };
		hResult = pSensor->NuiImageStreamGetNextFrame(hColorHandle, 0, &colorImageFrame);
		if (FAILED(hResult)) {
			std::cerr << "Error : NuiImageStreamGetNextFrame( COLOR )" << std::endl;
			exit(1);
		}

		// Color画像データの取得
		INuiFrameTexture* pColorFrameTexture = colorImageFrame.pFrameTexture;
		NUI_LOCKED_RECT colorLockedRect;
		pColorFrameTexture->LockRect(0, &colorLockedRect, nullptr, 0);

		// Depthセンサーからフレームを取得
		NUI_IMAGE_FRAME depthPlayerImageFrame = { 0 };
		hResult = pSensor->NuiImageStreamGetNextFrame(hDepthPlayerHandle, 0, &depthPlayerImageFrame);
		if (FAILED(hResult)) {
			std::cerr << "Error : NuiImageStreamGetNextFrame( DEPTH&PLAYER )" << std::endl;
			exit(1);
		}

		// Depth&Playerデータの取得
		BOOL nearMode = false;
		INuiFrameTexture* pDepthPlayerFrameTexture = nullptr;
		pSensor->NuiImageFrameGetDepthImagePixelFrameTexture(hDepthPlayerHandle, &depthPlayerImageFrame, &nearMode, &pDepthPlayerFrameTexture);
		NUI_LOCKED_RECT depthPlayerLockedRect;
		pDepthPlayerFrameTexture->LockRect(0, &depthPlayerLockedRect, nullptr, 0);

		// Skeletonフレームを取得
		NUI_SKELETON_FRAME skeletonFrame = { 0 };
		hResult = pSensor->NuiSkeletonGetNextFrame(0, &skeletonFrame);
		if (FAILED(hResult)) {
			std::cout << "Error : NuiSkeletonGetNextFrame" << std::endl;
			exit(1);
		}

		/*
		// スムージング
		NUI_TRANSFORM_SMOOTH_PARAMETERS smoothParameter;
		smoothParameter.fSmoothing = 0.5; // 平滑化[0.0f-1.0f]
		smoothParameter.fCorrection = 0.5; // 補正量[0.0f-1.0f]
		smoothParameter.fPrediction = 0.0f; // 予測フレーム数[0.0f-]
		smoothParameter.fJitterRadius = 0.05f; // ジッタ抑制半径[0.0f-]
		smoothParameter.fMaxDeviationRadius = 0.04f; // 最大抑制範囲[0.0f-]

		hResult = NuiTransformSmooth( &skeletonFrame, &smoothParameter );
		*/

		// 表示
		cv::Mat colorMat(height, width, CV_8UC4, reinterpret_cast<unsigned char*>(colorLockedRect.pBits));

		cv::Mat bufferMat = cv::Mat::zeros(height, width, CV_16UC1);
		cv::Mat playerMat = cv::Mat::zeros(height, width, CV_8UC3);
		NUI_DEPTH_IMAGE_PIXEL* pDepthPlayerPixel = reinterpret_cast<NUI_DEPTH_IMAGE_PIXEL*>(depthPlayerLockedRect.pBits);
		pCordinateMapper->MapDepthFrameToColorFrame(NUI_IMAGE_RESOLUTION_640x480, width * height, pDepthPlayerPixel, NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, width * height, &pColorPoint[0]);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned int index = y * width + x;
				bufferMat.at<unsigned short>(pColorPoint[index].y, pColorPoint[index].x) = pDepthPlayerPixel[index].depth;
				playerMat.at<cv::Vec3b>(pColorPoint[index].y, pColorPoint[index].x) = color[pDepthPlayerPixel[index].playerIndex];
			}
		}
		cv::Mat depthMat(height, width, CV_8UC1);
		bufferMat.convertTo(depthMat, CV_8U, -255.0f / 10000.0f, 255.0f);

		cv::Mat skeletonMat = cv::Mat::zeros(height, width, CV_8UC3);
		NUI_COLOR_IMAGE_POINT colorPoint;

		Point joints[NUI_SKELETON_POSITION_COUNT];//関節の画面上の座標


		for (int count = 0; count < NUI_SKELETON_COUNT; count++) {
			NUI_SKELETON_DATA skeletonData = skeletonFrame.SkeletonData[count];
			if (skeletonData.eTrackingState == NUI_SKELETON_TRACKED) {
				for (int position = 0; position < NUI_SKELETON_POSITION_COUNT; position++) {
					pCordinateMapper->MapSkeletonPointToColorPoint(&skeletonData.SkeletonPositions[position], NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, &colorPoint);
					if ((colorPoint.x >= 0) && (colorPoint.x < width) && (colorPoint.y >= 0) && (colorPoint.y < height)) {

						cv::circle(colorMat, cv::Point(colorPoint.x, colorPoint.y), 10, Scalar(0,255,255), -1, CV_AA);

						joints[position].x = colorPoint.x;
						joints[position].y = colorPoint.y;
					}
				}

				/*std::stringstream ss;
				ss << skeletonData.SkeletonPositions[NUI_SKELETON_POSITION_HIP_CENTER].z;
				pCordinateMapper->MapSkeletonPointToColorPoint(&skeletonData.SkeletonPositions[NUI_SKELETON_POSITION_HEAD], NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, &colorPoint);
				if ((colorPoint.x >= 0) && (colorPoint.x < width) && (colorPoint.y >= 0) && (colorPoint.y < height)) {

					cv::putText(colorMat, ss.str(), cv::Point(colorPoint.x - 50, colorPoint.y - 20), cv::FONT_HERSHEY_SIMPLEX, 1.5f, static_cast<cv::Scalar>(color[count + 1]));
				}*/
			}
			/*else if (skeletonData.eTrackingState == NUI_SKELETON_POSITION_ONLY) {
				pCordinateMapper->MapSkeletonPointToColorPoint(&skeletonData.SkeletonPositions[NUI_SKELETON_POSITION_HIP_CENTER], NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, &colorPoint);
				if ((colorPoint.x >= 0) && (colorPoint.x < width) && (colorPoint.y >= 0) && (colorPoint.y < height)) {
					cv::circle(colorMat, cv::Point(colorPoint.x, colorPoint.y), 10, static_cast<cv::Scalar>(color[count + 1]), -1, CV_AA);
				}
			}*/
		}

		vector<double> angles(4);
		
		//角度を計算
		angles[0] = angle(joints[NUI_SKELETON_POSITION_HAND_LEFT], joints[NUI_SKELETON_POSITION_ELBOW_LEFT], joints[NUI_SKELETON_POSITION_SHOULDER_LEFT]);//左肘
		angles[1] = angle(joints[NUI_SKELETON_POSITION_ELBOW_LEFT], joints[NUI_SKELETON_POSITION_SHOULDER_LEFT], joints[NUI_SKELETON_POSITION_SHOULDER_CENTER]);//左肩
		angles[2] = angle(joints[NUI_SKELETON_POSITION_HAND_RIGHT], joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT]);//右肘
		angles[3] = angle(joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT], joints[NUI_SKELETON_POSITION_SHOULDER_CENTER]);//右肩
		

		putText(colorMat, to_string(round(angles[0])), joints[NUI_SKELETON_POSITION_ELBOW_LEFT], cv::FONT_HERSHEY_PLAIN, 2,  Scalar(0,255,255));
		//putText(colorMat, to_string(round(angles[1])), joints[NUI_SKELETON_POSITION_SHOULDER_LEFT], cv::FONT_HERSHEY_PLAIN, 2,  Scalar(0,255,255));
		//putText(colorMat, to_string(round(angles[2])), joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], cv::FONT_HERSHEY_PLAIN, 2,  Scalar(0,255,255));
		//putText(colorMat, to_string(round(angles[3])), joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT], cv::FONT_HERSHEY_PLAIN, 2,  Scalar(0,255,255));
		
		//line(colorMat, joints[NUI_SKELETON_POSITION_HAND_LEFT], joints[NUI_SKELETON_POSITION_ELBOW_LEFT], Scalar(0, 255, 0), 2);
		//line(colorMat, joints[NUI_SKELETON_POSITION_ELBOW_LEFT], joints[NUI_SKELETON_POSITION_SHOULDER_LEFT], Scalar(0, 255, 0), 2);
		//line(colorMat, joints[NUI_SKELETON_POSITION_SHOULDER_LEFT], joints[NUI_SKELETON_POSITION_SHOULDER_CENTER], Scalar(0, 255, 0), 2);

		//line(colorMat, joints[NUI_SKELETON_POSITION_HAND_RIGHT], joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], Scalar(0, 255, 0), 2);
		//line(colorMat, joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT], Scalar(0, 255, 0), 2);
		//line(colorMat, joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT], joints[NUI_SKELETON_POSITION_SHOULDER_CENTER], Scalar(0, 255, 0), 2);


		//お手本の表示
		vector<double> correct_angle(4);
		

		//右肘の角度を計算
		correct_angle[0] = 0;
		correct_angle[1] = 180;
		correct_angle[2] = 90;
		correct_angle[3] = 180;
		Point correct_vec;//お手本の角度


		double large_t = 750;
		double t = (int)clock.elapsed() %(int)(large_t*2);

		if (t < large_t) {
			correct_angle[0] = 180 - 135 / large_t * t;
		}
		else {
			correct_angle[0] = 45 + 135 / large_t * (t-large_t);
		}

		double correct_ang_rad = CV_PI / 180 * correct_angle[0];

		cout << correct_angle[0] << endl;

		for (int i = 0; i < 4; i++) {
			if (abs(correct_angle[i] - angles[i]) / correct_angle[i] > .3) {
				num_incorrect++;
				switch (i)
				{
				case 0:
					circle(colorMat, joints[NUI_SKELETON_POSITION_ELBOW_LEFT], 10, Scalar(0, 0, 255), -1);
					break;
				/*case 1:
					circle(colorMat, joints[NUI_SKELETON_POSITION_SHOULDER_LEFT], 10, Scalar(0, 0, 255), -1);
					break;
				case 2:
					circle(colorMat, joints[NUI_SKELETON_POSITION_ELBOW_RIGHT], 10, Scalar(0, 0, 255), -1);
					break;
				case 3:
					circle(colorMat, joints[NUI_SKELETON_POSITION_SHOULDER_RIGHT], 10, Scalar(0, 0, 255), -1);
					break;*/
				default:
					break;
				}
			}
			else {
				if (i == 0) {
					num_correct++;
				}
			}
		}
		double l = sqrt((joints[NUI_SKELETON_POSITION_HAND_LEFT] - joints[NUI_SKELETON_POSITION_ELBOW_LEFT]).dot(joints[NUI_SKELETON_POSITION_HAND_LEFT] - joints[NUI_SKELETON_POSITION_ELBOW_LEFT]));

		Point vec_src = joints[NUI_SKELETON_POSITION_ELBOW_LEFT] - joints[NUI_SKELETON_POSITION_SHOULDER_LEFT];

		Point vec_dst = -2*
			Point(
				round((double)vec_src.x*cos(correct_ang_rad)+(double)vec_src.y*sin(correct_ang_rad)),
				round(-(double)vec_src.x*sin(correct_ang_rad) + (double)vec_src.y*cos(correct_ang_rad))
				);

		line(colorMat, joints[NUI_SKELETON_POSITION_ELBOW_LEFT], joints[NUI_SKELETON_POSITION_ELBOW_LEFT] + vec_dst, Scalar(0, 255, 0),5,CV_AA);


		Mat show;
		resize(colorMat, show, Size(), 2, 2);

		cv::imshow("dst", show);
		//cv::imshow("Depth", depthMat);
		//cv::imshow("Player", playerMat);
		//cv::imshow("Skeleton", skeletonMat);


		// フレームの解放
		pColorFrameTexture->UnlockRect(0);
		pDepthPlayerFrameTexture->UnlockRect(0);
		pSensor->NuiImageStreamReleaseFrame(hColorHandle, &colorImageFrame);
		pSensor->NuiImageStreamReleaseFrame(hDepthPlayerHandle, &depthPlayerImageFrame);

		// ループの終了判定(Escキー)
		if (cv::waitKey(30) == VK_ESCAPE) {
			break;
		}
	}

	// Kinectの終了処理
	pSensor->NuiShutdown();
	pSensor->NuiSkeletonTrackingDisable();
	pCordinateMapper->Release();
	CloseHandle(hColorEvent);
	CloseHandle(hDepthPlayerEvent);
	CloseHandle(hSkeletonEvent);


	Mat dst = Mat(480, 640, CV_8UC3,Scalar(255,255,255));

	putText(dst, to_string(((double)num_correct*100.0 / (double)(num_correct + num_incorrect))), Point(50, 250), CV_FONT_HERSHEY_COMPLEX,4,Scalar(0,0,0),5);


	imshow("dst", dst);
	waitKey(0);

	cv::destroyAllWindows();




	return 0;
}
