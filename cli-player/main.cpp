#include <opencv2/opencv.hpp>
#include <SDL.h>
#include <windows.h>
#include <conio.h>
#include <fstream>

#define SPACE	32
#define ESC	27

#undef main
#undef max

using namespace cv;
using namespace std;
using namespace chrono;

double exponential = 2.7;		// 控制丢帧时阈值的指数常量
string ffmpegPath = "ffmpeg";		// ffmpeg路径
string tmpAudioFile = "TmpAudio.wav";	// 临时音频文件名称

VideoCapture video;

SDL_AudioDeviceID deviceId;	// 音频设备 ID
SDL_AudioSpec wavSpec;		// 音频规格
Uint8* wavBuffer;		// 音频缓冲区
Uint32 wavLength;		// 音频长度

bool isPaused = false;
double currentTime, pausedTime;


// 获取设置文件
void get_cfg() {
	ifstream cfg("config.txt");
	string str;

	// 若设置文件不存在则创建
	if (!cfg.is_open()) {
		cout << "Warning: 无法打开配置文件config.txt" << endl;
		ofstream fp("config.txt");
		fp << "ffmpegPath=" << ffmpegPath << endl;
		fp << "exponential=" << exponential;
		return;
	}

	// 逐行获取设置选项
	while (getline(cfg, str)) {
		auto a = str.find("=");
		if (a == string::npos) continue;
		string key = str.substr(0, a);
		string value = str.substr(a + 1, str.size() - 1);

		if (key == "ffmpegPath") ffmpegPath = value;
		else if (key == "exponential") exponential = stod(value);
	}
}


// 初始化控制台与SDL2
void init() {
	// 启用控制台虚拟终端序列
	DWORD dwMode = 0;
	GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwMode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	// 初始化 SDL2 用于播放音频
	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		cerr << "ERROR: SDL 初始化失败: " << SDL_GetError() << endl;
		exit(1);
	}
}


// 获取视频/音频文件
void get_file(int argc, char* argv[]) {
	// 获取视频文件路径
	string videoName = "";
	if (argc < 2) {
		cout << "请输入视频路径：";
		getline(cin, videoName);
	}
	else videoName += argv[1];

	if (videoName[0] == '\"') {
		videoName.erase(videoName.end() - 1);
		videoName.erase(videoName.begin());
	}

	// 打开视频文件
	if (!video.open(videoName)) {
		cerr << "ERROR: 无法打开视频文件" << endl;
		exit(1);
	}

	// 提取音频并保存为 WAV 文件
	string command = ffmpegPath + " -i \"" + videoName + "\" -f wav -vn " + tmpAudioFile;
	if (system(command.c_str()) != 0) { // 检查 ffmpeg 命令是否执行成功
		cerr << "ERROR: 音频提取失败" << endl;
		video.release();
		exit(1);
	}
}


// 播放音频
void play_audio() {
	// 加载音频文件到缓冲区
	if (SDL_LoadWAV(tmpAudioFile.c_str(), &wavSpec, &wavBuffer, &wavLength) == NULL) {
		cerr << "ERROR: 无法加载音频文件: " << SDL_GetError() << endl;
		SDL_Quit();
		exit(1);
	}

	// 打开音频设备
	deviceId = SDL_OpenAudioDevice(NULL, 0, &wavSpec, NULL, 0);
	if (deviceId == 0) {
		cerr << "ERROR: 无法打开音频设备: " << SDL_GetError() << endl;
		SDL_FreeWAV(wavBuffer);
		SDL_Quit();
		exit(1);
	}

	// 开始播放音频
	SDL_QueueAudio(deviceId, wavBuffer, wavLength);
	SDL_PauseAudioDevice(deviceId, 0);
}


// 停止音频并清理资源
void stop_audio() {
	SDL_CloseAudioDevice(deviceId);
	SDL_FreeWAV(wavBuffer);
	remove(tmpAudioFile.c_str());
}


// 暂停/播放事件处理
void pause_audio() {
	isPaused = !isPaused;
	SDL_PauseAudioDevice(deviceId, isPaused ? 1 : 0);
}


