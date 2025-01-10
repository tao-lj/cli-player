#include <opencv2/opencv.hpp>
#include <SDL_mixer.h>
#include <Windows.h>
#include <conio.h>
#include <iostream>

const int PAUSE_KEY	= 32;
const int ESC_KEY	= 27;
const int FORWARD_KEY	= 77;
const int BACKWARD_KEY	= 75;

double eps = 2.7;

double skipInterval = 5.0;

std::string ffmpegPath = "ffmpeg";
std::string audioName = "tmp.mp3";

Mix_Music *music;

bool isPaused = false;
double currentTime;
double pauseTime;
std::chrono::system_clock::time_point startTime;

cv::VideoCapture video;

int videoFPS;
int videoWidth;
int videoHeight;

// ��ʼ����Ƶ/��Ƶ�ļ�
int initialize_resources(std::string videoName) {
	// ���ÿ���̨�����ն�����
	DWORD dwMode = 0;
	GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwMode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	// ����Ƶ�ļ�
	if (!video.open(videoName)) {
		std::cerr << "ERROR: �޷�����Ƶ�ļ�" << std::endl;
		return -1;
	}

	// ��ȡ��Ƶ�ļ�
	std::string command = ffmpegPath + " -i \"" + videoName + "\" -f mp3 -vn " + audioName;
	if (system(command.c_str()) != 0) { // ��� ffmpeg �����Ƿ�ִ�гɹ�
		std::cerr << "ERROR: �޷���ȡ��Ƶ�ļ�" << std::endl;
		video.release();
		return -1;
	}

	// ��ʼ��SDL_mixer
	if (Mix_Init(MIX_INIT_MP3) < 0) {
		std::cerr << "ERROR���޷���ʼ��SDL_mixer:" << Mix_GetError() << std::endl;
		video.release();
		return -1;
	}

	// ������Ƶ�豸
	if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
		std::cerr << "ERROR: �޷�������Ƶ�豸:" << Mix_GetError() << std::endl;
		video.release();
		Mix_Quit();
		return -1;
	}

	//������Ƶ�ļ�
	music = Mix_LoadMUS(audioName.c_str());
	if (music == NULL) {
		std::cerr << "ERROR: �޷�������Ƶ�ļ�:" << Mix_GetError() << std::endl;
		video.release();
		Mix_CloseAudio();
		Mix_Quit();
		return -1;
	}

	// ������Ƶ��Ϣ
	videoFPS	= video.get(cv::CAP_PROP_FPS);
	videoWidth	= video.get(cv::CAP_PROP_FRAME_WIDTH);
	videoHeight	= video.get(cv::CAP_PROP_FRAME_HEIGHT);

	return 0;
}

// ������Դ���˳�
void quit(int exitValue) {
	video.release();
	Mix_HaltMusic();
	Mix_FreeMusic(music);
	Mix_CloseAudio();
	Mix_Quit();
	remove(audioName.c_str());
	puts("\33[0m\n\33[?25h"); // �ָ�����̨����
	exit(exitValue);
}

// �����ַ����ߴ�
void get_size(int& width, int& height) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	width = (csbi.srWindow.Right - csbi.srWindow.Left) << 1;
	height = (csbi.srWindow.Bottom - csbi.srWindow.Top) << 1;

	double aspectRatio = (double)csbi.dwSize.X / width / csbi.dwSize.Y * height;

	// ������߱�
	if ((double)videoWidth / width > aspectRatio * videoHeight / height) {
		height = aspectRatio * width / videoWidth * videoHeight;
		height = MIN(height, videoHeight);
		height &= ~1; // ȷ���߶�Ϊż��
	} else {
		width = (double)height / videoHeight * videoWidth / aspectRatio;
		width = MIN(width, videoWidth);
	}
}

// ������ɫ�����Ƿ񳬹���ֵ
bool cmp(cv::Vec3b x, cv::Vec3b y, int threshold) {
	int dr = x[2] - y[2];
	int dg = x[1] - y[1];
	int db = x[0] - y[0];
	int distSq = dr * dr + dg * dg + db * db;
	return distSq > threshold * threshold;
}

