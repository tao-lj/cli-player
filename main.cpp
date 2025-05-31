#include <opencv2/opencv.hpp>
#include <SDL_mixer.h>
#include <iostream>
#include <fstream>

#ifdef _WIN32

#include <Windows.h>
#include <conio.h>

#elif __linux__

#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>

void setRawMode(bool enable) {
	static struct termios oldt, newt;
	if (enable) {
		tcgetattr(STDIN_FILENO, &oldt);	// 获取当前终端属性
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);  // 关闭行缓冲 & 关闭回显
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	else {
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);  // 恢复原来的终端设置
	}
}

bool _kbhit() {
	struct pollfd pfds[1];
	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;

	return poll(pfds, 1, 0) > 0;
}

int _getch() {
	int ch;
	read(STDIN_FILENO, &ch, 1);

	if (ch == 27) {
		usleep(10000);
		if (!_kbhit()) return 27;

		char seq[2] = { 0 };

		if (read(STDIN_FILENO, &seq[0], 1) == 0) return 27;
		if (read(STDIN_FILENO, &seq[1], 1) == 0) return 27;

		if (seq[0] == '[') {
			switch (seq[1]) {
			case 'A': return 72;
			case 'B': return 80;
			case 'C': return 77;
			case 'D': return 75;
			case 'H': return 71;
			case 'F': return 79;
			default: return 27;
			}
		}

		return 27;
	}

	return ch;
}

void quit(int exitValue);
void get_size();

void signal_hander(int sig) {
	quit(sig);
}

void handle_winch(int sig) {
	get_size();
}

#endif

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
cv::Size outSize;

int videoFPS;
int videoWidth;
int videoHeight;

// 清理资源并退出
void quit(int exitValue) {
#ifdef __linux__
	setRawMode(false);
#endif

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
void get_size() {
#ifdef _WIN32

	// 获取控制台输出缓冲区尺寸
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

	outSize.width = csbi.dwSize.X;
	outSize.height = csbi.dwSize.Y << 1;

	// 获取控制台字符尺寸
	HDC hdc = GetDC(NULL);
	HFONT hFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
	SelectObject(hdc, hFont);
	SIZE size;
	GetTextExtentPoint32(hdc, "A", 1, &size);

	int charWidth = size.cx;
	int charHeight = size.cy >> 1;

	int windowWidth = charWidth * outSize.width;
	int windowHeight = charHeight * outSize.height;
	float aspectRatio = 1.0f * videoWidth / videoHeight;

	if (1.0f * windowWidth / windowHeight >= aspectRatio) {
		outSize.width = 1.0f * windowHeight * aspectRatio / charWidth;
	}
	else {
		outSize.height = 1.0f * windowWidth / aspectRatio / charHeight;
	}

	outSize.width = MIN(outSize.width, videoWidth);
	outSize.height = MIN(outSize.height, videoHeight);

#elif __linux__

	struct winsize size;

	ioctl(STDIN_FILENO, TIOCGWINSZ, &size);

	float bufferWidth = size.ws_col;
	float bufferHeight = size.ws_row << 1;

	bufferWidth = MIN(bufferWidth, videoWidth);
	bufferHeight = MIN(bufferHeight, videoHeight);

	if (bufferWidth / videoWidth > bufferHeight / videoHeight) {
		outSize.height = bufferHeight;
		outSize.width = bufferHeight * videoWidth / videoHeight;
	}
	else {
		outSize.width = bufferWidth;
		outSize.height = bufferHeight * videoHeight / videoWidth;
	}

#endif

	outSize.height &= ~1;
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
	int width = outSize.width, height = outSize.height;

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

	cv::resize(frame, frame, outSize);

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
	if (!colorChanged) fputs(output.c_str(), stdout);

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
#ifdef __linux__
	setRawMode(true);
#endif

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
	get_size();

	// 视频播放主循环
	while (true) {
		handle_keyboard();
		if (isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		}
		else {
			currentTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - pauseTime;
		}

#ifdef _WIN32
		get_size();
#endif

		if (render_frame(lastFrame, lastIdx)) quit(0);
	}
}

// 读取设置文件
int read_config(std::string filename) {
	int missing = 0;
	std::ifstream file(filename);
	if (!file.is_open()) {
		std::cerr << "WARNING: 无法打开配置文件: " << filename << ", 将使用默认配置." << std::endl;
		return -1;
	}

	std::string line;
	std::unordered_map<std::string, std::string> settings;

	// 逐行读取设置文件
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#') continue;

		size_t equalPos = line.find('=');
		if (equalPos != std::string::npos) {
			int i = 0;
			std::string key = "";
			for (; line[i] != ' ' && line[i] != '='; ++i)
				key += line[i];
			for (i = equalPos + 1; line[i] == ' '; ++i);
			std::string value = line.substr(i);
			settings[key] = value;
			std::cout << key << ' ' << value << std::endl;
		}
	}

	// 初始化各项设置
	if (settings.find("ffmpegPath") == settings.end()) {
		std::cerr << "WARNING: 配置文件中缺少 'ffmpegPath' 项，已使用默认值: " << ffmpegPath << std::endl;
		++missing;
	}
	else ffmpegPath = settings["ffmpegPath"];

	if (settings.find("audioName") == settings.end()) {
		std::cerr << "WARNING: 配置文件中缺少 'audioName' 项，已使用默认值: " << audioName << std::endl;
		++missing;
	}
	else audioName = settings["audioName"];

	if (settings.find("eps") == settings.end()) {
		std::cerr << "WARNING: 配置文件中缺少 'eps' 项，已使用默认值: " << eps << std::endl;
		++missing;
	}
	else {
		try {
			eps = std::stod(settings["eps"]);
		}
		catch (const std::invalid_argument& e) {
			std::cerr << "WARNING: 'eps' 转换失败，已使用默认值: " << eps << std::endl;
		}
	}

	if (settings.find("skipInterval") == settings.end()) {
		std::cerr << "WARNING: 配置文件中缺少 'skipInterval' 项，已使用默认值: " << skipInterval << std::endl;
	}
	else {
		try {
			skipInterval = std::stod(settings["skipInterval"]);
		}
		catch (const std::invalid_argument& e) {
			std::cerr << "WARNING: 'skipInterval' 转换失败，已使用默认值: " << skipInterval << std::endl;
		}
	}

	return missing;
}

// 初始化视频/音频文件
int init_resources(std::string videoName) {
#ifdef _WIN32
	// 启用控制台虚拟终端序列
	DWORD dwMode = 0;
	GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwMode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#elif __linux__
	signal(SIGINT, signal_hander);
	signal(SIGWINCH, handle_winch);
#endif

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

int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
#endif

	std::string configPath = "config.txt";
	read_config(configPath);

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
		return -1;
	}

	play();

	return 0;
}
