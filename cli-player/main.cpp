#include <opencv2/opencv.hpp>
#include <SDL_mixer.h>
#include <Windows.h>
#include <conio.h>
#include <iostream>

const int PAUSE_KEY = 32;
const int ESC_KEY = 27;
const int FORWARD_KEY = 77;
const int BACKWARD_KEY = 75;

double eps = 2.7;

double skipInterval = 5.0;

std::string ffmpegPath = "ffmpeg";
std::string audioName = "tmp.mp3";

Mix_Music* music;

bool isPaused = false;
double currentTime;
double pauseTime;
std::chrono::system_clock::time_point startTime;

cv::VideoCapture video;

int videoFPS;
int videoWidth;
int videoHeight;

// 初始化视频/音频文件
int init_resources(std::string videoName) {
	// 启用控制台虚拟终端序列
	DWORD dwMode = 0;
	GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwMode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	// 打开视频文件
	if (!video.open(videoName)) {
		std::cerr << "ERROR: 无法打开视频文件" << std::endl;
		return -1;
	}

	// 提取音频文件
	std::string command = ffmpegPath + " -i \"" + videoName + "\" -f mp3 -vn " + audioName;
	if (system(command.c_str()) != 0) { // 检查 ffmpeg 命令是否执行成功
		std::cerr << "ERROR: 无法提取音频文件" << std::endl;
		video.release();
		return -1;
	}

	// 初始化SDL_mixer
	if (Mix_Init(MIX_INIT_MP3) < 0) {
		std::cerr << "ERROR：无法初始化SDL_mixer:" << Mix_GetError() << std::endl;
		video.release();
		return -1;
	}

	// 开启音频设备
	if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
		std::cerr << "ERROR: 无法开启音频设备:" << Mix_GetError() << std::endl;
		video.release();
		Mix_Quit();
		return -1;
	}

	//加载音频文件
	music = Mix_LoadMUS(audioName.c_str());
	if (music == NULL) {
		std::cerr << "ERROR: 无法加载音频文件:" << Mix_GetError() << std::endl;
		video.release();
		Mix_CloseAudio();
		Mix_Quit();
		return -1;
	}

	// 保存视频信息
	videoFPS = video.get(cv::CAP_PROP_FPS);
	videoWidth = video.get(cv::CAP_PROP_FRAME_WIDTH);
	videoHeight = video.get(cv::CAP_PROP_FRAME_HEIGHT);

	return 0;
}

// 清理资源并退出
void quit(int exitValue) {
	video.release();
	Mix_HaltMusic();
	Mix_FreeMusic(music);
	Mix_CloseAudio();
	Mix_Quit();
	remove(audioName.c_str());
	puts("\33[0m\n\33[?25h"); // 恢复控制台设置
	exit(exitValue);
}

// 计算字符画尺寸
void get_size(int& width, int& height) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	width = (csbi.srWindow.Right - csbi.srWindow.Left) << 1;
	height = (csbi.srWindow.Bottom - csbi.srWindow.Top) << 1;

	double aspectRatio = (double)csbi.dwSize.X / width / csbi.dwSize.Y * height;

	// 调整宽高比
	if ((double)videoWidth / width > aspectRatio * videoHeight / height) {
		height = aspectRatio * width / videoWidth * videoHeight;
		height = MIN(height, videoHeight);
		height &= ~1; // 确保高度为偶数
	}
	else {
		width = (double)height / videoHeight * videoWidth / aspectRatio;
		width = MIN(width, videoWidth);
	}
}

// 计算颜色差异是否超过阈值
bool cmp(cv::Vec3b x, cv::Vec3b y, int threshold) {
	int dr = x[2] - y[2];
	int dg = x[1] - y[1];
	int db = x[0] - y[0];
	int distSq = dr * dr + dg * dg + db * db;
	return distSq > threshold * threshold;
}

