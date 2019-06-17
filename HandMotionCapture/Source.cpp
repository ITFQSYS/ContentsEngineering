
#define _CRT_SECURE_NO_WARNINGS//OpenCV内部のfopenエラーを黙らせる

//ヘッダファイル
#include <iostream>
#include <fstream>
#include<opencv2\\opencv.hpp>


#ifdef _DEBUG
#pragma comment(lib,"c:\\dev\\opencv-3.4.0\\build\\x64\\vc15\\lib\\opencv_world340d.lib")
#else
#pragma comment(lib,"c:\\dev\\opencv-3.4.0\\build\\x64\\vc15\\lib\\opencv_world340.lib")
#endif // DEBUG

using namespace cv;
using namespace std;

const int c_sholder=0;
const int l_sholder = 1;
const int l_elbow=2;
const int l_hand=3;
const int r_sholder = 4;
const int r_elbow = 5;
const int r_hand = 6;



int height;//画像の高さ


class Writer {

private:

	std::ofstream out;//保存するストリーム


					  //ソートするときに使う構造体
	struct DataPack {

		int index;
		std::vector<double>data;

		bool operator<(const DataPack &right)const {

			return index < right.index;
		}
	};

	std::list<DataPack> all_data;//全データ


	void write() {//書き込み

		all_data.sort();


		for (auto itr = all_data.begin(); itr != all_data.end(); ++itr) {

			out << itr->index;

			for (int i = 0; i < itr->data.size(); i++) {

				out << "," << itr->data[i];
			}

			out << std::endl;
		}
	}

	//http://d.hatena.ne.jp/dew_false/20070726/p1 参照
	bool isExist(const std::string &path) {

		struct stat st;
		int ret;
		ret = stat(path.c_str(), &st);

		if (0 == ret)
		{
			return true;
		}
		else {

			return false;
		}
	}



public:

	Writer() {

	}

	bool open(const std::string &path) {

		//すでにファイルがあるか?
		if (isExist(path)) {

			std::cerr << "ファイルがすでに存在します。" << std::endl;

			return false;
		}

		out.open(path);

		if (!out.is_open()) {
			return false;
		}
	}


	Writer(std::string &path) {

		if (!open(path)) {
			std::cerr << "Failed to open " << path << " !" << std::endl;
		}
	}

	//データの追加
	void insert(int index, const vector<double> &data) {

		DataPack tmp;

		tmp.index = index;
		tmp.data = data;

		//同一のフレームならば上書き
		for (auto itr = all_data.begin(); itr != all_data.end(); itr++) {

			if (itr->index == index) {

				*itr = tmp;
				return;
			}

		}
	}


	~Writer() {
		write();
	}
};

int main(void) {
	
	VideoCapture cap;//ストリーム
	Mat src;//動画のフレームそのまま
	std::string filename;

	//動画を開く
	do {

		std::cout << "File:";


		std::cin >> filename;

		cap.open(filename);

		cap >> src;

	} while (src.empty());

	for (int index = 0;;) {

	}

	return 0;
}