// ��ӡ��ǰ֡
int render_frame(cv::Mat &lastFrame, unsigned long long &lastIdx) {
	// ��ȡ��ǰʱ������Ӧ��֡�ı��
	unsigned long long idx = videoFPS * currentTime;
	
	if (idx < 0 || idx >= video.get(cv::CAP_PROP_FRAME_COUNT)) return 1;
	if (lastIdx == idx && lastIdx && !isPaused) return 0;

	cv::Mat frame;
	cv::Vec3b bgColor = {0, 0, 0};	// ������ɫ
	cv::Vec3b txtColor = {0, 0, 0};	// �ı���ɫ
	std::string output = "";
	bool firstFrame = true;
	bool colorChanged = true;
	long long lost = idx - lastIdx;
	int threshold = MIN(pow(lost, eps), 128); // ���ݶ�֡��������ɫ�����ж���ֵ
	int width, height;
	get_size(width, height);

	// ��ȡ��ǰ֡
	if (lost > 0) {
		for (int i = 0; i < lost; ++i) {
			if (!video.grab()) {
				puts("\33[0m\n\33[?25h"); // �ָ�����̨����
				std::cerr << "ERROR: ��ȡ��Ƶʱ����" << std::endl;
				quit(-1);
			}
		}
	} else {
		video.set(cv::CAP_PROP_POS_FRAMES, idx);
	}
	
	if (!video.retrieve(frame)) {
		puts("\33[0m\n\33[?25h");
		std::cerr << "ERROR: ������Ƶʱ����" << std::endl;
		quit(-1);
	}

	cv::resize(frame, frame, {width, height});

	// ����ߴ�ı�������
	if (lastFrame.cols != width || lastFrame.rows != height) {
		lastFrame = cv::Mat(width, height, CV_8UC1, cv::Scalar(0, 0, 0));
		puts("\33c");
	}

	// �����ַ���
	for (int i = 0; i < height; i += 2) {
		for (int j = 0; j < width; ++j) {
			cv::Vec3b nbgColor = frame.at<cv::Vec3b>(i, j);
			cv::Vec3b ntxtColor = frame.at<cv::Vec3b>(i + 1, j);

			if (!cmp(nbgColor, lastFrame.at<cv::Vec3b>(i, j), threshold) && 
					!cmp(ntxtColor, lastFrame.at<cv::Vec3b>(i + 1, j), threshold)) { // ��ɫδ�ı�������
				frame.at<cv::Vec3b>(i, j) = lastFrame.at<cv::Vec3b>(i, j);
				frame.at<cv::Vec3b>(i + 1, j) = lastFrame.at<cv::Vec3b>(i + 1, j);
				firstFrame = true;
				continue;
			}
			
			if (firstFrame) {
				firstFrame = colorChanged = false;
				output += "\33[" + std::to_string(i / 2 + 1) + ';' + std::to_string(j + 1) + 'H'; // �ƶ����
			}
			
			if (cmp(nbgColor, bgColor, threshold)) { // �ı䱳����ɫ
				output += "\33[48;2;" + std::to_string(nbgColor[2]) + ';'
					+ std::to_string(nbgColor[1]) + ';'
					+ std::to_string(nbgColor[0]) + 'm';
				bgColor = nbgColor;
			} else {
				frame.at<cv::Vec3b>(i, j) = bgColor;
			}

			if (cmp(ntxtColor, txtColor, threshold)) { // �ı��ı���ɫ
				output += "\33[38;2;" + std::to_string(ntxtColor[2]) + ';'
					+ std::to_string(ntxtColor[1]) + ';'
					+ std::to_string(ntxtColor[0]) + 'm';
				txtColor = ntxtColor;
			} else {
				frame.at<cv::Vec3b>(i + 1, j) = txtColor;
			}
			output += "�{";
		}
		if (!firstFrame && i + 2 < height) output += '\n';
	}

	// ��ӡ�ַ���
	if (!colorChanged) puts(output.c_str());

	lastFrame = frame;
	lastIdx = idx;
	return 0;
}

// �����¼�����
void handle_keyboard() {
	if (!_kbhit()) return;
	
	int ch = _getch();

	switch (ch) {
	case PAUSE_KEY:		// ��ͣ��ָ�����
		isPaused = !isPaused;
		if (isPaused) Mix_PauseMusic();
		else Mix_ResumeMusic();
		break;
	case ESC_KEY:		// �˳�����
		quit(0);
		break;
	case FORWARD_KEY:	// �����Ƶ
		currentTime += skipInterval;
		if (currentTime < Mix_MusicDuration(music)) {
			Mix_SetMusicPosition(currentTime);
		} else {
			quit(0);
		}
		if (!isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		}
		break;
	case BACKWARD_KEY:	// ������Ƶ
		currentTime -= skipInterval;
		if (currentTime > 0) {
			Mix_SetMusicPosition(currentTime);
		} else {
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

// ��Ƶ����������
void play() {
	cv::Mat lastFrame = cv::Mat(1, 1, CV_8UC1, cv::Scalar(0, 0, 0));
	unsigned long long lastIdx = 0;

	// ���������ع��
	puts("\33c\n\33[?25l\n\33[30;40m");
	
	// ��������
	if (Mix_PlayMusic(music, -1) == -1) {
		puts("\33[0m\n\33[?25h");
		printf("ERROR: �޷�������Ƶ %s\n", Mix_GetError());
		quit(-1);
	}

	startTime = std::chrono::system_clock::now();

	// ��Ƶ������ѭ��
	while (true) {
		handle_keyboard();
		if (isPaused) {
			pauseTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - currentTime;
		} else {
			currentTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count() - pauseTime;
		}
		if (render_frame(lastFrame, lastIdx)) quit(0);
	}
}

int main(int argc, char *argv[]) {
	std::string videoName = "";
	// ��ȡ��Ƶ�ļ�����
	if (argc < 2) {
		std::cout << "��������Ƶ·����";
		getline(std::cin, videoName);
	} else {
		videoName += argv[1];
	}

	if (videoName[0] == '\"') {
		videoName.erase(videoName.end() - 1);
		videoName.erase(videoName.begin());
	}

	if (initialize_resources(videoName)) {
		system("pause");
		return -1;
	}

	play();

	return 0;
}