// 打印当前帧
int render_frame(cv::Mat& lastFrame, unsigned long long& lastIdx) {
	// 获取当前时间所对应的帧的编号
	unsigned long long idx = videoFPS * currentTime;

	if (idx < 0 || idx >= video.get(cv::CAP_PROP_FRAME_COUNT)) return 1;
	if (lastIdx == idx && lastIdx && !isPaused) return 0;

	cv::Mat frame;
	cv::Vec3b bgColor = { 0, 0, 0 };	// 背景颜色
	cv::Vec3b txtColor = { 0, 0, 0 };	// 文本颜色
	std::string output = "";
	bool isOpened = true;
	bool firstFrame = true;
	bool colorChanged = true;
	long long lost = idx - lastIdx;
	int threshold = MIN(pow(lost, eps), 128); // 根据丢帧数调整颜色差异判断阈值
	int width, height;
	get_size(width, height);

	// 获取当前帧
	if (lost > 0) {
		for (int i = 0; i < lost; ++i) {
			if (!video.grab()) {
				puts("\33[0m\n\33[?25h"); // 恢复控制台设置
				std::cerr << "ERROR: 读取视频时出错" << std::endl;
				quit(-1);
			}
		}
		isOpened = video.retrieve(frame);
	}
	else {
		video.set(cv::CAP_PROP_POS_FRAMES, idx);
		isOpened = video.read(frame);
	}

	if (!isOpened) {
		puts("\33[0m\n\33[?25h");
		std::cerr << "ERROR: 解码视频时出错" << std::endl;
		quit(-1);
	}

	cv::resize(frame, frame, { width, height });

	// 如果尺寸改变则清屏
	if (lastFrame.cols != width || lastFrame.rows != height) {
		lastFrame = cv::Mat(width, height, CV_8UC1, cv::Scalar(0, 0, 0));
		puts("\33c");
	}

	// 生成字符画
	for (int i = 0; i < height; i += 2) {
		for (int j = 0; j < width; ++j) {
			cv::Vec3b nbgColor = frame.at<cv::Vec3b>(i, j);
			cv::Vec3b ntxtColor = frame.at<cv::Vec3b>(i + 1, j);

			if (!cmp(nbgColor, lastFrame.at<cv::Vec3b>(i, j), threshold) &&
				!cmp(ntxtColor, lastFrame.at<cv::Vec3b>(i + 1, j), threshold)) { // 颜色未改变则跳过
				frame.at<cv::Vec3b>(i, j) = lastFrame.at<cv::Vec3b>(i, j);
				frame.at<cv::Vec3b>(i + 1, j) = lastFrame.at<cv::Vec3b>(i + 1, j);
				firstFrame = true;
				continue;
			}

			if (firstFrame) {
				firstFrame = colorChanged = false;
				output += "\33[" + std::to_string(i / 2 + 1) + ';' + std::to_string(j + 1) + 'H'; // 移动光标
			}

			if (cmp(nbgColor, bgColor, threshold)) { // 改变背景颜色
				output += "\33[48;2;" + std::to_string(nbgColor[2]) + ';'
					+ std::to_string(nbgColor[1]) + ';'
					+ std::to_string(nbgColor[0]) + 'm';
				bgColor = nbgColor;
			}
			else {
				frame.at<cv::Vec3b>(i, j) = bgColor;
			}

			if (cmp(ntxtColor, txtColor, threshold)) { // 改变文本颜色
				output += "\33[38;2;" + std::to_string(ntxtColor[2]) + ';'
					+ std::to_string(ntxtColor[1]) + ';'
					+ std::to_string(ntxtColor[0]) + 'm';
				txtColor = ntxtColor;
			}
			else {
				frame.at<cv::Vec3b>(i + 1, j) = txtColor;
			}
			output += "▄";
		}
		if (!firstFrame && i + 2 < height) output += '\n';
	}

	// 打印字符画
	if (!colorChanged) puts(output.c_str());

	lastFrame = frame;
	lastIdx = idx;
	return 0;
}

// 键盘事件处理
void handle_keyboard() {
	if (!_kbhit()) return;

	int ch = _getch();

	switch (ch) {
	case PAUSE_KEY:		// 暂停或恢复播放
		isPaused = !isPaused;
		if (isPaused) Mix_PauseMusic();
		else Mix_ResumeMusic();
		break;
	case ESC_KEY:		// 退出播放
		quit(0);
		break;
	case FORWARD_KEY:	// 快进视频
		currentTime += skipInterval;
		if (currentTime < Mix_MusicDuration(music)) {
			Mix_SetMusicPosition(currentTime);
		}
		else {
			quit(0);
		}
		if (!isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		}
		break;
	case BACKWARD_KEY:	// 快退视频
		currentTime -= skipInterval;
		if (currentTime > 0) {
			Mix_SetMusicPosition(currentTime);
		}
		else {
			currentTime = 0;
			Mix_SetMusicPosition(currentTime);
		}
		if (!isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		}
		break;
	default:
		break;
	}
}

// 视频播放主函数
void play() {
	cv::Mat lastFrame = cv::Mat(1, 1, CV_8UC1, cv::Scalar(0, 0, 0));
	unsigned long long lastIdx = 0;

	// 清屏并隐藏光标
	puts("\33c\n\33[?25l\n\33[30;40m");

	// 播放音乐
	if (Mix_PlayMusic(music, -1) == -1) {
		puts("\33[0m\n\33[?25h");
		printf("ERROR: 无法播放音频 %s\n", Mix_GetError());
		quit(-1);
	}

	startTime = std::chrono::system_clock::now();

	// 视频播放主循环
	while (true) {
		handle_keyboard();
		if (isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		}
		else {
			currentTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - pauseTime;
		}
		if (render_frame(lastFrame, lastIdx)) quit(0);
	}
}

int main(int argc, char* argv[]) {
	std::string videoName = "";
	// 获取视频文件名称
	if (argc < 2) {
		std::cout << "请输入视频路径：";
		getline(std::cin, videoName);
	}
	else {
		videoName += argv[1];
	}

	if (videoName[0] == '\"') {
		videoName.erase(videoName.end() - 1);
		videoName.erase(videoName.begin());
	}

	if (init_resources(videoName)) {
		system("pause");
		return -1;
	}

	play();

	return 0;
}