// 计算字符画尺寸
void get_size(int& width, int& height) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	width = csbi.srWindow.Right - csbi.srWindow.Left << 1;
	height = csbi.srWindow.Bottom - csbi.srWindow.Top << 1;

	double aspectRatio = (double)csbi.dwSize.X / width / csbi.dwSize.Y * height;

	// 调整宽高比
	if ((double)video.get(CAP_PROP_FRAME_WIDTH) / width > aspectRatio * video.get(CAP_PROP_FRAME_HEIGHT) / height) {
		height = aspectRatio * width / video.get(CAP_PROP_FRAME_WIDTH) * video.get(CAP_PROP_FRAME_HEIGHT);
		height = height >> 1 << 1; // 确保高度为偶数
	}
	else {
		width = (double)height / video.get(CAP_PROP_FRAME_HEIGHT) * video.get(CAP_PROP_FRAME_WIDTH) / aspectRatio;
	}
}


// 清理资源并退出
void stop_and_exit() {
	stop_audio();
	puts("\33[0m\n\33[?25h\n\33c");
	video.release();
	SDL_Quit();
	exit(0);
}


// 键盘事件处理
void handle_keyboard() {
	if (_kbhit()) {
		char ch = _getch();
		switch (ch) {
		case SPACE:
			pause_audio();
			break;
		case ESC:
			stop_and_exit();
		default:
			break;
		}
	}
}


void play() {
	int width, height, d, lost = 0, totalFrames = video.get(CAP_PROP_FRAME_COUNT);
	Mat img;
	Vec3b color, bgColor = { 0, 0, 0 }, txtColor = { 0, 0, 0 };
	auto startTime = high_resolution_clock::now();
	string output;

	// 判断颜色差异是否超过阈值，超过则返回 true
	auto f = [&d](Vec3b x, Vec3b y) -> bool {
		return max({ abs(x[0] - y[0]), abs(x[1] - y[1]), abs(x[2] - y[2]) }) > d;
		};

	// 获取当前播放时间并将其转换为视频帧数
	auto getTime = [&]() -> double {
		currentTime = (duration<double>(high_resolution_clock::now() - startTime).count() - pausedTime);
		return currentTime * video.get(CAP_PROP_FPS);
		};

	puts("\33c\n\33[?25l\n\33[30;40m"); // 清屏并隐藏光标
	play_audio();

	// 视频播放主循环
	for (int idx = 0; idx < totalFrames; ++idx) {
		handle_keyboard();
		while (isPaused) {
			SDL_Delay(50);
			handle_keyboard();
			pausedTime = duration<double>(high_resolution_clock::now() - startTime).count() - currentTime;
		}

		Mat tmp;
		double current = getTime();
		if (!video.read(tmp)) break;

		if (current > idx) { // 跳过丢失的帧
			++lost;
			continue;
		}
		while (current < idx) current = getTime(); // 等待同步

		get_size(width, height);

		Mat frame;
		bool firstFrame = 1, colorChanged = 1;
		d = pow(lost, exponential); // 根据丢帧数调整颜色差异阈值
		lost = 0;
		output.clear();
		resize(tmp, frame, { width, height }); // 调整图像尺寸

		// 如果尺寸变化则清屏
		if (img.cols != width || img.rows != height) {
			img = Mat(width, height, CV_8UC1, Scalar(0, 0, 0));
			puts("\33c");
		}

		for (int i = 0; i < height; i += 2) {
			for (int j = 0; j < width; ++j) {
				if (!f(frame.at<Vec3b>(i, j), img.at<Vec3b>(i, j)) && !f(frame.at<Vec3b>(i + 1, j), img.at<Vec3b>(i + 1, j))) {
					firstFrame = 1;
					continue; // 如果屏幕上颜色和图像颜色相同则跳过输出
				}
				if (firstFrame) {
					firstFrame = colorChanged = 0;
					output += "\33[" + to_string(i / 2 + 1) + ';' + to_string(j + 1) + 'H'; // 移动光标
				}
				color = frame.at<Vec3b>(i, j);
				if (f(color, bgColor)) { // 改变背景颜色
					output += "\33[48;2;" + to_string(color[2]) + ';' + to_string(color[1]) + ';' + to_string(color[0]) + 'm';
					bgColor = color;
				}
				color = frame.at<Vec3b>(i + 1, j);
				if (f(color, txtColor)) { // 改变文本颜色
					output += "\33[38;2;" + to_string(color[2]) + ';' + to_string(color[1]) + ';' + to_string(color[0]) + 'm';
					txtColor = color;
				}
				output += "▄"; // 输出字符
			}
			if (!firstFrame && i + 2 < height) output += '\n';
		}
		if (!colorChanged) fputs(output.c_str(), stdout); // 打印输出
		img = frame; // 更新图像
	}

	return;
}


int main(int argc, char* argv[]) {
	get_cfg();
	init();
	get_file(argc, argv);
	play();
	stop_and_exit();
}